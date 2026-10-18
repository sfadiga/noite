#include "helpers.h"
#include "registry.h"

#include "db.h"

#include <noite/backup.h>

#include <QDir>
#include <QFile>

using namespace noite;
using namespace noite::test;

namespace {

// Each installation backs up into its own subfolder: <dest>/noite-backups/<installId>/.
QString installDir(const Store& store, const QString& dest) { return dest + "/noite-backups/" + store.installId(); }
QString snapshotsDir(const Store& store, const QString& dest) { return installDir(store, dest) + "/snapshots"; }

QStringList entries(const QString& dir, const QString& pattern = "*")
{
    return QDir(dir).entryList({pattern}, QDir::Files, QDir::Name);
}

void touchFile(const QString& path, qint64 mtimeMs)
{
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }
    setMtime(path, mtimeMs);
}

} // namespace

class BackupTest : public QObject {
    Q_OBJECT

private slots:
    // A3: a snapshot is one self-contained .db; restoring it needs nothing else.
    void snapshotIsSelfContained()
    {
        auto f = fixture();
        const QString image = f->store->putAttachment("png", "png");
        const Scratch original = f->store->createScratch(htmlWithImage("pic", image));
        QTemporaryDir dest;

        const QString path = backup::snapshot(*f->store, dest.path());
        QCOMPARE(path, snapshotsDir(*f->store, dest.path()) + "/noite-20261015T080000000Z.db");
        QVERIFY(QFile::exists(path));
        QVERIFY(!QDir(installDir(*f->store, dest.path()) + "/attachments").exists());
        QVERIFY(entries(snapshotsDir(*f->store, dest.path()), "*.tmp").isEmpty());
        backup::verify(path);

        QTemporaryDir restored;
        const QString root = restored.filePath("data");
        QVERIFY(QDir().mkpath(root));
        QVERIFY(QFile::copy(path, root + "/noite.db"));
        auto store = Store::open(DataDir::acquire(root), f->clock);
        QCOMPARE(store->get(original.id).html, original.html);
        QCOMPARE(store->attachmentBytes(image), QByteArray("png"));
    }

    void missingDestIsAnError()
    {
        auto f = fixture();
        QTemporaryDir tmp;
        const QString gone = tmp.filePath("gone");
        NOITE_EXPECT_ERROR(ErrorCode::BackupDirMissing, backup::snapshot(*f->store, gone));
        QVERIFY(!QFileInfo::exists(gone));
    }

    void verifyMissingRowFails()
    {
        auto f = fixture();
        const QString image = f->store->putAttachment("png", "png");
        f->store->createScratch(htmlWithImage("pic", image));
        QTemporaryDir dest;
        const QString path = backup::snapshot(*f->store, dest.path());
        {
            detail::Db snap = detail::Db::open(path);
            snap.exec("DELETE FROM attachments");
        }
        NOITE_EXPECT_ERROR(ErrorCode::BackupInvalid, backup::verify(path));
    }

    void pruneKeepsNewest()
    {
        auto f = fixture();
        f->store->createScratch(html({"x"}));
        QTemporaryDir dest;
        QStringList made;
        for (int i = 0; i < 4; ++i) {
            made << QFileInfo(backup::snapshot(*f->store, dest.path())).fileName();
            f->clock->advance(60'000);
        }
        const backup::PruneReport report = backup::prune(*f->store, dest.path(), 2);
        QCOMPARE(report.snapshotsDeleted, 2);
        QCOMPARE(entries(snapshotsDir(*f->store, dest.path()), "*.db"), made.mid(2));
    }

    // Review I3: a temp file may be another machine's snapshot in progress (synced folder);
    // only old leftovers are removed.
    void pruneRemovesOnlyStaleTmp()
    {
        auto f = fixture();
        QTemporaryDir dest;
        backup::snapshot(*f->store, dest.path());
        const QString snaps = snapshotsDir(*f->store, dest.path());
        touchFile(snaps + "/noite-old.db.tmp", t0() - 2 * 3'600'000);
        touchFile(snaps + "/noite-fresh.db.tmp", t0() - 60'000);
        backup::prune(*f->store, dest.path(), 14);
        QCOMPARE(entries(snaps, "*.tmp"), QStringList({"noite-fresh.db.tmp"}));
        QCOMPARE(entries(snaps, "*.db").size(), 1);
    }

    // Review I3: two installations (two PCs on one Google account, or two portable folders)
    // backing up to the same folder must not prune each other's snapshots.
    void twoInstallationsSharingAFolderKeepTheirOwnSnapshots()
    {
        auto a = fixture();
        auto b = fixture();
        a->store->createScratch(html({"from a"}));
        b->store->createScratch(html({"from b"}));
        QTemporaryDir dest;
        for (int i = 0; i < 2; ++i) {
            backup::snapshot(*a->store, dest.path());
            backup::snapshot(*b->store, dest.path());
            a->clock->advance(60'000);
            b->clock->advance(60'000);
        }
        backup::prune(*a->store, dest.path(), 1);
        QCOMPARE(entries(snapshotsDir(*a->store, dest.path()), "*.db").size(), 1);
        QCOMPARE(entries(snapshotsDir(*b->store, dest.path()), "*.db").size(), 2);
    }
};

NOITE_TEST(BackupTest)
#include "tst_backup.moc"
