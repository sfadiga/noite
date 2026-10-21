#include "helpers.h"
#include "registry.h"

using namespace noite;
using namespace noite::test;

class LifecycleTest : public QObject {
    Q_OBJECT

private slots:
    void doneThenReopen()
    {
        auto f = fixture();
        const Scratch s = f->store->createScratch(html({"task"}));
        f->store->markDone(s.id);
        Scratch g = f->store->get(s.id);
        QCOMPARE(g.status, Status::Done);
        QVERIFY(g.doneAt == t0());
        f->store->reopen(s.id);
        g = f->store->get(s.id);
        QCOMPARE(g.status, Status::Active);
        QVERIFY(!g.doneAt);
    }

    void restoreReturnsPreviousStatus()
    {
        auto f = fixture();
        const Scratch active = f->store->createScratch(html({"a"}));
        f->store->trash(active.id);
        Scratch g = f->store->get(active.id);
        QCOMPARE(g.status, Status::Trashed);
        QVERIFY(g.trashedAt == t0());
        f->store->restore(active.id);
        g = f->store->get(active.id);
        QCOMPARE(g.status, Status::Active);
        QVERIFY(!g.trashedAt);

        const Scratch done = f->store->createScratch(html({"d"}));
        f->store->markDone(done.id);
        f->clock->advance(5000);
        f->store->trash(done.id);
        f->store->restore(done.id);
        g = f->store->get(done.id);
        QCOMPARE(g.status, Status::Done);
        QVERIFY(g.doneAt == t0());
    }

    void invalidTransitions()
    {
        auto f = fixture();
        const Scratch s = f->store->createScratch(html({"x"}));
        NOITE_EXPECT_ERROR(ErrorCode::InvalidTransition, f->store->reopen(s.id));
        NOITE_EXPECT_ERROR(ErrorCode::InvalidTransition, f->store->restore(s.id));
        f->store->trash(s.id);
        NOITE_EXPECT_ERROR(ErrorCode::InvalidTransition, f->store->trash(s.id));
        NOITE_EXPECT_ERROR(ErrorCode::InvalidTransition, f->store->markDone(s.id));
        NOITE_EXPECT_ERROR(ErrorCode::InvalidTransition, f->store->updateContent(s.id, html({"y"})));
        NOITE_EXPECT_ERROR(ErrorCode::InvalidTransition, f->store->setColor(s.id, Color::Red));
        NOITE_EXPECT_ERROR(ErrorCode::NotFound, f->store->markDone("missing"));
    }

    // M8c F1.
    void deferAndBringBack()
    {
        auto f = fixture();
        const Scratch s = f->store->createScratch(html({"someday"}));
        f->clock->advance(1000);
        f->store->defer(s.id);
        QCOMPARE(f->store->get(s.id).status, Status::Deferred);
        QCOMPARE(f->store->get(s.id).modifiedAt, t0() + 1000);
        f->store->updateContent(s.id, html({"someday, edited"})); // editing keeps it deferred
        QCOMPARE(f->store->get(s.id).status, Status::Deferred);
        f->store->bringBack(s.id);
        QCOMPARE(f->store->get(s.id).status, Status::Active);
    }

    // F7: only active notes are deferred; only deferred ones come back.
    void deferOnlyFromActive()
    {
        auto f = fixture();
        const Scratch s = f->store->createScratch(html({"x"}));
        NOITE_EXPECT_ERROR(ErrorCode::InvalidTransition, f->store->bringBack(s.id));
        f->store->markDone(s.id);
        NOITE_EXPECT_ERROR(ErrorCode::InvalidTransition, f->store->defer(s.id));
        f->store->trash(s.id);
        NOITE_EXPECT_ERROR(ErrorCode::InvalidTransition, f->store->defer(s.id));
        NOITE_EXPECT_ERROR(ErrorCode::InvalidTransition, f->store->bringBack(s.id));
        NOITE_EXPECT_ERROR(ErrorCode::NotFound, f->store->defer("missing"));
    }

    // F6: done and trash from Deferred; restore goes back to Deferred.
    void doneAndTrashFromDeferred()
    {
        auto f = fixture();
        const Scratch a = f->store->createScratch(html({"a"}));
        f->store->defer(a.id);
        f->store->markDone(a.id);
        QCOMPARE(f->store->get(a.id).status, Status::Done);

        const Scratch b = f->store->createScratch(html({"b"}));
        f->store->defer(b.id);
        f->store->trash(b.id);
        QCOMPARE(f->store->get(b.id).status, Status::Trashed);
        f->store->restore(b.id);
        QCOMPARE(f->store->get(b.id).status, Status::Deferred);
    }
};

NOITE_TEST(LifecycleTest)
#include "tst_lifecycle.moc"
