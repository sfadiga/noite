#include "helpers.h"
#include "registry.h"

#include "editor/AttachmentStore.h"
#include "editor/NoteEdit.h"
#include "radar/ReviewWindow.h"

#include <noite/content.h>
#include <noite/store.h>

#include <QClipboard>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>

using namespace noite;

namespace {

// Overdue (due 10-10), due today, stale A (20 days), stale B (10 days): the review's order.
struct Review {
    std::unique_ptr<test::Fixture> f = test::fixture();
    StoreAttachments attachments{*f->store};
    ScratchId overdue, dueToday, staleA, staleB;
    std::unique_ptr<ReviewWindow> window;

    Review()
    {
        overdue = note("pay rent", 0);
        f->store->setDue(overdue, QDate(2026, 10, 10));
        dueToday = note("call bob", 0);
        f->store->setDue(dueToday, test::today());
        staleA = note("old idea", 20 * kDayMs);
        staleB = note("older plan", 10 * kDayMs);
        window = std::make_unique<ReviewWindow>(*f->store, attachments, [] { return test::today(); });
        window->start();
        window->show();
    }
    ScratchId note(const QString& text, qint64 agoMs)
    {
        f->clock->set(test::t0() - agoMs);
        const ScratchId id = f->store->createScratch(test::html({text})).id;
        f->clock->set(test::t0());
        return id;
    }
    Store& store() { return *f->store; }
    void key(int k, Qt::KeyboardModifiers mods = {}) { QTest::keyClick(window->editor(), Qt::Key(k), mods); }
    QString text(const ScratchId& id) { return content::plainText(store().get(id).html); }
};

} // namespace

class ReviewWindowTest : public QObject {
    Q_OBJECT

private slots:
    void startsAtFirstWithHeader()
    {
        Review r;
        QCOMPARE(r.window->position(), 1);
        QCOMPARE(r.window->total(), 4);
        QCOMPARE(r.window->currentId(), std::optional<ScratchId>(r.overdue));
        QCOMPARE(r.window->headerText(), QString("OVERDUE · 5 days"));
        QVERIFY(r.window->editor()->isReadOnly());
        QCOMPARE(r.window->editor()->toPlainText(), QString("pay rent"));
    }

    void keysActAndAdvance()
    {
        Review r;
        r.key(Qt::Key_D);
        QCOMPARE(r.store().get(r.overdue).status, Status::Done);
        QCOMPARE(r.window->position(), 2);
        QCOMPARE(r.window->headerText(), QString("DUE TODAY"));
        r.f->clock->advance(1000);
        r.key(Qt::Key_K);
        QCOMPARE(r.store().get(r.dueToday).modifiedAt, test::t0() + 1000);
        QCOMPARE(r.window->headerText(), QString("STALE · 20 days untouched"));
        r.key(Qt::Key_Delete);
        QCOMPARE(r.store().get(r.staleA).status, Status::Trashed);
        r.key(Qt::Key_S);
        r.key(Qt::Key_1);
        QCOMPARE(r.store().get(r.staleB).due, std::optional<QDate>(QDate(2026, 10, 16)));
        QVERIFY(r.window->isFinished());
        QCOMPARE(r.window->summaryText(), QString("Done 1 · Snoozed 1 · Kept 1 · Deferred 0 · Trashed 1"));
    }

    // Review Focus 3.
    void snoozeDates()
    {
        QCOMPARE(ReviewWindow::nextMonday(QDate(2026, 10, 15)), QDate(2026, 10, 19)); // Thursday
        QCOMPARE(ReviewWindow::nextMonday(QDate(2026, 10, 19)), QDate(2026, 10, 26)); // Monday: a week on
        QCOMPARE(ReviewWindow::nextMonday(QDate(2026, 10, 18)), QDate(2026, 10, 19)); // Sunday
        Review r;
        r.key(Qt::Key_S);
        r.key(Qt::Key_2);
        QCOMPARE(r.store().get(r.overdue).due, std::optional<QDate>(QDate(2026, 10, 19)));
    }

    // M8c F5: L defers the note; Left undoes it; the summary counts it.
    void lDefersAndCounts()
    {
        Review r;
        r.key(Qt::Key_L);
        QCOMPARE(r.store().get(r.overdue).status, Status::Deferred);
        QCOMPARE(r.window->position(), 2);
        r.key(Qt::Key_Left);
        QCOMPARE(r.store().get(r.overdue).status, Status::Active);
        r.key(Qt::Key_L);
        r.key(Qt::Key_K);
        r.key(Qt::Key_K);
        r.key(Qt::Key_L);
        QVERIFY(r.window->isFinished());
        QCOMPARE(r.window->summaryText(), QString("Done 0 · Snoozed 0 · Kept 2 · Deferred 2 · Trashed 0"));
    }

    void backUndoesPrevious()
    {
        Review r;
        r.key(Qt::Key_D);
        r.key(Qt::Key_Left);
        QCOMPARE(r.window->position(), 1);
        QCOMPARE(r.store().get(r.overdue).status, Status::Active);
        r.key(Qt::Key_K);
        r.key(Qt::Key_K);
        r.key(Qt::Key_K);
        r.key(Qt::Key_K);
        QCOMPARE(r.window->summaryText(), QString("Done 0 · Snoozed 0 · Kept 4 · Deferred 0 · Trashed 0"));
    }

    void ctrlZUndoesLast()
    {
        Review r;
        const qint64 before = r.store().get(r.staleA).modifiedAt;
        r.key(Qt::Key_D);
        r.key(Qt::Key_K);
        r.f->clock->advance(5000);
        r.key(Qt::Key_K); // staleA
        QCOMPARE(r.window->position(), 4);
        r.key(Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(r.window->position(), 3);
        QCOMPARE(r.window->currentId(), std::optional<ScratchId>(r.staleA));
        QCOMPARE(r.store().get(r.staleA).modifiedAt, before);
    }

    void escMidwayLeavesRest()
    {
        Review r;
        r.key(Qt::Key_D);
        r.key(Qt::Key_Escape);
        QVERIFY(!r.window->isVisible());
        QCOMPARE(r.store().get(r.dueToday).due, std::optional<QDate>(test::today()));
        QCOMPARE(r.store().get(r.staleA).status, Status::Active);
        QCOMPARE(r.store().reviewQueue(test::today(), 7).size(), 3);
    }

    void editInPlaceReturns()
    {
        Review r;
        r.key(Qt::Key_Return);
        QVERIFY(r.window->isEditing());
        QVERIFY(!r.window->editor()->isReadOnly());
        r.window->editor()->moveCursor(QTextCursor::End);
        QTest::keyClicks(r.window->editor(), " more");
        r.key(Qt::Key_Escape);
        QVERIFY(!r.window->isEditing());
        QVERIFY(r.window->isVisible());
        QCOMPARE(r.window->position(), 1);
        QCOMPARE(r.text(r.overdue), QString("pay rent more"));
    }

    // Final review I4 (and Review Focus 2): a note trashed or finished elsewhere is skipped,
    // never a dead end.
    void noteNoLongerActiveIsSkipped()
    {
        Review r;
        r.store().trash(r.overdue);
        r.key(Qt::Key_D);
        QCOMPARE(r.window->statusText(), QString("That note is no longer on the radar"));
        QCOMPARE(r.window->position(), 2);
        r.store().markDone(r.dueToday);
        r.key(Qt::Key_K);
        QCOMPARE(r.window->position(), 3);
    }

    // Final review I3: undoing Done, Trash or Snooze puts the stale clock back too.
    void undoRestoresStaleClock()
    {
        Review r;
        r.key(Qt::Key_K);
        r.key(Qt::Key_K); // now on staleA
        const qint64 before = r.store().get(r.staleA).modifiedAt;
        r.f->clock->advance(60'000);
        r.key(Qt::Key_D);
        r.key(Qt::Key_Left);
        QCOMPARE(r.store().get(r.staleA).modifiedAt, before);
        r.key(Qt::Key_Delete);
        r.key(Qt::Key_Left);
        QCOMPARE(r.store().get(r.staleA).modifiedAt, before);
        r.key(Qt::Key_S);
        r.key(Qt::Key_1);
        r.key(Qt::Key_Left);
        QCOMPARE(r.store().get(r.staleA).modifiedAt, before);
        QCOMPARE(r.store().get(r.staleA).due, std::optional<QDate>());
    }

    // Final review I7.
    void closingMidEditSaves()
    {
        Review r;
        r.key(Qt::Key_Return);
        r.window->editor()->moveCursor(QTextCursor::End);
        QTest::keyClicks(r.window->editor(), " now");
        r.window->close(); // inside the autosave debounce
        QCOMPARE(r.text(r.overdue), QString("pay rent now"));
    }

    // Final review I8: the summary page still takes Left, Ctrl+Z and Esc.
    void summaryKeysStillWork()
    {
        Review r;
        for (int i = 0; i < 4; ++i)
            r.key(Qt::Key_K);
        QVERIFY(r.window->isFinished());
        QTest::keyClick(r.window.get(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(r.window->position(), 4);
        r.key(Qt::Key_K);
        QTest::keyClick(r.window.get(), Qt::Key_Left);
        QCOMPARE(r.window->position(), 4);
        r.key(Qt::Key_K);
        QTest::keyClick(r.window.get(), Qt::Key_Escape);
        QVERIFY(!r.window->isVisible());
    }

    // Final review minor 1, regraded: a read-only note takes no paste.
    void readOnlyNoteTakesNoPaste()
    {
        Review r;
        QGuiApplication::clipboard()->setText("zzz");
        r.key(Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        r.key(Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(r.window->editor()->toPlainText(), QString("pay rent"));
        QCOMPARE(r.text(r.overdue), QString("pay rent"));
    }

    // Review Focus 2.
    void noteGoneElsewhereIsSkipped()
    {
        Review r;
        r.store().trash(r.overdue);
        r.f->clock->advance(31 * kDayMs);
        r.store().purgeTrash(30);
        r.key(Qt::Key_D);
        QCOMPARE(r.window->statusText(), QString("That note is gone"));
        QCOMPARE(r.window->position(), 2);
    }

    void openBoardFromSummary()
    {
        Review r;
        QSignalSpy spy(r.window.get(), &ReviewWindow::openBoardRequested);
        for (int i = 0; i < 4; ++i)
            r.key(Qt::Key_K);
        QVERIFY(r.window->isFinished());
        r.window->requestOpenBoard();
        QCOMPARE(spy.size(), 1);
    }
};

NOITE_TEST(ReviewWindowTest)
#include "tst_review_window.moc"
