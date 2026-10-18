#include "helpers.h"
#include "registry.h"

#include "board/BoardModel.h"
#include "board/BoardWindow.h"
#include "board/NoteOverlay.h"
#include "board/UndoLog.h"
#include "editor/AttachmentStore.h"

#include <noite/store.h>

#include <QListView>
#include <QMimeData>
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
    // Bundles B and C as "bravo" (alpha, [bravo], on the board).
    BundleId bundleBC()
    {
        const BundleId bundle = store().bundleTogether(b, c);
        window->reload();
        return bundle;
    }
    void selectStack(const BundleId& bundle)
    {
        grid()->setCurrentIndex(window->model().index(window->model().rowOfStack(bundle)));
    }
    bool stackSelected() { return window->model().isStack(grid()->currentIndex().row()); }
    std::optional<BundleId> bundleOf(const ScratchId& id) { return store().get(id).bundleId; }
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

    // ---- M3b: stacks ----

    void clickOrEnterTogglesStack()
    {
        Board t;
        const BundleId bundle = t.bundleBC();
        t.selectStack(bundle);
        QTest::keyClick(t.grid(), Qt::Key_Return);
        QVERIFY(t.window->model().isExpanded(bundle));
        QVERIFY(t.stackSelected()); // the stack keeps the selection
        QTest::keyClick(t.grid(), Qt::Key_Return);
        QVERIFY(!t.window->model().isExpanded(bundle));
        const QRect rect = t.grid()->visualRect(t.window->model().index(t.window->model().rowOfStack(bundle)));
        QTest::mouseClick(t.grid()->viewport(), Qt::LeftButton, {}, rect.center());
        QVERIFY(t.window->model().isExpanded(bundle));
        QVERIFY(!t.window->overlay().isOpen()); // a stack opens in place, not in the editor
    }

    void escOnMemberCollapsesToStack()
    {
        Board t;
        const BundleId bundle = t.bundleBC();
        t.window->model().setExpanded(bundle, true);
        t.select(t.c);
        QTest::keyClick(t.grid(), Qt::Key_Escape);
        QVERIFY(!t.window->model().isExpanded(bundle));
        QVERIFY(t.stackSelected());
        QVERIFY(t.window->isVisible());
    }

    void viewStateKeepsFilterAndExpanded()
    {
        Board t;
        const BundleId bundle = t.bundleBC();
        t.store().setColor(t.b, Color::Red);
        BoardFilter red;
        red.colors = {Color::Red};
        t.window->model().setExpanded(bundle, true);
        t.window->model().setFilter(red);
        const BoardWindow::ViewState state = t.window->viewState();
        QCOMPARE(state.filter, red);
        QCOMPARE(state.expanded, QSet<BundleId>({bundle}));

        BoardWindow other(t.store(), t.attachments, [] { return test::today(); });
        other.restoreViewState(state);
        QCOMPARE(other.model().filter(), red);
        QVERIFY(other.model().isExpanded(bundle));
    }

    // C7.
    void stackIgnoresCardActions()
    {
        Board t;
        const BundleId bundle = t.bundleBC();
        t.selectStack(bundle);
        QTest::keyClick(t.grid(), Qt::Key_D);
        QTest::keyClick(t.grid(), Qt::Key_Delete);
        QTest::keyClick(t.grid(), Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(t.store().get(t.b).status, Status::Active);
        QCOMPARE(t.store().get(t.c).status, Status::Active);
        QCOMPARE(t.store().get(t.b).color, std::optional<Color>());
        QCOMPARE(t.window->statusText(), QString("Open the bundle to act on its notes"));
        QCOMPARE(t.window->undoLog().size(), 0);
    }

    void renameBundle()
    {
        Board t;
        const BundleId bundle = t.bundleBC();
        t.selectStack(bundle);
        t.window->renameSelectedBundle("Trip");
        QCOMPARE(t.store().listBundles().first().name, QString("Trip"));
        QCOMPARE(t.window->model().row(t.window->model().rowOfStack(bundle)).bundleName, QString("Trip"));
        QVERIFY(t.stackSelected());
        t.window->renameSelectedBundle("   ");
        QCOMPARE(t.store().listBundles().first().name, QString("Trip"));
        QVERIFY(!t.window->statusText().isEmpty());
    }

    void removeFromBundleMenuAction()
    {
        Board t;
        const BundleId bundle = t.bundleBC();
        t.window->model().setExpanded(bundle, true);
        t.select(t.c);
        t.window->removeSelectedFromBundle();
        QCOMPARE(t.bundleOf(t.c), std::optional<BundleId>());
        QCOMPARE(t.window->statusText(), QString("Removed from bravo"));
        QCOMPARE(t.current(), t.c);
        QTest::keyClick(t.grid(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(t.bundleOf(t.c), std::optional<BundleId>(bundle));
    }

    // ---- M3b: drag and drop ----

    void dropCardOnCardBundles()
    {
        Board t;
        t.window->dropCard(t.a, t.window->model().rowOf(t.c));
        QVERIFY(t.bundleOf(t.a).has_value());
        QCOMPARE(t.bundleOf(t.a), t.bundleOf(t.c));
        QCOMPARE(t.window->statusText(), QString("Bundled into charlie"));
        QCOMPARE(t.current(), t.a);
        QTest::keyClick(t.grid(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(t.bundleOf(t.a), std::optional<BundleId>());
        QCOMPARE(t.bundleOf(t.c), std::optional<BundleId>());
        QVERIFY(t.store().listBundles().isEmpty());
    }

    void dropCardOnStackMoves()
    {
        Board t;
        const BundleId bundle = t.bundleBC();
        t.window->dropCard(t.a, t.window->model().rowOfStack(bundle));
        QCOMPARE(t.bundleOf(t.a), std::optional<BundleId>(bundle));
        QCOMPARE(t.window->statusText(), QString("Moved to bravo"));
        QTest::keyClick(t.grid(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(t.bundleOf(t.a), std::optional<BundleId>());
    }

    void dropMemberOnEmptyLeaves()
    {
        Board t;
        const BundleId bundle = t.bundleBC();
        t.window->model().setExpanded(bundle, true);
        t.window->dropCard(t.c, -1);
        QCOMPARE(t.bundleOf(t.c), std::optional<BundleId>());
        QCOMPARE(t.window->statusText(), QString("Removed from bravo"));
        QTest::keyClick(t.grid(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(t.bundleOf(t.c), std::optional<BundleId>(bundle));
    }

    // Review Focus 2.
    void dropOnSelfOrOwnStackIsNoOp()
    {
        Board t;
        const BundleId bundle = t.bundleBC();
        t.window->dropCard(t.a, t.window->model().rowOf(t.a));
        t.window->dropCard(t.b, t.window->model().rowOfStack(bundle));
        t.window->dropCard(t.a, -1);
        QCOMPARE(t.bundleOf(t.a), std::optional<BundleId>());
        QCOMPARE(t.bundleOf(t.b), std::optional<BundleId>(bundle));
        QVERIFY(t.window->statusText().isEmpty());
        QCOMPARE(t.window->undoLog().size(), 0);
    }

    // Review Focus 5.
    void removingLastMembersDeletesBundleAndUndoRestores()
    {
        Board t;
        const BundleId bundle = t.bundleBC();
        t.window->model().setExpanded(bundle, true);
        t.window->dropCard(t.c, -1);
        t.window->dropCard(t.b, -1);
        QVERIFY(t.store().listBundles().isEmpty());
        QCOMPARE(t.current(), t.b);
        QTest::keyClick(t.grid(), Qt::Key_Z, Qt::ControlModifier);
        QTest::keyClick(t.grid(), Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(t.bundleOf(t.b).has_value());
        QCOMPARE(t.bundleOf(t.b), t.bundleOf(t.c));
        QCOMPARE(t.store().listBundles().first().name, QString("bravo"));
    }

    void dropGoesThroughModelMime()
    {
        Board t;
        BoardModel& model = t.window->model();
        QMimeData* mime = model.mimeData({model.index(model.rowOf(t.a))});
        QVERIFY(mime && mime->hasFormat(BoardModel::kMimeType));
        QVERIFY(!model.dropMimeData(mime, Qt::MoveAction, -1, 0, model.index(model.rowOf(t.c))));
        delete mime;
        QCOMPARE(t.bundleOf(t.a), t.bundleOf(t.c));
        QVERIFY(t.bundleOf(t.a).has_value());
    }

    void stacksAreNotDraggable()
    {
        Board t;
        const BundleId bundle = t.bundleBC();
        BoardModel& model = t.window->model();
        const Qt::ItemFlags stack = model.flags(model.index(model.rowOfStack(bundle)));
        QVERIFY(!(stack & Qt::ItemIsDragEnabled));
        QVERIFY(stack & Qt::ItemIsDropEnabled);
        QVERIFY(model.flags(model.index(model.rowOf(t.a))) & Qt::ItemIsDragEnabled);
    }
};

NOITE_TEST(BoardActionsTest)
#include "tst_board_actions.moc"
