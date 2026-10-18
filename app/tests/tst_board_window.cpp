#include "helpers.h"
#include "registry.h"

#include "board/BoardModel.h"
#include "board/BoardWindow.h"
#include "editor/AttachmentStore.h"

#include <noite/store.h>

#include <QLineEdit>
#include <QListView>
#include "board/TabStrip.h"
#include <QTest>

using namespace noite;

namespace {

struct Board {
    std::unique_ptr<test::Fixture> f = test::fixture();
    StoreAttachments attachments{*f->store};
    std::unique_ptr<BoardWindow> window;

    explicit Board(bool seed = true)
    {
        if (seed) {
            add("buy milk");
            add("call bob");
            add("plan trip");
            f->store->markDone(add("old task"));
        }
        window = make();
    }
    std::unique_ptr<BoardWindow> make()
    {
        return std::make_unique<BoardWindow>(*f->store, attachments, [] { return test::today(); });
    }
    ScratchId add(const QString& text) { return f->store->createScratch(test::html({text})).id; }
    void show()
    {
        window->show();
        window->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(window.get()));
    }
};

} // namespace

class BoardWindowTest : public QObject {
    Q_OBJECT

private slots:
    void showsTodayByDefault()
    {
        Board b;
        QCOMPARE(b.window->model().rowCount(), 3);
        QCOMPARE(b.window->tabStrip()->currentIndex(), 0);
        QCOMPARE(b.window->tabStrip()->count(), 4);
    }

    void tabsAreEqualCapsButtonsWithCounts()
    {
        Board b;
        b.show();
        TabStrip* tabs = b.window->tabStrip();
        QCOMPARE(QStringList({tabs->label(0), tabs->label(1), tabs->label(2), tabs->label(3)}),
                 QStringList({"TODAY", "ALL", "COMPLETED", "TRASH"}));
        QCOMPARE(QStringList({tabs->countText(0), tabs->countText(1), tabs->countText(2), tabs->countText(3)}),
                 QStringList({"3", "3", "1", "0"}));
        for (int i = 1; i < 4; ++i)
            QCOMPARE(tabs->button(i)->size(), tabs->button(0)->size());
        QVERIFY(tabs->button(0)->font().pointSizeF() > b.window->font().pointSizeF());
    }

    void countsFollowSearchAndActions()
    {
        Board b;
        b.show();
        TabStrip* tabs = b.window->tabStrip();
        b.window->searchField()->setText("mil");
        QTRY_COMPARE(tabs->countText(0), QString("1"));
        QCOMPARE(tabs->countText(2), QString("0"));
        b.window->markDoneOrReopen(); // "buy milk", the only match
        QCOMPARE(tabs->countText(0), QString("0"));
        QCOMPARE(tabs->countText(2), QString("1"));
    }

    void clickingATabSwitches()
    {
        Board b;
        b.show();
        QTest::mouseClick(b.window->tabStrip()->button(3), Qt::LeftButton);
        QCOMPARE(b.window->model().tab(), Tab::Trash);
        QCOMPARE(b.window->tabStrip()->currentIndex(), 3);
    }

    void searchMatchesTabs()
    {
        Board b;
        b.show();
        QCOMPARE(b.window->searchField()->height(), b.window->tabStrip()->button(0)->height());
        QCOMPARE(b.window->searchField()->font().pointSizeF(), b.window->tabStrip()->button(0)->font().pointSizeF());
    }

    void tabSwitchReloads()
    {
        Board b;
        b.window->tabStrip()->setCurrentIndex(2);
        QCOMPARE(b.window->model().tab(), Tab::Completed);
        QCOMPARE(b.window->model().rowCount(), 1);
    }

    void searchIsDebounced()
    {
        Board b;
        b.show();
        QTest::keyClicks(b.window->searchField(), "mil");
        QVERIFY(b.window->model().search().isEmpty());
        QTRY_COMPARE(b.window->model().search(), QString("mil"));
        QCOMPARE(b.window->model().rowCount(), 1);
    }

    void slashFocusesSearch()
    {
        Board b;
        b.show();
        QVERIFY(b.window->grid()->hasFocus());
        QTest::keyClick(b.window->grid(), '/');
        QVERIFY(b.window->searchField()->hasFocus());
        QVERIFY(b.window->searchField()->text().isEmpty()); // the slash isn't typed
    }

    void escClearsThenCloses()
    {
        Board b;
        b.show();
        b.window->searchField()->setFocus();
        QTest::keyClicks(b.window->searchField(), "x");
        QTest::keyClick(b.window->searchField(), Qt::Key_Escape);
        QVERIFY(b.window->searchField()->text().isEmpty());
        QVERIFY(b.window->model().search().isEmpty());
        QVERIFY(b.window->isVisible());
        QTest::keyClick(b.window->searchField(), Qt::Key_Escape);
        QVERIFY(!b.window->isVisible());
    }

    void escOnGridCloses()
    {
        Board b;
        b.show();
        QTest::keyClick(b.window->grid(), Qt::Key_Escape);
        QVERIFY(!b.window->isVisible());
    }

    void viewStateRoundTrip()
    {
        Board b;
        b.window->setTab(Tab::Trash);
        b.window->searchField()->setText("a");
        const BoardWindow::ViewState state = b.window->viewState();
        QCOMPARE(state.tab, Tab::Trash);
        QCOMPARE(state.search, QString("a"));

        auto other = b.make();
        other->restoreViewState(state);
        QCOMPARE(other->tabStrip()->currentIndex(), 3);
        QCOMPARE(other->searchField()->text(), QString("a"));
        QCOMPARE(other->model().tab(), Tab::Trash);
        QCOMPARE(other->model().search(), QString("a"));
    }

    void firstCardSelectedOnShow()
    {
        Board b;
        b.show();
        QCOMPARE(b.window->grid()->currentIndex().row(), 0);
        QVERIFY(b.window->grid()->selectionModel()->isSelected(b.window->grid()->currentIndex()));
    }

    // Review Focus 2.
    void emptyStateMessage()
    {
        Board empty(false);
        empty.show();
        QCOMPARE(empty.window->emptyText(), QString("No notes"));

        Board b;
        b.show();
        QVERIFY(b.window->emptyText().isEmpty());
        b.window->searchField()->setText("zzz");
        QTRY_COMPARE(b.window->emptyText(), QString("Nothing matches"));
    }
};

NOITE_TEST(BoardWindowTest)
#include "tst_board_window.moc"
