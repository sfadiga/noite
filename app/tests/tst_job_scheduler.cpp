#include "helpers.h"
#include "registry.h"

#include "jobs/JobScheduler.h"

#include <noite/store.h>

#include <sqlite3.h>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace noite;

namespace {

QStringList g_log;
QtMessageHandler g_previousHandler = nullptr;

void captureHandler(QtMsgType, const QMessageLogContext&, const QString& message)
{
    g_log << message;
}

// Records the jobs it is given; the test emits the outcome. Like the real runner, busy() stays
// true from start() until the outcome is delivered.
class FakeBackupRunner final : public BackupRunner {
public:
    void start(const backup::Job& job) override
    {
        jobs << job;
        busy_ = true;
    }
    bool busy() const override { return busy_; }
    void cancelAndWait() override { busy_ = false; }

    void succeed(qint64 bytes = 1'000'000, int pruned = 0)
    {
        busy_ = false;
        emit finished(backup::Result{"snap.db", bytes, pruned});
    }
    void fail(const QString& message, ErrorCode code = ErrorCode::BackupDirMissing)
    {
        busy_ = false;
        emit failed(code, message);
    }

    QList<backup::Job> jobs;

private:
    bool busy_ = false;
};

// A store and a scheduler over a settable clock; the store's clock moves with it (same instant).
struct Sched {
    std::unique_ptr<test::Fixture> f;
    QTemporaryDir dest;
    QDateTime now{QDate(2026, 10, 15), QTime(9, 0)};
    FakeBackupRunner runner;
    std::unique_ptr<JobScheduler> scheduler;

    explicit Sched(bool withFolder = true, std::unique_ptr<test::Fixture> fixture = test::fixture())
        : f(std::move(fixture))
    {
        f->clock->set(now.toMSecsSinceEpoch());
        if (withFolder)
            configure([this](Settings& s) { s.backupDir = dest.path(); });
        scheduler = std::make_unique<JobScheduler>(*f->store, [this] { return now; }, runner);
    }

    void configure(const std::function<void(Settings&)>& edit)
    {
        Settings s = f->store->settings();
        edit(s);
        f->store->saveSettings(s);
    }
    void at(QDate day, int hour = 9, int minute = 0)
    {
        now = QDateTime(day, QTime(hour, minute));
        f->clock->set(now.toMSecsSinceEpoch());
    }
    void setState(const std::function<void(JobState&)>& edit)
    {
        JobState state = f->store->jobState();
        edit(state);
        f->store->saveJobState(state);
    }
    JobState state() const { return f->store->jobState(); }
};

QDate oct(int day)
{
    return QDate(2026, 10, day);
}

} // namespace

class JobSchedulerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { g_previousHandler = qInstallMessageHandler(captureHandler); }
    void cleanupTestCase() { qInstallMessageHandler(g_previousHandler); }
    void init() { g_log.clear(); }

    void purgesOncePerDay()
    {
        Sched s;
        s.f->clock->set(s.now.addDays(-31).toMSecsSinceEpoch());
        const ScratchId id = s.f->store->createScratch(test::html({"old"})).id;
        s.f->store->trash(id);
        s.f->clock->set(s.now.toMSecsSinceEpoch());
        QSignalSpy purged(s.scheduler.get(), &JobScheduler::purged);

        s.scheduler->check();
        QCOMPARE(purged.size(), 1);
        QCOMPARE(purged.at(0).at(0).value<PurgeReport>().scratchesDeleted, 1);
        QCOMPARE(s.state().lastPurgeDay, std::optional<QDate>(oct(15)));
        QVERIFY(g_log.contains("purge: 1 notes, 0 attachments"));

        s.scheduler->check();
        QCOMPARE(purged.size(), 1);

        s.at(oct(16), 0, 30);
        s.scheduler->check();
        QCOMPARE(purged.size(), 2);
        QCOMPARE(purged.at(1).at(0).value<PurgeReport>().scratchesDeleted, 0);
    }

    void dailyBackupOncePerDay()
    {
        Sched s;
        s.scheduler->check();
        QCOMPARE(s.runner.jobs.size(), 1);
        QCOMPARE(s.runner.jobs[0].dest, s.dest.path());
        QCOMPARE(s.runner.jobs[0].keep, 14);
        QCOMPARE(s.runner.jobs[0].nowMs, s.now.toMSecsSinceEpoch());
        s.runner.succeed();
        QCOMPARE(s.state().lastBackupDay, std::optional<QDate>(oct(15)));
        s.scheduler->check();
        QCOMPARE(s.runner.jobs.size(), 1);
    }

    void catchesUpAfterDaysOff()
    {
        Sched s;
        s.setState([](JobState& st) { st.lastBackupDay = oct(10); });
        s.scheduler->check();
        QCOMPARE(s.runner.jobs.size(), 1);
    }

    void weeklyDueOnDaySeven()
    {
        Sched s;
        s.configure([](Settings& st) { st.backupFrequency = BackupFrequency::Weekly; });
        s.setState([](JobState& st) { st.lastBackupDay = oct(9); });
        s.scheduler->check();
        QCOMPARE(s.runner.jobs.size(), 0);
        s.at(oct(16));
        s.scheduler->check();
        QCOMPARE(s.runner.jobs.size(), 1);
    }

    void weeklyWithFutureLastDayIsDue()
    {
        Sched s;
        s.configure([](Settings& st) { st.backupFrequency = BackupFrequency::Weekly; });
        s.setState([](JobState& st) { st.lastBackupDay = oct(20); });
        s.scheduler->check();
        QCOMPARE(s.runner.jobs.size(), 1);
    }

    void noFolderNoBackupNoToast()
    {
        Sched s(false);
        QSignalSpy toast(s.scheduler.get(), &JobScheduler::toast);
        s.scheduler->check();
        s.scheduler->check();
        QCOMPARE(s.runner.jobs.size(), 0);
        QCOMPARE(toast.size(), 0);
        QVERIFY(!s.scheduler->backUpNow());
        QCOMPARE(s.runner.jobs.size(), 0);
        QCOMPARE(g_log.count("backup: skipped, no folder"), 1); // once per day
    }

    void failureRetriesNextCheck()
    {
        Sched s;
        QSignalSpy toast(s.scheduler.get(), &JobScheduler::toast);
        s.scheduler->check();
        s.runner.fail("backup folder not found: G:/x");
        QCOMPARE(toast.size(), 0);
        QCOMPARE(s.state().backupFailingSinceMs, std::optional<qint64>(s.now.toMSecsSinceEpoch()));
        QVERIFY(g_log.contains("backup: failed: backup folder not found: G:/x"));
        s.scheduler->check();
        QCOMPARE(s.runner.jobs.size(), 2);
    }

    void alertAfterThreeDaysOncePerDay()
    {
        Sched s;
        QSignalSpy toast(s.scheduler.get(), &JobScheduler::toast);
        s.setState([](JobState& st) { st.backupFailingSinceMs = QDateTime(oct(12), QTime(9, 0)).toMSecsSinceEpoch(); });

        s.at(oct(15), 8, 59);
        s.scheduler->check();
        s.runner.fail("no folder");
        QCOMPARE(toast.size(), 0);

        s.at(oct(15), 9, 0);
        s.scheduler->check();
        s.runner.fail("no folder");
        QCOMPARE(toast.size(), 1);
        QCOMPARE(toast.at(0).at(0).toString(), QString("noite"));
        QCOMPARE(toast.at(0).at(1).toString(), QString("Backups have been failing since Oct 12: no folder"));

        s.at(oct(15), 10, 0);
        s.scheduler->check();
        s.runner.fail("no folder");
        QCOMPARE(toast.size(), 1);

        s.at(oct(16), 9, 0);
        s.scheduler->check();
        s.runner.fail("no folder");
        QCOMPARE(toast.size(), 2);
    }

    void successClearsFailure()
    {
        Sched s;
        s.setState([](JobState& st) {
            st.backupFailingSinceMs = 123;
            st.backupAlertDay = oct(14);
        });
        s.scheduler->check();
        s.runner.succeed();
        QCOMPARE(s.state().backupFailingSinceMs, std::optional<qint64>());
        QCOMPARE(s.state().backupAlertDay, std::optional<QDate>());
    }

    void backUpNowToastsResult()
    {
        Sched s;
        QSignalSpy toast(s.scheduler.get(), &JobScheduler::toast);
        QVERIFY(s.scheduler->backUpNow());
        QCOMPARE(s.runner.jobs.size(), 1);
        s.runner.succeed(14'889'779);
        QCOMPARE(toast.size(), 1);
        QCOMPARE(toast.at(0).at(0).toString(), QString("noite"));
        QCOMPARE(toast.at(0).at(1).toString(), QString("Backed up · 14.2 MB"));
        QCOMPARE(s.state().lastBackupDay, std::optional<QDate>(oct(15)));

        QVERIFY(s.scheduler->backUpNow()); // ignores the schedule
        s.runner.fail("disk full");
        QCOMPARE(toast.size(), 2);
        QCOMPARE(toast.at(1).at(1).toString(), QString("Backup failed: disk full"));
    }

    void scheduledSuccessIsSilent()
    {
        Sched s;
        QSignalSpy toast(s.scheduler.get(), &JobScheduler::toast);
        s.scheduler->check();
        s.runner.succeed();
        QCOMPARE(toast.size(), 0);
        QVERIFY(g_log.filter("backup: ok snap.db · 1.0 MB · ").size() == 1);
    }

    void backUpNowWhileBusyRefused()
    {
        Sched s;
        QSignalSpy busy(s.scheduler.get(), &JobScheduler::busyChanged);
        s.scheduler->check();
        QVERIFY(s.scheduler->isBusy());
        QVERIFY(!s.scheduler->backUpNow());
        QCOMPARE(s.runner.jobs.size(), 1);
        s.runner.succeed();
        QVERIFY(!s.scheduler->isBusy());
        QCOMPARE(busy.size(), 2);
        QCOMPARE(busy.at(0).at(0).toBool(), true);
        QCOMPARE(busy.at(1).at(0).toBool(), false);
    }

    // trashRetentionDays 0 in the raw settings row makes purgeTrash refuse; fixing it lets the next check purge.
    void purgeFailureRetries()
    {
        auto f = test::fixture();
        f->store.reset();
        sqlite3* db = nullptr;
        QCOMPARE(sqlite3_open((f->root() + "/noite.db").toUtf8().constData(), &db), SQLITE_OK);
        QCOMPARE(sqlite3_exec(db, "INSERT INTO settings(key, value) VALUES('settings', '{\"trashRetentionDays\":0}')",
                              nullptr, nullptr, nullptr),
                 SQLITE_OK);
        test::reopen(*f);
        Sched s(false, std::move(f));
        QSignalSpy purged(s.scheduler.get(), &JobScheduler::purged);

        s.scheduler->check();
        QCOMPARE(purged.size(), 0);
        QCOMPARE(s.state().lastPurgeDay, std::optional<QDate>());
        QVERIFY(!g_log.filter("purge: failed: ").isEmpty());

        QCOMPARE(sqlite3_exec(db, "UPDATE settings SET value = '{\"trashRetentionDays\":30}' WHERE key = 'settings'",
                              nullptr, nullptr, nullptr),
                 SQLITE_OK);
        sqlite3_close(db);
        s.scheduler->check();
        QCOMPARE(purged.size(), 1);
        QCOMPARE(s.state().lastPurgeDay, std::optional<QDate>(oct(15)));
    }
};

NOITE_TEST(JobSchedulerTest)
#include "tst_job_scheduler.moc"
