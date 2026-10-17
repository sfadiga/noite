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
};

NOITE_TEST(LifecycleTest)
#include "tst_lifecycle.moc"
