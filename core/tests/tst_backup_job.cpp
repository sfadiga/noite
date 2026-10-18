#include "helpers.h"
#include "registry.h"

#include "db.h"

#include <noite/backup.h>

#include <QDir>
#include <QFile>

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
    }
};

NOITE_TEST(BackupJobTest)
#include "tst_backup_job.moc"
