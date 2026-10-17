#include "helpers.h"
#include "registry.h"

#include "board/BoardModel.h"
#include "board/BoardWindow.h"
#include "board/UndoLog.h"
#include "editor/AttachmentStore.h"

#include <noite/store.h>

#include <QListView>
#include <QTest>

using namespace noite;

namespace {

// Notes A, B, C in Today, oldest first (so the board shows them in that order).
struct Board {
    std::unique_ptr<test::Fixture> f = test::fixture();
    StoreAttachments attachments{*f->store};
    ScratchId a, b, c;
    std::unique_ptr<BoardWindow> window;

    explicit Board(bool seed = true)
    {
        if (seed) {
            a = add("alpha");
            b = add("bravo");
            c = add("charlie");
        }
        window = std::make_unique<BoardWindow>(*f->store, attachments, [] { return test::today(); });
        window->show();
        window->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(window.get()));
    }
    ScratchId add(const QString& text)
    {
        f->clock->advance(60'000);
        return f->store->createScratch(test::html({text})).id;
    }
    Store& store() { return *f->store; }
    QListView* grid() { return window->grid(); }
    void select(const ScratchId& id) { grid()->setCurrentIndex(window->model().index(window->model().rowOf(id))); }
    ScratchId current() { return window->model().summary(grid()->currentIndex().row()).id; }
    qsizetype count(Tab tab) { return store().list(tab, test::today()).size(); }
};

} // namespace

class BoardActionsTest : public QObject {
    Q_OBJECT

private slots:
    void doneRemovesCardAndSelectsNext()
    {
        Board w;
        w.select(w.a);
        QTest::keyClick(w.grid(), Qt::Key_D);
        QCOMPARE(w.store().get(w.a).status, Status::Done);
        QCOMPARE(w.window->model().rowCount(), 2);
        QCOMPARE(w.grid()->currentIndex().row(), 0);
        QCOMPARE(w.current(), w.b);
        QVERIFY(w.window->statusText().contains("Marked done"));
    }

    void doneOnLastCardSelectsPrevious()
    {
        Board w;
        w.select(w.c);
        QTest::keyClick(w.grid(), Qt::Key_D);
        QCOMPARE(w.current(), w.b);
    }

    void deleteTrashesAndRestoresInTrashTab()
    {
        Board w;
        w.select(w.a);
        QTest::keyClick(w.grid(), Qt::Key_Delete);
        QCOMPARE(w.store().get(w.a).status, Status::Trashed);
        w.window->setTab(Tab::Trash);
        w.select(w.a);
        QTest::keyClick(w.grid(), Qt::Key_Delete);
        QCOMPARE(w.store().get(w.a).status, Status::Active);
        QCOMPARE(w.window->model().rowCount(), 0);
    }

    void reopenInCompletedTab()
    {
        Board w;
        w.store().markDone(w.a);
        w.window->setTab(Tab::Completed);
        w.select(w.a);
        QTest::keyClick(w.grid(), Qt::Key_D);
        QCOMPARE(w.store().get(w.a).status, Status::Active);
    }

    void colourKeyCycles()
    {
        Board w;
        w.select(w.b);
        QTest::keyClick(w.grid(), Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(w.store().get(w.b).color, std::optional<Color>(Color::Red));
        QCOMPARE(w.current(), w.b);
    }

    void setDueAndClear()
    {
        Board w;
        w.window->setTab(Tab::All); // a future date takes the card out of Today
        w.select(w.b);
        w.window->setDue(QDate(2026, 10, 16));
        QCOMPARE(w.store().get(w.b).due, std::optional<QDate>(QDate(2026, 10, 16)));
        QCOMPARE(w.current(), w.b);
        w.window->setDue(std::nullopt);
        QCOMPARE(w.store().get(w.b).due, std::nullopt);
    }

    void undoReversesEachAction()
    {
        Board w;
        w.select(w.a);
        QTest::keyClick(w.grid(), Qt::Key_D);
        QTest::keyClick(w.grid(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(w.store().get(w.a).status, Status::Active);
        QVERIFY(w.window->statusText().startsWith("Undone"));

        w.select(w.a);
        QTest::keyClick(w.grid(), Qt::Key_Delete);
        w.window->undo();
        QCOMPARE(w.store().get(w.a).status, Status::Active);

        w.select(w.a);
        w.window->setColor(Color::Red);
        w.window->undo();
        QCOMPARE(w.store().get(w.a).color, std::nullopt);

        w.select(w.a);
        w.window->setDue(QDate(2026, 10, 20));
        w.window->undo();
        QCOMPARE(w.store().get(w.a).due, std::nullopt);

        w.select(w.a);
        w.window->cloneSelected();
        QCOMPARE(w.count(Tab::Today), 4);
        const ScratchId clone = w.current();
        QVERIFY(clone != w.a);
        w.window->undo();
        QCOMPARE(w.store().get(clone).status, Status::Trashed);
        QCOMPARE(w.count(Tab::Today), 3);

        QCOMPARE(w.window->undoLog().size(), 0);
        w.window->undo();
        QCOMPARE(w.window->statusText(), QString("Nothing to undo"));
    }

    // Review Focus 1.
    void staleCardActionReloads()
    {
        Board w;
        w.select(w.a);
        w.store().trash(w.a); // behind the board's back
        QTest::keyClick(w.grid(), Qt::Key_Delete);
        QVERIFY(!w.window->statusText().isEmpty());
        QCOMPARE(w.window->model().rowOf(w.a), -1);
        QCOMPARE(w.window->undoLog().size(), 0);
    }

    // Review Focus 2.
    void actionsOnEmptyGridAreNoOps()
    {
        Board w(false);
        for (int key : {int(Qt::Key_D), int(Qt::Key_Delete), int(Qt::Key_Return)})
            QTest::keyClick(w.grid(), Qt::Key(key));
        QTest::keyClick(w.grid(), Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::keyClick(w.grid(), Qt::Key_Z, Qt::ControlModifier);
        w.window->cloneSelected();
        w.window->setDue(QDate(2026, 10, 16));
        QCOMPARE(w.window->undoLog().size(), 0);
        QCOMPARE(w.count(Tab::All), 0);
    }

    void undoLogRunsNewestFirst()
    {
        UndoLog log;
        QStringList ran;
        log.push("first", [&] { ran << "first"; });
        log.push("second", [&] { ran << "second"; });
        QCOMPARE(log.size(), 2);
        QCOMPARE(log.undo(), std::optional<QString>("second"));
        QCOMPARE(log.undo(), std::optional<QString>("first"));
        QCOMPARE(log.undo(), std::nullopt);
        QCOMPARE(ran, QStringList({"second", "first"}));
        log.push("x", [] {});
        log.clear();
        QCOMPARE(log.size(), 0);
    }
};

NOITE_TEST(BoardActionsTest)
#include "tst_board_actions.moc"
