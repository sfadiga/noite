#include "helpers.h"
#include "registry.h"

#include "radar/ReviewScheduler.h"

#include <noite/store.h>

#include <QSignalSpy>
#include <QTest>

using namespace noite;

namespace {

// A store with one overdue note, and a scheduler over a settable clock and lock state.
struct Sched {
    std::unique_ptr<test::Fixture> f = test::fixture();
    QDateTime now{QDate(2026, 10, 15), QTime(5, 59)};
    bool unlocked = true;
    ReviewScheduler scheduler{*f->store, [this] { return now; }, [this] { return unlocked; }};
    QSignalSpy due{&scheduler, &ReviewScheduler::reviewDue};

    explicit Sched(bool withNote = true)
    {
        if (withNote) {
            const ScratchId id = f->store->createScratch(test::html({"pay rent"})).id;
            f->store->setDue(id, QDate(2026, 10, 10));
        }
    }
    void at(int hour, int minute = 0, QDate day = QDate(2026, 10, 15)) { now = QDateTime(day, QTime(hour, minute)); }
};

} // namespace

class ReviewSchedulerTest : public QObject {
    Q_OBJECT

private slots:
    void beforeDayStartNothing()
    {
        Sched s;
        s.scheduler.check("test");
        QCOMPARE(s.due.size(), 0);
        QCOMPARE(s.scheduler.msUntilNextCheck(), 60'000);
        QCOMPARE(s.f->store->lastReviewDay(), std::optional<QDate>());
    }

    void afterDayStartOnce()
    {
        Sched s;
        s.at(6, 1);
        s.scheduler.check("test");
        QCOMPARE(s.due.size(), 1);
        QCOMPARE(s.f->store->lastReviewDay(), std::optional<QDate>(QDate(2026, 10, 15)));
        s.at(9);
        s.scheduler.check("test");
        QCOMPARE(s.due.size(), 1);
    }

    void lockedThenUnlocked()
    {
        Sched s;
        s.at(7);
        s.unlocked = false;
        s.scheduler.check("test");
        QCOMPARE(s.due.size(), 0);
        QCOMPARE(s.f->store->lastReviewDay(), std::optional<QDate>());
        s.unlocked = true;
        s.scheduler.check("test");
        QCOMPARE(s.due.size(), 1);
    }

    void emptyQueueMarksSilently()
    {
        Sched s(false);
        s.at(7);
        s.scheduler.check("test");
        QCOMPARE(s.due.size(), 0);
        QCOMPARE(s.f->store->lastReviewDay(), std::optional<QDate>(QDate(2026, 10, 15)));
    }

    // M8d: every check says in the log what started it and what it decided.
    void eachCheckLogsItsTriggerAndOutcome()
    {
        Sched s;
        QTest::ignoreMessage(QtInfoMsg, "review: start · before the day start (06:00)");
        s.scheduler.check("start");
        s.at(7);
        s.unlocked = false;
        QTest::ignoreMessage(QtInfoMsg, "review: resume · locked, waits for the unlock");
        s.scheduler.check("resume");
        s.unlocked = true;
        QTest::ignoreMessage(QtInfoMsg, "review: unlock · shown · 1 overdue, 0 due today, 0 stale, 0 undated");
        s.scheduler.check("unlock");
        QTest::ignoreMessage(QtInfoMsg, "review: timer · already shown today");
        s.scheduler.fireTimerForTest();
    }

    void noActiveNoteLogsNothingToReview()
    {
        Sched s(false);
        s.at(7);
        QTest::ignoreMessage(QtInfoMsg, "review: start · nothing to review");
        s.scheduler.check("start");
        QCOMPARE(s.due.size(), 0);
    }

    // M8d (the author's call): with nothing overdue, due or stale, the review still opens.
    void undatedNotesAloneStillOpen()
    {
        Sched s(false);
        s.f->store->createScratch(test::html({"just a thought"}));
        s.at(7);
        QTest::ignoreMessage(QtInfoMsg, "review: start · shown · 0 overdue, 0 due today, 0 stale, 1 undated");
        s.scheduler.check("start");
        QCOMPARE(s.due.size(), 1);
    }

    void nextDayAgain()
    {
        Sched s;
        s.at(7);
        s.scheduler.check("test");
        s.at(6, 30, QDate(2026, 10, 16));
        s.scheduler.check("test");
        QCOMPARE(s.due.size(), 2);
    }

    // Review Focus 1: the day starts while the PC is in use.
    void timerAtDayStartTriggersOnce()
    {
        Sched s;
        s.at(5);
        s.scheduler.check("test");
        QCOMPARE(s.scheduler.msUntilNextCheck(), 3'600'000);
        s.at(6);
        s.scheduler.fireTimerForTest();
        QCOMPARE(s.due.size(), 1);
        s.scheduler.fireTimerForTest();
        QCOMPARE(s.due.size(), 1);
        QCOMPARE(s.scheduler.msUntilNextCheck(), 24 * 3'600'000); // tomorrow's day start
    }
};

NOITE_TEST(ReviewSchedulerTest)
#include "tst_review_scheduler.moc"
