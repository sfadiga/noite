#include "helpers.h"
#include "registry.h"

using namespace noite;
using namespace noite::test;

class BundlesTest : public QObject {
    Q_OBJECT

private slots:
    void dropCreatesBundleNamedAfterTarget()
    {
        auto f = fixture();
        const Scratch a = f->store->createScratch(html({"Groceries"}));
        const Scratch b = f->store->createScratch(html({"milk"}));
        const BundleId id = f->store->bundleTogether(a.id, b.id);
        QVERIFY(f->store->get(a.id).bundleId == id);
        QVERIFY(f->store->get(b.id).bundleId == id);
        const QList<Bundle> bundles = f->store->listBundles();
        QCOMPARE(bundles.size(), 1);
        QCOMPARE(bundles[0].id, id);
        QCOMPARE(bundles[0].name, QString("Groceries"));
        QCOMPARE(bundles[0].createdAt, t0());
    }

    void untitledTargetNamesBundleBundle()
    {
        auto f = fixture();
        const Scratch a = f->store->createScratch(htmlWithImage("", fakeAttachmentId()));
        const Scratch b = f->store->createScratch(html({"b"}));
        f->store->bundleTogether(a.id, b.id);
        QCOMPARE(f->store->listBundles()[0].name, QString("Bundle"));
    }

    void dropOntoBundledJoinsIt()
    {
        auto f = fixture();
        const Scratch a = f->store->createScratch(html({"a"}));
        const Scratch b = f->store->createScratch(html({"b"}));
        const Scratch c = f->store->createScratch(html({"c"}));
        const BundleId id = f->store->bundleTogether(a.id, b.id);
        QCOMPARE(f->store->bundleTogether(a.id, c.id), id);
        QVERIFY(f->store->get(c.id).bundleId == id);
        QCOMPARE(f->store->listBundles().size(), 1);
    }

    void draggingOutOfOneBundleIntoAnotherDeletesTheEmptiedOne()
    {
        auto f = fixture();
        const Scratch a = f->store->createScratch(html({"a"}));
        const Scratch b = f->store->createScratch(html({"b"}));
        const Scratch c = f->store->createScratch(html({"c"}));
        const Scratch d = f->store->createScratch(html({"d"}));
        f->store->bundleTogether(a.id, b.id);
        const BundleId second = f->store->bundleTogether(c.id, d.id);
        f->store->moveToBundle(a.id, second);
        f->store->moveToBundle(b.id, second);
        const QList<Bundle> bundles = f->store->listBundles();
        QCOMPARE(bundles.size(), 1);
        QCOMPARE(bundles[0].id, second);
    }

    void dropOntoSelfRejected()
    {
        auto f = fixture();
        const Scratch a = f->store->createScratch(html({"a"}));
        NOITE_EXPECT_ERROR(ErrorCode::InvalidTransition, f->store->bundleTogether(a.id, a.id));
        QVERIFY(f->store->listBundles().isEmpty());
    }

    void lastMemberLeavingDeletesBundle()
    {
        auto f = fixture();
        const Scratch a = f->store->createScratch(html({"a"}));
        const Scratch b = f->store->createScratch(html({"b"}));
        f->store->bundleTogether(a.id, b.id);
        f->store->moveToBundle(a.id, std::nullopt);
        QCOMPARE(f->store->listBundles().size(), 1);
        f->store->moveToBundle(b.id, std::nullopt);
        QVERIFY(f->store->listBundles().isEmpty());
        QVERIFY(!f->store->get(b.id).bundleId);
    }

    // Undo needs the very bundle back: same id, name and creation time (M3b final review).
    void recreateBundleRestoresIdNameAndTime()
    {
        auto f = fixture();
        const Scratch a = f->store->createScratch(html({"a"}));
        const Scratch b = f->store->createScratch(html({"b"}));
        const BundleId id = f->store->bundleTogether(a.id, b.id);
        f->store->renameBundle(id, "Trip");
        const Bundle saved = f->store->listBundles().first();
        f->store->moveToBundle(a.id, std::nullopt);
        f->store->moveToBundle(b.id, std::nullopt);
        QVERIFY(f->store->listBundles().isEmpty());
        f->clock->advance(5000);

        f->store->recreateBundle(saved);
        f->store->moveToBundle(a.id, id);
        const QList<Bundle> bundles = f->store->listBundles();
        QCOMPARE(bundles.size(), 1);
        QCOMPARE(bundles[0].id, saved.id);
        QCOMPARE(bundles[0].name, QString("Trip"));
        QCOMPARE(bundles[0].createdAt, saved.createdAt);
        QCOMPARE(f->store->get(a.id).bundleId, std::optional<BundleId>(id));
        f->store->recreateBundle(saved); // already there: no-op
        QCOMPARE(f->store->listBundles().size(), 1);
    }

    // The board's "Unbundle": every member, any status, leaves; the bundle goes.
    void dissolveBundleReleasesAllMembers()
    {
        auto f = fixture();
        const Scratch a = f->store->createScratch(html({"a"}));
        const Scratch b = f->store->createScratch(html({"b"}));
        const Scratch c = f->store->createScratch(html({"c"}));
        const BundleId id = f->store->bundleTogether(a.id, b.id);
        f->store->moveToBundle(c.id, id);
        f->store->markDone(c.id);
        QStringList released = f->store->dissolveBundle(id);
        released.sort();
        QStringList expected = {a.id, b.id, c.id};
        expected.sort();
        QCOMPARE(released, expected);
        for (const Scratch& s : {a, b, c})
            QVERIFY(!f->store->get(s.id).bundleId);
        QVERIFY(f->store->listBundles().isEmpty());
        NOITE_EXPECT_ERROR(ErrorCode::NotFound, f->store->dissolveBundle(id));
    }

    void trashedMemberKeepsBundleAlive()
    {
        auto f = fixture();
        const Scratch a = f->store->createScratch(html({"a"}));
        const Scratch b = f->store->createScratch(html({"b"}));
        f->store->bundleTogether(a.id, b.id);
        f->store->trash(a.id);
        f->store->moveToBundle(b.id, std::nullopt);
        QCOMPARE(f->store->listBundles().size(), 1);
    }

    void moveToMissingBundleIsNotFound()
    {
        auto f = fixture();
        const Scratch a = f->store->createScratch(html({"a"}));
        NOITE_EXPECT_ERROR(ErrorCode::NotFound, f->store->moveToBundle(a.id, BundleId("nope")));
    }

    void renameBundleWorks()
    {
        auto f = fixture();
        const Scratch a = f->store->createScratch(html({"a"}));
        const Scratch b = f->store->createScratch(html({"b"}));
        const BundleId id = f->store->bundleTogether(a.id, b.id);
        f->store->renameBundle(id, "Errands");
        QCOMPARE(f->store->listBundles()[0].name, QString("Errands"));
        NOITE_EXPECT_ERROR(ErrorCode::NotFound, f->store->renameBundle("nope", "x"));
    }

    void cloneFollowsD20()
    {
        auto f = fixture();
        const Scratch o = f->store->createScratch(html({"Plan #a"}));
        const Scratch other = f->store->createScratch(html({"other"}));
        f->store->setManualTags(o.id, {"b"});
        f->store->setColor(o.id, Color::Blue);
        f->store->setDue(o.id, QDate(2026, 10, 20));
        f->store->bundleTogether(o.id, other.id);
        f->store->markDone(o.id);
        const Scratch original = f->store->get(o.id);

        f->clock->advance(5000);
        const Scratch c = f->store->cloneScratch(o.id);
        QVERIFY(c.id != o.id);
        QCOMPARE(c.html, original.html);
        QCOMPARE(c.tags, original.tags);
        QCOMPARE(c.manualTags, original.manualTags);
        QVERIFY(c.color == Color::Blue);
        QVERIFY(c.bundleId == original.bundleId);
        QVERIFY(!c.due);
        QCOMPARE(c.status, Status::Active);
        QVERIFY(!c.doneAt);
        QCOMPARE(c.createdAt, t0() + 5000);
        QCOMPARE(c.modifiedAt, t0() + 5000);
    }
};

NOITE_TEST(BundlesTest)
#include "tst_bundles.moc"
