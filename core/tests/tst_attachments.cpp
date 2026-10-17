#include "helpers.h"
#include "registry.h"

#include <noite/content.h>

#include <QDir>
#include <QFile>

using namespace noite;
using namespace noite::test;

namespace {

QStringList files(const QString& dir)
{
    return QDir(dir).entryList(QDir::Files);
}

void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

} // namespace

class AttachmentsTest : public QObject {
    Q_OBJECT

private slots:
    void putDedupesAndNamesByHash()
    {
        auto f = fixture();
        const QString id = f->store->putAttachment("abc", "PNG");
        QCOMPARE(f->store->putAttachment("abc", "png"), id);
        QVERIFY(content::isValidAttachmentId(id));
        QVERIFY(id.endsWith(".png"));
        QCOMPARE(files(f->store->dataDir().attachmentsDir()), QStringList({id}));
        QFile stored(f->store->attachmentPath(id));
        QVERIFY(stored.open(QIODevice::ReadOnly));
        QCOMPARE(stored.readAll(), QByteArray("abc"));
    }

    void badExtensionBecomesBin()
    {
        auto f = fixture();
        QVERIFY(f->store->putAttachment("x", "../evil").endsWith(".bin"));
        QVERIFY(f->store->putAttachment("y", "").endsWith(".bin"));
    }

    void rejectsOversized()
    {
        auto f = fixture();
        NOITE_EXPECT_ERROR(ErrorCode::AttachmentTooLarge, f->store->putAttachment(QByteArray(kMaxAttachmentBytes + 1, '\0'), "bin"));
        QVERIFY(files(f->store->dataDir().attachmentsDir()).isEmpty());
    }

    void attachmentPathRejectsTraversal()
    {
        auto f = fixture();
        for (const QString& bad : {QString("../noite.db"), QString("..\\x"), QString("abc.png"), QString()})
            NOITE_EXPECT_ERROR(ErrorCode::InvalidAttachmentId, f->store->attachmentPath(bad));
        QCOMPARE(f->store->attachmentPath(fakeAttachmentId()), f->store->dataDir().attachmentsDir() + "/" + fakeAttachmentId());
    }

    void retentionBoundaries()
    {
        auto f = fixture();
        const QString id = f->store->createScratch(html({"x"})).id;
        f->store->trash(id);
        f->clock->set(t0() + 29 * kDayMs);
        QCOMPARE(f->store->purgeTrash(30).scratchesDeleted, 0);
        f->clock->set(t0() + 30 * kDayMs);
        QCOMPARE(f->store->purgeTrash(30).scratchesDeleted, 1);
        NOITE_EXPECT_ERROR(ErrorCode::NotFound, f->store->get(id));
        QVERIFY(f->store->list(Tab::Trash, today(), "x").isEmpty());
    }

    void shortenedRetentionAppliesToExistingTrash()
    {
        auto f = fixture();
        const QString id = f->store->createScratch(html({"x"})).id;
        f->store->trash(id);
        f->clock->set(t0() + 8 * kDayMs);
        QCOMPARE(f->store->purgeTrash(7).scratchesDeleted, 1);
    }

    void retentionOutOfRangeRejected()
    {
        auto f = fixture();
        NOITE_EXPECT_ERROR(ErrorCode::InvalidSetting, f->store->purgeTrash(0));
        NOITE_EXPECT_ERROR(ErrorCode::InvalidSetting, f->store->purgeTrash(366));
    }

    void doneIsNeverPurged()
    {
        auto f = fixture();
        const QString id = f->store->createScratch(html({"x"})).id;
        f->store->markDone(id);
        f->clock->set(t0() + 400 * kDayMs);
        QCOMPARE(f->store->purgeTrash(30).scratchesDeleted, 0);
        QCOMPARE(f->store->get(id).status, Status::Done);
    }

    // Spec §9's first test: a clone shares the original's image; purging the original keeps it.
    void cloneKeepsSharedImageAfterOriginalPurged()
    {
        auto f = fixture();
        const QString image = f->store->putAttachment("png-bytes", "png");
        setMtime(f->store->attachmentPath(image), t0() - 2 * kDayMs);
        const QString original = f->store->createScratch(htmlWithImage("pic", image)).id;
        const QString clone = f->store->cloneScratch(original).id;
        f->store->trash(original);
        f->clock->set(t0() + 31 * kDayMs);

        const PurgeReport report = f->store->purgeTrash(30);
        QCOMPARE(report.scratchesDeleted, 1);
        QCOMPARE(report.attachmentsDeleted, 0);
        QVERIFY(QFile::exists(f->store->attachmentPath(image)));
        QVERIFY(content::attachmentRefs(f->store->get(clone).html).contains(image));
    }

    void unreferencedOldAttachmentDeleted()
    {
        auto f = fixture();
        const QString id = f->store->putAttachment("orphan", "png");
        setMtime(f->store->attachmentPath(id), t0() - 2 * kDayMs);
        QCOMPARE(f->store->purgeTrash(30).attachmentsDeleted, 1);
        QVERIFY(!QFile::exists(f->store->attachmentPath(id)));
    }

    // Review Focus 4: pasted while autosave is still pending, so nothing references it yet.
    void unreferencedFreshAttachmentSurvives()
    {
        auto f = fixture();
        const QString id = f->store->putAttachment("just pasted", "png");
        setMtime(f->store->attachmentPath(id), t0() - 3'600'000);
        QCOMPARE(f->store->purgeTrash(30).attachmentsDeleted, 0);
        QVERIFY(QFile::exists(f->store->attachmentPath(id)));
    }

    // Review I1: pasting bytes that already exist must restart the grace period, or the
    // purge can delete the file while the new note's autosave is still pending.
    void rePastedOldAttachmentSurvivesPurge()
    {
        auto f = fixture();
        const QString image = f->store->putAttachment("same bytes", "png");
        const QString original = f->store->createScratch(htmlWithImage("a", image)).id;
        setMtime(f->store->attachmentPath(image), t0() - 40 * kDayMs);
        f->store->trash(original);
        f->clock->set(t0() + 35 * kDayMs);

        QCOMPARE(f->store->putAttachment("same bytes", "png"), image); // pasted again, not saved yet
        const PurgeReport report = f->store->purgeTrash(30);
        QCOMPARE(report.scratchesDeleted, 1);
        QVERIFY(QFile::exists(f->store->attachmentPath(image)));
    }

    void staleTempFilesCleaned()
    {
        auto f = fixture();
        const QString dir = f->store->dataDir().attachmentsDir();
        writeFile(dir + "/stale.tmp", "x");
        setMtime(dir + "/stale.tmp", t0() - 2 * kDayMs);
        writeFile(dir + "/fresh.tmp", "y");
        setMtime(dir + "/fresh.tmp", t0() - 3'600'000);
        f->store->purgeTrash(30);
        QCOMPARE(files(dir), QStringList({"fresh.tmp"}));
    }

    void purgeDeletesBundleLeftEmpty()
    {
        auto f = fixture();
        const QString a = f->store->createScratch(html({"a"})).id;
        const QString b = f->store->createScratch(html({"b"})).id;
        f->store->bundleTogether(a, b);
        f->store->moveToBundle(b, std::nullopt);
        f->store->trash(a);
        QCOMPARE(f->store->listBundles().size(), 1);
        f->clock->set(t0() + 31 * kDayMs);
        f->store->purgeTrash(30);
        QVERIFY(f->store->listBundles().isEmpty());
    }
};

NOITE_TEST(AttachmentsTest)
#include "tst_attachments.moc"
