#include "helpers.h"
#include "registry.h"

#include <noite/clock.h>
#include <noite/data_dir.h>

#include <QDir>
#include <QTemporaryDir>

using namespace noite;

class DataDirTest : public QObject {
    Q_OBJECT

private slots:
    void acquireCreatesLayout()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        const DataDir dir = DataDir::acquire(root);
        QVERIFY(QDir(dir.attachmentsDir()).exists());
        QVERIFY(QDir(dir.logsDir()).exists());
        QVERIFY(QDir(dir.migrationBackupsDir()).exists());
        QCOMPARE(dir.dbPath(), root + "/noite.db");
        QCOMPARE(dir.root(), root);
    }

    void secondAcquireFailsUntilReleased()
    {
        QTemporaryDir tmp;
        {
            const DataDir first = DataDir::acquire(tmp.path());
            NOITE_EXPECT_ERROR(ErrorCode::AlreadyRunning, DataDir::acquire(tmp.path()));
        }
        const DataDir again = DataDir::acquire(tmp.path());
        QVERIFY(QDir(again.attachmentsDir()).exists());
    }

    void differentRootsCoexist()
    {
        QTemporaryDir tmp;
        const DataDir a = DataDir::acquire(tmp.filePath("a"));
        const DataDir b = DataDir::acquire(tmp.filePath("b"));
        QVERIFY(a.root() != b.root());
    }

    void movedDataDirKeepsTheLock()
    {
        QTemporaryDir tmp;
        DataDir first = DataDir::acquire(tmp.path());
        DataDir moved = std::move(first);
        NOITE_EXPECT_ERROR(ErrorCode::AlreadyRunning, DataDir::acquire(tmp.path()));
        QCOMPARE(moved.root(), tmp.path());
    }

    void detectsSyncFolders()
    {
        const QStringList od = {R"(C:\Users\u\OneDrive)"};
        QVERIFY(syncProviderFor(R"(G:\My Drive\noite\data)", {}) == SyncProvider::GoogleDrive);
        QVERIFY(syncProviderFor(R"(C:\Users\u\Google Drive\noite)", {}) == SyncProvider::GoogleDrive);
        QVERIFY(syncProviderFor(R"(d:\my drive\x)", {}) == SyncProvider::GoogleDrive);
        QVERIFY(syncProviderFor("C:/Users/u/Dropbox/noite", {}) == SyncProvider::Dropbox);
        QVERIFY(syncProviderFor(R"(c:\users\u\onedrive\noite)", od) == SyncProvider::OneDrive);
        QVERIFY(!syncProviderFor(R"(D:\tools\noite\data)", od));
        QVERIFY(!syncProviderFor(R"(C:\Users\u\OneDriveX\noite)", od));
    }

    // Review I2: real-world names the first version missed.
    void detectsLocalizedAndSharedSyncFolders()
    {        QVERIFY(syncProviderFor(R"(G:\Meu Drive\noite)", {}) == SyncProvider::GoogleDrive);
        QVERIFY(syncProviderFor(R"(G:\Mi unidad\noite)", {}) == SyncProvider::GoogleDrive);
        QVERIFY(syncProviderFor(R"(G:\Shared drives\Team\noite)", {}) == SyncProvider::GoogleDrive);
        QVERIFY(syncProviderFor(R"(G:\Drives compartilhados\Equipe\noite)", {}) == SyncProvider::GoogleDrive);
        QVERIFY(syncProviderFor(R"(C:\Users\u\Dropbox (Personal)\noite)", {}) == SyncProvider::Dropbox);
        QVERIFY(syncProviderFor(R"(C:\Users\u\Dropbox (Acme Inc)\noite)", {}) == SyncProvider::Dropbox);
        QVERIFY(!syncProviderFor(R"(D:\dropboxer\noite)", {}));
        // Google Drive for Desktop mounts a volume labelled "Google Drive", whatever the folder names.
        QVERIFY(syncProviderFor(R"(G:\Whatever\noite)", {}, "Google Drive") == SyncProvider::GoogleDrive);
        QVERIFY(!syncProviderFor(R"(D:\tools\noite)", {}, "DATA"));
    }

    void fixedClockAdvances()
    {
        FixedClock clock(1000);
        QCOMPARE(clock.nowMs(), qint64(1000));
        clock.advance(500);
        QCOMPARE(clock.nowMs(), qint64(1500));
        clock.set(42);
        QCOMPARE(clock.nowMs(), qint64(42));
        QVERIFY(SystemClock().nowMs() > noite::test::t0() - kDayMs * 3650);
    }
};

NOITE_TEST(DataDirTest)
#include "tst_data_dir.moc"
