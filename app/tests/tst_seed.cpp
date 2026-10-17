#include "helpers.h"
#include "registry.h"

#include "Seed.h"

#include <noite/store.h>

#include <QTest>

#include <algorithm>

using namespace noite;

class SeedTest : public QObject {
    Q_OBJECT

private slots:
    void seedCreatesMixedNotes()
    {
        auto f = test::fixture();
        seedBoard(*f->store, 200, test::today(), 42, [&](qint64 ms) { f->clock->set(ms); });

        const auto all = f->store->list(Tab::All, test::today());
        const auto done = f->store->list(Tab::Completed, test::today());
        const auto trash = f->store->list(Tab::Trash, test::today());
        QCOMPARE(all.size() + done.size() + trash.size(), 200);
        QVERIFY(!done.isEmpty());
        QVERIFY(!trash.isEmpty());

        const auto today = f->store->list(Tab::Today, test::today());
        QVERIFY(std::any_of(today.begin(), today.end(), [](const ScratchSummary& s) { return s.due.has_value(); }));
        QVERIFY(std::any_of(all.begin(), all.end(), [](const ScratchSummary& s) { return s.firstImage.has_value(); }));
        QVERIFY(std::any_of(all.begin(), all.end(), [](const ScratchSummary& s) { return s.color.has_value(); }));

        const auto [oldest, newest] = std::minmax_element(
            all.begin(), all.end(), [](const ScratchSummary& a, const ScratchSummary& b) { return a.createdAt < b.createdAt; });
        QVERIFY(newest->createdAt - oldest->createdAt > 30 * kDayMs);
    }
};

NOITE_TEST(SeedTest)
#include "tst_seed.moc"
