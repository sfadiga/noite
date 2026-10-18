#include "helpers.h"
#include "registry.h"

#include "db.h"

#include <noite/content.h>

using namespace noite;
using namespace noite::test;

namespace {

qint64 scalar(Store& store, const QString& sql, const QString& id = {})
{
    detail::Stmt s = store.db().prepare(sql);
    if (!id.isEmpty())
        s.bind(1, id);
    s.step();
    return s.int64(0);
}

qint64 rowCount(Store& store)
{
    return scalar(store, "SELECT count(*) FROM attachments");
}

qint64 touchedAt(Store& store, const QString& id)
{
    return scalar(store, "SELECT touched_at FROM attachments WHERE id = ?", id);
}

// Ages a row, as if it was pasted at `ms`.
void setTouchedAt(Store& store, const QString& id, qint64 ms)
{
    detail::Stmt s = store.db().prepare("UPDATE attachments SET touched_at = ? WHERE id = ?");
    s.bind(1, ms);
    s.bind(2, id);
    s.step();
}

qint64 dbBytes(Store& store)
{
    return scalar(store, "PRAGMA page_count") * scalar(store, "PRAGMA page_size");
}

} // namespace

class AttachmentsTest : public QObject {
    Q_OBJECT

private slots:
    void putDedupsAndStampsTouchedAt()
    {
        auto f = fixture();
        const QString id = f->store->putAttachment("abc", "PNG");
        QCOMPARE(f->store->putAttachment("abc", "png"), id);
        QVERIFY(content::isValidAttachmentId(id));
        QVERIFY(id.endsWith(".png"));
        QCOMPARE(rowCount(*f->store), 1);
        QCOMPARE(f->store->attachmentBytes(id), QByteArray("abc"));
        QCOMPARE(touchedAt(*f->store, id), t0());
        f->clock->advance(1000);
        f->store->putAttachment("abc", "png");
        QCOMPARE(touchedAt(*f->store, id), t0() + 1000);
        QVERIFY(!QDir(f->root() + "/attachments").exists()); // nothing on disk
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
        QCOMPARE(rowCount(*f->store), 0);
    }

    void attachmentBytesErrors()
    {
        auto f = fixture();
        for (const QString& bad : {QString("../noite.db"), QString("..\\x"), QString("abc.png"), QString()})
            NOITE_EXPECT_ERROR(ErrorCode::InvalidAttachmentId, f->store->attachmentBytes(bad));
        NOITE_EXPECT_ERROR(ErrorCode::AttachmentMissing, f->store->attachmentBytes(fakeAttachmentId()));
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
        setTouchedAt(*f->store, image, t0() - 2 * kDayMs);
        const QString original = f->store->createScratch(htmlWithImage("pic", image)).id;
        const QString clone = f->store->cloneScratch(original).id;
        f->store->trash(original);
        f->clock->set(t0() + 31 * kDayMs);

        const PurgeReport report = f->store->purgeTrash(30);
        QCOMPARE(report.scratchesDeleted, 1);
        QCOMPARE(report.attachmentsDeleted, 0);
        QCOMPARE(f->store->attachmentBytes(image), QByteArray("png-bytes"));
        QVERIFY(content::attachmentRefs(f->store->get(clone).html).contains(image));
    }

    // Review Focus 4: big blobs leave the file when they are purged.
    void purgeSweepsRowsAfterGraceAndShrinksFile()
    {
        auto f = fixture();
        const QString id = f->store->putAttachment(QByteArray(4 * 1024 * 1024, 'z'), "bin");
        setTouchedAt(*f->store, id, t0() - 2 * kDayMs);
        const qint64 before = dbBytes(*f->store);
        QVERIFY(before > 4 * 1024 * 1024);
        QCOMPARE(f->store->purgeTrash(30).attachmentsDeleted, 1);
        QCOMPARE(rowCount(*f->store), 0);
        QVERIFY2(before - dbBytes(*f->store) >= 3 * 1024 * 1024,
                 qPrintable(QString("before %1 after %2").arg(before).arg(dbBytes(*f->store))));
    }

    // Pasted while autosave is still pending, so nothing references it yet.
    void unreferencedFreshAttachmentSurvives()
    {
        auto f = fixture();
        const QString id = f->store->putAttachment("just pasted", "png");
        setTouchedAt(*f->store, id, t0() - 3'600'000);
        QCOMPARE(f->store->purgeTrash(30).attachmentsDeleted, 0);
        QCOMPARE(f->store->attachmentBytes(id), QByteArray("just pasted"));
    }

    // Review Focus 3: pasting bytes that already exist must restart the grace period, or the
    // purge can delete the row while the new note's autosave is still pending.
    void rePastedOldAttachmentSurvivesPurge()
    {
        auto f = fixture();
        const QString image = f->store->putAttachment("same bytes", "png");
        const QString original = f->store->createScratch(htmlWithImage("a", image)).id;
        setTouchedAt(*f->store, image, t0() - 40 * kDayMs);
        f->store->trash(original);
        f->clock->set(t0() + 35 * kDayMs);

        QCOMPARE(f->store->putAttachment("same bytes", "png"), image); // pasted again, not saved yet
        const PurgeReport report = f->store->purgeTrash(30);
        QCOMPARE(report.scratchesDeleted, 1);
        QCOMPARE(report.attachmentsDeleted, 0);
        QCOMPARE(f->store->attachmentBytes(image), QByteArray("same bytes"));
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
