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
        QVERIFY(spy.at(0).at(1).toString().contains(absent));
        QVERIFY(!runner.busy());
    }

    void busyIgnoresSecondStart()
    {
        auto f = test::fixture();
        QTemporaryDir dest;
        ThreadBackupRunner runner;
        QSignalSpy spy(&runner, &BackupRunner::finished);

        runner.start(jobFor(*f->store, dest.path()));
        runner.start(jobFor(*f->store, dest.path()));
        QVERIFY(spy.wait(10'000));
        QTest::qWait(300);
        QCOMPARE(spy.count(), 1);
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
        for (int i = 0; i < 4; ++i) {
            QByteArray bytes(5 * 1024 * 1024, 0);
            QRandomGenerator::global()->fillRange(reinterpret_cast<quint32*>(bytes.data()), bytes.size() / 4);
            f->store->putAttachment(bytes, "bin");
        }
        QTemporaryDir dest;
        {
            ThreadBackupRunner runner;
            runner.start(jobFor(*f->store, dest.path()));
            QElapsedTimer timer;
            timer.start();
            runner.cancelAndWait();
            QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
            QVERIFY(!runner.busy());
        }
        const QString snaps = snapshotsDir(*f->store, dest.path());
        const QStringList dbs = entries(snaps, "*.db");
        QVERIFY(dbs.size() <= 1);
        for (const QString& db : dbs) {
            backup::verify(snaps + "/" + db);
        }
        QVERIFY(entries(snaps, "*.tmp").isEmpty());
    }
};

NOITE_TEST(BackupRunnerTest)
#include "tst_backup_runner.moc"
