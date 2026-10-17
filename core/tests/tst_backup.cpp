#include "helpers.h"
#include "registry.h"

#include <noite/backup.h>

#include <QDir>
#include <QFile>

using namespace noite;
using namespace noite::test;

namespace {

QString snapshotsDir(const QString& dest) { return dest + "/noite-backups/snapshots"; }
QString backupAttachmentsDir(const QString& dest) { return dest + "/noite-backups/attachments"; }

QStringList entries(const QString& dir, const QString& pattern = "*")
{
    return QDir(dir).entryList({pattern}, QDir::Files, QDir::Name);
}

} // namespace

class BackupTest : public QObject {
    Q_OBJECT

private slots:
    void snapshotLayoutAndContents()
    {
        auto f = fixture();
        const QString image = f->store->putAttachment("png", "png");
        f->store->createScratch(htmlWithImage("pic", image));
        QTemporaryDir dest;

        const QString path = backup::snapshot(*f->store, dest.path());
        QCOMPARE(path, snapshotsDir(dest.path()) + "/noite-20261015T080000000Z.db");
        QVERIFY(QFile::exists(path));
        QCOMPARE(entries(backupAttachmentsDir(dest.path())), QStringList({image}));
        QVERIFY(entries(snapshotsDir(dest.path()), "*.tmp").isEmpty());
        backup::verify(path);
    }

    void missingDestIsAnError()
    {
        auto f = fixture();
        QTemporaryDir tmp;
        const QString gone = tmp.filePath("gone");
        NOITE_EXPECT_ERROR(ErrorCode::BackupDirMissing, backup::snapshot(*f->store, gone));
        QVERIFY(!QFileInfo::exists(gone));
    }

    void snapshotRestoresIntoFreshStore()
    {
        auto f = fixture();
        const QString image = f->store->putAttachment("png", "png");
        const Scratch original = f->store->createScratch(htmlWithImage("pic", image));
        QTemporaryDir dest;
        const QString path = backup::snapshot(*f->store, dest.path());

        QTemporaryDir restored;
        const QString root = restored.filePath("data");
        QVERIFY(QDir().mkpath(root + "/attachments"));
        QVERIFY(QFile::copy(path, root + "/noite.db"));
        QVERIFY(QFile::copy(backupAttachmentsDir(dest.path()) + "/" + image, root + "/attachments/" + image));

        auto store = Store::open(DataDir::acquire(root), f->clock);
        QCOMPARE(store->get(original.id).html, original.html);
        QVERIFY(QFile::exists(store->attachmentPath(image)));
    }

    void verifyDetectsMissingAttachment()
    {
        auto f = fixture();
        const QString image = f->store->putAttachment("png", "png");
        f->store->createScratch(htmlWithImage("pic", image));
        QTemporaryDir dest;
        const QString path = backup::snapshot(*f->store, dest.path());
        QVERIFY(QFile::remove(backupAttachmentsDir(dest.path()) + "/" + image));
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
        const backup::PruneReport report = backup::prune(dest.path(), 2);
        QCOMPARE(report.snapshotsDeleted, 2);
        QCOMPARE(entries(snapshotsDir(dest.path()), "*.db"), made.mid(2));
    }

    void pruneSweepsUnreferencedBackupAttachments()
    {
        auto f = fixture();
        const QString image = f->store->putAttachment("png", "png");
        const Scratch s = f->store->createScratch(htmlWithImage("pic", image));
        QTemporaryDir dest;
        backup::snapshot(*f->store, dest.path());
        f->store->updateContent(s.id, html({"no image now"}));
        for (int i = 0; i < 2; ++i) {
            f->clock->advance(60'000);
            backup::snapshot(*f->store, dest.path());
        }
        const backup::PruneReport report = backup::prune(dest.path(), 2);
        QCOMPARE(report.attachmentsDeleted, 1);
        QVERIFY(entries(backupAttachmentsDir(dest.path())).isEmpty());
    }

    void pruneRemovesStrayTmp()
    {
        auto f = fixture();
        QTemporaryDir dest;
        backup::snapshot(*f->store, dest.path());
        QFile stray(snapshotsDir(dest.path()) + "/noite-x.db.tmp");
        QVERIFY(stray.open(QIODevice::WriteOnly));
        stray.close();
        backup::prune(dest.path(), 14);
        QVERIFY(entries(snapshotsDir(dest.path()), "*.tmp").isEmpty());
        QCOMPARE(entries(snapshotsDir(dest.path()), "*.db").size(), 1);
    }
};

NOITE_TEST(BackupTest)
#include "tst_backup.moc"
