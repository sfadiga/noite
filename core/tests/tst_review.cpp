#include "helpers.h"
#include "registry.h"

using namespace noite;
using namespace noite::test;

namespace {

// Creates a note as if written `daysAgo` days before t0 (the store's clock goes back and returns).
QString noteFrom(Fixture& f, const QString& text, qint64 agoMs)
{
    f.clock->set(t0() - agoMs);
    const QString id = f.store->createScratch(html({text})).id;
    f.clock->set(t0());
    return id;
}

QStringList ids(const QList<ReviewItem>& items)
{
    QStringList out;
    for (const ReviewItem& item : items)
        out << item.note.id;
    return out;
}

} // namespace

class ReviewTest : public QObject {
    Q_OBJECT

private slots:
    void queueOrderAndReasons()
    {
        auto f = fixture();
        const QString overdueOld = noteFrom(*f, "overdue old", 0);
        f->store->setDue(overdueOld, QDate(2026, 10, 10));
        const QString overdueNew = noteFrom(*f, "overdue new", 0);
        f->store->setDue(overdueNew, QDate(2026, 10, 14));
        const QString today1 = noteFrom(*f, "today one", 2000);
        f->store->setDue(today1, today());
        const QString today2 = noteFrom(*f, "today two", 1000);
        f->store->setDue(today2, today());
        const QString staleOld = noteFrom(*f, "stale old", 20 * kDayMs);
        const QString staleNew = noteFrom(*f, "stale new", 8 * kDayMs);
        noteFrom(*f, "fresh", 0);
        const QString upcoming = noteFrom(*f, "upcoming", 30 * kDayMs);
        f->store->setDue(upcoming, QDate(2026, 10, 20));
        f->store->markDone(noteFrom(*f, "done", 30 * kDayMs));
        f->store->trash(noteFrom(*f, "trashed", 30 * kDayMs));

        const QList<ReviewItem> queue = f->store->reviewQueue(today(), 7);
        QCOMPARE(ids(queue), QStringList({overdueOld, overdueNew, today1, today2, staleOld, staleNew}));
        const QList<ReviewReason> reasons = {ReviewReason::Overdue, ReviewReason::Overdue,  ReviewReason::DueToday,
                                             ReviewReason::DueToday, ReviewReason::Stale, ReviewReason::Stale};
        const QList<int> days = {5, 1, 0, 0, 20, 8};
        for (int i = 0; i < queue.size(); ++i) {
            QCOMPARE(queue[i].reason, reasons[i]);
            QCOMPARE(queue[i].days, days[i]);
        }
    }

    // Review Focus 4.
    void staleBoundaryIsExactlyNDays()
    {
        auto f = fixture();
        const QString exactly = noteFrom(*f, "exactly", 7 * kDayMs);
        noteFrom(*f, "almost", 7 * kDayMs - 1);
        const QList<ReviewItem> queue = f->store->reviewQueue(today(), 7);
        QCOMPARE(ids(queue), QStringList({exactly}));
        QCOMPARE(queue[0].days, 7);
    }

    // M8c F2: a deferred note raises nothing, not even in the fallback.
    void deferredNotesRaiseNothing()
    {
        auto f = fixture();
        const QString overdue = noteFrom(*f, "overdue", 20 * kDayMs);
        f->store->setDue(overdue, QDate(2026, 10, 10));
        f->store->defer(overdue);
        f->store->defer(noteFrom(*f, "stale", 20 * kDayMs));
        QVERIFY(f->store->reviewQueue(today(), 7).isEmpty());
        QCOMPARE(f->store->reviewCounts(today()).overdue, 0);
    }

    void touchResetsStaleClock()
    {
        auto f = fixture();
        const QString stale = noteFrom(*f, "stale", 20 * kDayMs);
        QCOMPARE(f->store->reviewQueue(today(), 7).size(), 1);
        f->store->touch(stale);
        QVERIFY(f->store->reviewQueue(today(), 7).isEmpty());
        QCOMPARE(f->store->get(stale).modifiedAt, t0());
    }

    // Undo of Keep puts the stale clock back where it was.
    void setModifiedAtRestoresStaleClock()
    {
        auto f = fixture();
        const QString stale = noteFrom(*f, "stale", 20 * kDayMs);
        const qint64 before = f->store->get(stale).modifiedAt;
        f->store->touch(stale);
        f->store->setModifiedAt(stale, before);
        QCOMPARE(f->store->get(stale).modifiedAt, before);
        QCOMPARE(f->store->reviewQueue(today(), 7).size(), 1);
        NOITE_EXPECT_ERROR(ErrorCode::NotFound, f->store->setModifiedAt("missing", before));
    }

    void touchRejectsTrashedAndMissing()
    {
        auto f = fixture();
        NOITE_EXPECT_ERROR(ErrorCode::NotFound, f->store->touch("missing"));
        const QString trashed = noteFrom(*f, "t", 0);
        f->store->trash(trashed);
        try {
            f->store->touch(trashed);
            QFAIL("touch on a trashed note must throw");
        } catch (const Error&) {
        }
    }

    void lastReviewDayRoundTripsAndSurvivesSettings()
    {
        auto f = fixture();
        QCOMPARE(f->store->lastReviewDay(), std::optional<QDate>());
        f->store->setLastReviewDay(QDate(2026, 10, 15));
        QCOMPARE(f->store->lastReviewDay(), std::optional<QDate>(QDate(2026, 10, 15)));
        f->store->saveSettings(f->store->settings());
        QCOMPARE(f->store->lastReviewDay(), std::optional<QDate>(QDate(2026, 10, 15)));
    }

    void staleDaysDefaultAndValidation()
    {
        auto f = fixture();
        QCOMPARE(Settings{}.staleDays, 7);
        Settings s = f->store->settings();
        s.staleDays = 0;
        NOITE_EXPECT_ERROR(ErrorCode::InvalidSetting, f->store->saveSettings(s));
        s.staleDays = 91;
        NOITE_EXPECT_ERROR(ErrorCode::InvalidSetting, f->store->saveSettings(s));
        s.staleDays = 90;
        f->store->saveSettings(s);
        QCOMPARE(f->store->settings().staleDays, 90);
    }
};

NOITE_TEST(ReviewTest)
#include "tst_review.moc"
