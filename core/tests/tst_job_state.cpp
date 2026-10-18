#include "helpers.h"
#include "registry.h"

#include <noite/data_dir.h>

using namespace noite;
using namespace noite::test;

class JobStateTest : public QObject {
    Q_OBJECT

private slots:
    void emptyAtFirst()
    {
        auto f = fixture();
        QCOMPARE(f->store->jobState(), JobState{});
    }

    void roundTripsEveryField()
    {
        auto f = fixture();
        JobState s;
        s.lastPurgeDay = QDate(2026, 10, 14);
        s.lastBackupDay = QDate(2026, 10, 13);
        s.backupAlertDay = QDate(2026, 10, 12);
        s.backupFailingSinceMs = t0() - 5000;
        f->store->saveJobState(s);
        QCOMPARE(f->store->jobState(), s);
        reopen(*f);
        QCOMPARE(f->store->jobState(), s);
    }

    void settingsAndJobStateIndependent()
    {
        auto f = fixture();
        JobState s;
        s.lastBackupDay = QDate(2026, 10, 13);
        s.backupFailingSinceMs = 42;
        f->store->saveJobState(s);

        Settings settings = f->store->settings();
        settings.backupKeep = 5;
        f->store->saveSettings(settings);
        QCOMPARE(f->store->jobState(), s);

        f->store->setLastReviewDay(today());
        QCOMPARE(f->store->jobState(), s);

        JobState other;
        other.lastPurgeDay = QDate(2026, 10, 1);
        f->store->saveJobState(other);
        QCOMPARE(f->store->settings().backupKeep, 5);
        QCOMPARE(f->store->lastReviewDay(), std::optional<QDate>(today()));
    }

    void driveFolderNamesHasMyDriveAndMeuDrive()
    {
        const QStringList names = googleDriveFolderNames();
        QVERIFY(names.contains("My Drive"));
        QVERIFY(names.contains("Meu Drive"));
        QVERIFY(!names.contains("Shared drives"));
    }
};

NOITE_TEST(JobStateTest)
#include "tst_job_state.moc"
