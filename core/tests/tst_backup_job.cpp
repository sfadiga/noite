#include "helpers.h"
#include "registry.h"

#include "db.h"

#include <noite/backup.h>

#include <QDir>
#include <QFile>
#include <QRandomGenerator>

#include <atomic>
#include <thread>

using namespace noite;
using namespace noite::test;

namespace {

QString snapshotsDir(const Store& store, const QString& dest)
{
    return dest + "/noite-backups/" + store.installId() + "/snapshots";
}

QStringList entries(const QString& dir, const QString& pattern = "*")
{
    return QDir(dir).entryList({pattern}, QDir::Files, QDir::Name);
}

backup::Job jobFor(const Store& store, const QString& dest, int keep = 14)
{
    return {store.dataDir().dbPath(), store.installId(), dest, keep, store.nowMs()};
}

} // namespace

class BackupJobTest : public QObject {
    Q_OBJECT

private slots:
    void runWritesVerifiesAndPrunes()
    {
        auto f = fixture();
        f->store->createScratch(html({"x"}));
        QTemporaryDir dest;
        for (int i = 0; i < 3; ++i) {
            backup::snapshot(*f->store, dest.path());
            f->clock->advance(60'000);
        }
        const backup::Result r = backup::run(jobFor(*f->store, dest.path(), 2));
        const QString snaps = snapshotsDir(*f->store, dest.path());
        QVERIFY(r.path.startsWith(snaps + "/"));
        QVERIFY(QFile::exists(r.path));
        QCOMPARE(r.bytes, QFileInfo(r.path).size());
        QCOMPARE(r.pruned, 2);
        const QStringList dbs = entries(snaps, "*.db");
        QCOMPARE(dbs.size(), 2);
        QCOMPARE(dbs.last(), QFileInfo(r.path).fileName());
    }

    void runIsConsistentWhileWriting()
    {
        auto f = fixture();
        f->store->createScratch(html({"seed"}));
        QTemporaryDir dest;
        std::atomic<bool> failed{false};
        std::thread writer([&] {
            try {
                for (int i = 0; i < 200; ++i)
                    f->store->createScratch(html({QString("note %1").arg(i)}));
            } catch (...) {
                failed = true;
            }
        });
        backup::Result r;
        try {
            r = backup::run(jobFor(*f->store, dest.path()));
        } catch (...) {
            writer.join();
            throw;
        }
        writer.join();
        QVERIFY(!failed);

        detail::Db snap = detail::Db::open(r.path, true);
        detail::Stmt check = snap.prepare("PRAGMA integrity_check");
        QVERIFY(check.step());
        QCOMPARE(check.text(0), QString("ok"));
        detail::Stmt a = snap.prepare("SELECT COUNT(*) FROM scratches");
        detail::Stmt b = snap.prepare("SELECT COUNT(*) FROM scratch_fts");
        QVERIFY(a.step());
        QVERIFY(b.step());
        QCOMPARE(a.int64(0), b.int64(0));
    }

    void runToNonAsciiFolder()
    {
        auto f = fixture();
        f->store->createScratch(html({"x"}));
        QTemporaryDir tmp;
        const QString dest = tmp.filePath(QString::fromUtf8("Meu Drive/Notas Jo\xC3\xA3o"));
        QVERIFY(QDir().mkpath(dest));
        const backup::Result r = backup::run(jobFor(*f->store, dest));
        QVERIFY(QFile::exists(r.path));
        backup::verify(r.path);
    }

    void missingDestThrows()
    {
        auto f = fixture();
        QTemporaryDir tmp;
        NOITE_EXPECT_ERROR(ErrorCode::BackupDirMissing, backup::run(jobFor(*f->store, tmp.filePath("absent"))));
        QVERIFY(QDir(tmp.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    }

    void errorMessagesUseNativeSeparators()
    {
        auto f = fixture();
        QTemporaryDir tmp;
        const QString absent = tmp.filePath("absent");
        try {
            backup::run(jobFor(*f->store, absent));
            QFAIL("expected BackupDirMissing");
        } catch (const Error& error) {
            QVERIFY2(QString::fromUtf8(error.what()).endsWith(QDir::toNativeSeparators(absent)), error.what());
        }
    }

    void cancelledRunLeavesNoDb()
    {
        auto f = fixture();
        for (int i = 0; i < 50; ++i)
            f->store->createScratch(html({QString("note %1").arg(i)}));
        QTemporaryDir dest;
        std::atomic<bool> cancel{true};
        NOITE_EXPECT_ERROR(ErrorCode::Cancelled, backup::run(jobFor(*f->store, dest.path()), &cancel));
        const QString snaps = snapshotsDir(*f->store, dest.path());
        QVERIFY(entries(snaps, "*.db").isEmpty());
        QVERIFY(entries(snaps, "*.tmp").isEmpty());
    }

    // The progress handler must stop a VACUUM INTO that is already running.
    void cancelDuringVacuumStopsIt_data()
    {
        QTest::addColumn<bool>("blobs");
        QTest::newRow("rows") << false;
        QTest::newRow("blobs") << true;
    }

    void cancelDuringVacuumStopsIt()
    {
        QFETCH(bool, blobs);
        auto f = fixture();
        if (blobs) { // realistic: a few dozen ~1 MB attachments, few VM ops each
            for (int i = 0; i < 30; ++i) {
                QByteArray bytes(1'000'000, 0);
                QRandomGenerator::global()->fillRange(reinterpret_cast<quint32*>(bytes.data()), bytes.size() / 4);
                f->store->putAttachment(bytes, "bin");
            }
        } else {
            f->store->db().exec("CREATE TABLE junk(a, b); WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n "
                                "WHERE i < 400000) INSERT INTO junk SELECT i, hex(randomblob(16)) FROM n");
        }
        QTemporaryDir dest;
        const QString snaps = snapshotsDir(*f->store, dest.path());
        std::atomic<bool> cancel{false};
        std::atomic<bool> done{false};
        // Flips the flag once the copy is under way (its .tmp exists and has data).
        std::thread watcher([&] {
            while (!done) {
                if (!entries(snaps, "*.tmp").isEmpty()) {
                    cancel = true;
                    return;
                }
                std::this_thread::yield();
            }
        });
        bool cancelled = false;
        QString message;
        try {
            backup::run(jobFor(*f->store, dest.path()), &cancel);
        } catch (const Error& e) {
            cancelled = e.code() == ErrorCode::Cancelled;
            message = QString::fromUtf8(e.what());
        }
        done = true;
        watcher.join();
        QVERIFY(cancelled);
        // The interrupt itself, not the later checks.
        QVERIFY2(message.contains("during snapshot"), qPrintable(message));
        QVERIFY(entries(snaps, "*.db").isEmpty());
        QVERIFY(entries(snaps, "*.tmp").isEmpty());
    }

    void invalidSnapshotIsDeleted()
    {
        auto f = fixture();
        const Scratch s = f->store->createScratch(html({"x"}));
        // The store refuses unknown attachments, so plant the dangling reference in SQL.
        detail::Stmt u = f->store->db().prepare("UPDATE scratches SET content = ? WHERE id = ?");
        u.bind(1, QString("<p><img src=\"noite-att:%1\"/></p>").arg(fakeAttachmentId('b')));
        u.bind(2, s.id);
        u.step();
        QTemporaryDir dest;
        NOITE_EXPECT_ERROR(ErrorCode::BackupInvalid, backup::run(jobFor(*f->store, dest.path())));
        QVERIFY(entries(snapshotsDir(*f->store, dest.path()), "*.db").isEmpty());
        QVERIFY(entries(snapshotsDir(*f->store, dest.path()), "*.tmp").isEmpty());
    }
};

NOITE_TEST(BackupJobTest)
#include "tst_backup_job.moc"
