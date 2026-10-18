#include "registry.h"

#include "helpers.h"
#include "jobs/BackupRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace noite;

namespace {

backup::Job jobFor(const Store& store, const QString& dest)
{
    return {store.dataDir().dbPath(), store.installId(), dest, 14, store.nowMs()};
}

QString snapshotsDir(const Store& store, const QString& dest)
{
    return dest + "/noite-backups/" + store.installId() + "/snapshots";
}

QStringList entries(const QString& dir, const QString& filter)
{
    return QDir(dir).entryList({filter}, QDir::Files, QDir::Name);
}

// Enough blob data that the snapshot copy takes a while, like core's cancelDuringVacuumStopsIt.
void seedBlobs(Store& store)
{
    for (int i = 0; i < 30; ++i) {
        QByteArray bytes(1'000'000, 0);
        QRandomGenerator::global()->fillRange(reinterpret_cast<quint32*>(bytes.data()), bytes.size() / 4);
        store.putAttachment(bytes, "bin");
    }
}

} // namespace

class BackupRunnerTest : public QObject {
    Q_OBJECT
private slots:
    void finishesOnMainThread()
    {
        auto f = test::fixture();
        QTemporaryDir dest;
        ThreadBackupRunner runner;
        QThread* slotThread = nullptr;
        connect(&runner, &BackupRunner::finished, this, [&] { slotThread = QThread::currentThread(); });
        QSignalSpy spy(&runner, &BackupRunner::finished);

        runner.start(jobFor(*f->store, dest.path()));
        QVERIFY(spy.wait(10'000));
        QCOMPARE(slotThread, qApp->thread());
        const auto result = spy.at(0).at(0).value<backup::Result>();
        QVERIFY(QFile::exists(result.path));
        QVERIFY(!runner.busy());
    }

    void failureIsReported()
    {
        auto f = test::fixture();
        QTemporaryDir tmp;
        const QString absent = tmp.filePath("absent");
        ThreadBackupRunner runner;
        QSignalSpy spy(&runner, &BackupRunner::failed);

        runner.start(jobFor(*f->store, absent));
        QVERIFY(spy.wait(10'000));
        QCOMPARE(spy.at(0).at(0).value<ErrorCode>(), ErrorCode::BackupDirMissing);
        QVERIFY(spy.at(0).at(1).toString().contains(QDir::toNativeSeparators(absent)));
        QVERIFY(!runner.busy());
    }

    void busyIgnoresSecondStart()
    {
        auto f = test::fixture();
        QTemporaryDir dest;
        ThreadBackupRunner runner;
        QSignalSpy spy(&runner, &BackupRunner::finished);
        QSignalSpy failures(&runner, &BackupRunner::failed);

        runner.start(jobFor(*f->store, dest.path()));
        runner.start(jobFor(*f->store, dest.path()));
        QVERIFY(spy.wait(10'000));
        QTest::qWait(300);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(failures.count(), 0);
    }

    void runsAgainAfterFinishing()
    {
        auto f = test::fixture();
        QTemporaryDir dest;
        ThreadBackupRunner runner;
        QSignalSpy spy(&runner, &BackupRunner::finished);

        runner.start(jobFor(*f->store, dest.path()));
        QVERIFY(spy.wait(10'000));
        f->clock->advance(1000);
        runner.start(jobFor(*f->store, dest.path()));
        QVERIFY(spy.wait(10'000));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(entries(snapshotsDir(*f->store, dest.path()), "*.db").size(), 2);
    }

    void runsAgainAfterCancel()
    {
        auto f = test::fixture();
        QTemporaryDir dest;
        ThreadBackupRunner runner;
        QSignalSpy spy(&runner, &BackupRunner::finished);

        runner.start(jobFor(*f->store, dest.path()));
        runner.cancelAndWait();
        QVERIFY(!runner.busy());
        spy.clear();
        f->clock->advance(1000);
        runner.start(jobFor(*f->store, dest.path()));
        QVERIFY(spy.wait(10'000));
        QCOMPARE(spy.count(), 1);
    }

    void cancelAndWaitIsPrompt()
    {
        auto f = test::fixture();
        seedBlobs(*f->store);
        QTemporaryDir dest;
        const QString snaps = snapshotsDir(*f->store, dest.path());
        ThreadBackupRunner runner;
        int delivered = 0;
        connect(&runner, &BackupRunner::finished, this, [&] { ++delivered; });
        connect(&runner, &BackupRunner::failed, this, [&] { ++delivered; });

        runner.start(jobFor(*f->store, dest.path()));
        QTRY_VERIFY(!entries(snaps, "*.tmp").isEmpty()); // the copy is under way
        QElapsedTimer timer;
        timer.start();
        runner.cancelAndWait();
        QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(!runner.busy());
        QTest::qWait(200);
        QCOMPARE(delivered, 0);
        const QStringList dbs = entries(snaps, "*.db");
        for (const QString& db : dbs) {
            QVERIFY_THROWS_NO_EXCEPTION(backup::verify(snaps + "/" + db));
        }
        QVERIFY(dbs.size() <= 1);
        QVERIFY(entries(snaps, "*.tmp").isEmpty());
    }

    // An outcome from before cancelAndWait must never be attributed to the next run.
    void cancelledRunDeliversNothingAndRunnerRestarts()
    {
        auto f = test::fixture();
        seedBlobs(*f->store);
        QTemporaryDir dest;
        const QString snaps = snapshotsDir(*f->store, dest.path());
        ThreadBackupRunner runner;
        int failures = 0;
        connect(&runner, &BackupRunner::failed, this, [&] { ++failures; });
        QSignalSpy finished(&runner, &BackupRunner::finished);

        runner.start(jobFor(*f->store, dest.path()));
        QTRY_VERIFY(!entries(snaps, "*.tmp").isEmpty());
        runner.cancelAndWait();
        QTest::qWait(200);
        QCOMPARE(failures, 0);
        QCOMPARE(finished.count(), 0);

        f->clock->advance(1000);
        runner.start(jobFor(*f->store, dest.path()));
        QVERIFY(runner.busy());
        QVERIFY(finished.wait(20'000));
        QCOMPARE(finished.count(), 1);
        QCOMPARE(failures, 0);
        QVERIFY(!runner.busy());
    }
};

NOITE_TEST(BackupRunnerTest)
#include "tst_backup_runner.moc"
