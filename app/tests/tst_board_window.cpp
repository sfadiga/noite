#include "helpers.h"
#include "registry.h"

#include "board/BoardModel.h"
#include "board/BoardWindow.h"
#include "board/ChipBar.h"
#include "board/NoteOverlay.h"
#include "editor/NoteEdit.h"
#include "editor/AttachmentStore.h"

#include <noite/store.h>

#include <QDir>
#include <QLineEdit>
#include <QListView>
#include <QStandardPaths>
#include "board/TabStrip.h"
#include <QSignalSpy>
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

    // M7c: Ctrl+1..4 switch tabs (Today, All, Completed, Trash); the first card is selected.
    void ctrlDigitSwitchesTab()
    {
        Board b;
        b.show();
        QTest::keyClick(b.window->grid(), Qt::Key_3, Qt::ControlModifier);
        QCOMPARE(b.window->tabStrip()->currentIndex(), 2);
        QCOMPARE(b.window->model().tab(), Tab::Completed);
        QCOMPARE(b.window->grid()->currentIndex().row(), 0);
        QCOMPARE(b.window->model().summary(0).title, QString("old task"));
        for (const auto& [key, index] : {std::pair{Qt::Key_4, 3}, {Qt::Key_2, 1}, {Qt::Key_1, 0}}) {
            QTest::keyClick(b.window->grid(), key, Qt::ControlModifier);
            QCOMPARE(b.window->tabStrip()->currentIndex(), index);
            QCOMPARE(int(b.window->model().tab()), index);
        }
    }

    void ctrlDigitSwitchesTabFromSearch()
    {
        Board b;
        b.show();
        b.window->searchField()->setFocus();
        QTest::keyClicks(b.window->searchField(), "buy");
        QTest::keyClick(b.window->searchField(), Qt::Key_2, Qt::ControlModifier);
        QCOMPARE(b.window->model().tab(), Tab::All);
        QCOMPARE(b.window->searchField()->text(), QString("buy"));
    }

    // The note overlay's editor keeps its own keys.
    void ctrlDigitIgnoredWhileOverlayOpen()
    {
        Board b;
        b.show();
        b.window->openSelected();
        QVERIFY(b.window->overlay().isOpen());
        QTest::keyClick(b.window->overlay().editor(), Qt::Key_3, Qt::ControlModifier);
        QCOMPARE(b.window->tabStrip()->currentIndex(), 0);
    }

    // M8a X3/X4: Ctrl+E asks for <last folder>/<safe title>.html and writes the file there.
    void ctrlEExportsSelectedWithSafeName()
    {
        Board b(false);
        b.add("q1/q2: plan");
        b.window = b.make();
        b.f->store->setLastExportDir(b.f->tmp.path());
        b.show();
        QString asked;
        b.window->setExportPathPicker([&](const QString& suggested) { return asked = suggested; });
        QSignalSpy aboutToChange(b.window.get(), &BoardWindow::aboutToChange);
        QTest::keyClick(b.window->grid(), Qt::Key_E, Qt::ControlModifier);
        QCOMPARE(asked, QDir(b.f->tmp.path()).filePath("q1_q2_ plan.html"));
        QFile file(asked);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("q1/q2: plan"));
        QCOMPARE(aboutToChange.size(), 1); // a capture holding the note saves first
    }

    void exportStartsInDocumentsAndFollowsTheChosenFormat()
    {
        Board b;
        b.show();
        QString asked;
        QString answer = b.f->tmp.filePath("note.pdf");
        b.window->setExportPathPicker([&](const QString& suggested) {
            asked = suggested;
            return answer;
        });
        b.window->exportSelected();
        QVERIFY(asked.startsWith(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)));
        QFile pdf(answer);
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QVERIFY(pdf.readAll().startsWith("%PDF-"));
        QCOMPARE(b.f->store->lastExportDir(), std::optional<QString>(b.f->tmp.path()));

        answer = b.f->tmp.filePath("plain"); // no extension: HTML
        b.window->exportSelected();
        QVERIFY(QFile::exists(b.f->tmp.filePath("plain.html")));
    }

    void exportCancelledWritesNothing()
    {
        Board b;
        b.show();
        b.window->setExportPathPicker([](const QString&) { return QString(); });
        const QStringList before = QDir(b.f->tmp.path()).entryList(QDir::Files);
        b.window->exportSelected();
        QCOMPARE(QDir(b.f->tmp.path()).entryList(QDir::Files), before);
        QCOMPARE(b.f->store->lastExportDir(), std::optional<QString>());
    }

    // X14: the open note is saved first, so the export has what is on screen.
    void ctrlEInOverlayExportsEditedText()
    {
        Board b;
        b.show();
        b.window->openSelected();
        QVERIFY(b.window->overlay().isOpen());
        b.window->overlay().editor()->moveCursor(QTextCursor::End);
        QTest::keyClicks(b.window->overlay().editor(), " unsaved words");
        const QString path = b.f->tmp.filePath("open.html");
        b.window->setExportPathPicker([&](const QString&) { return path; });
        QTest::keyClick(b.window->overlay().editor(), Qt::Key_E, Qt::ControlModifier);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("unsaved words"));
        QVERIFY(b.window->overlay().isOpen());
    }

    void exportFailureIsReported()
    {
        Board b;
        b.show();
        QString reported;
        b.window->setExportPathPicker([&](const QString&) { return b.f->tmp.filePath("no-such-folder/x.html"); });
        b.window->setExportErrorReporter([&](const QString& message) { reported = message; });
        b.window->exportSelected();
        QVERIFY2(reported.contains("no-such-folder"), qPrintable(reported));
        QCOMPARE(b.f->store->lastExportDir(), std::optional<QString>());
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

    // Opening the board restores its view in one query round, not one per field.
    void restoreViewStateReloadsOnce()
    {
        Board b;
        QSignalSpy resets(&b.window->model(), &QAbstractItemModel::modelReset);
        BoardFilter red;
        red.colors = {Color::Red};
        b.window->restoreViewState({Tab::All, "milk", red, {}});
        QCOMPARE(resets.size(), 1);
        QCOMPARE(b.window->model().tab(), Tab::All);
        QCOMPARE(b.window->model().search(), QString("milk"));
        QCOMPARE(b.window->model().filter(), red);
    }

    // ---- M3b: chips ----

    void chipNarrowsCardsAndCounts()
    {
        Board b;
        const QList<ScratchSummary> today = b.f->store->list(Tab::Today, test::today());
        b.f->store->setColor(today[0].id, Color::Red);
        b.f->store->setColor(today[1].id, Color::Red);
        b.window->reload();
        b.show();
        QVERIFY(b.window->chipBar()->chip("color:red"));
        QTest::mouseClick(b.window->chipBar()->chip("color:red"), Qt::LeftButton);
        QCOMPARE(b.window->model().rowCount(), 2);
        QCOMPARE(b.window->tabStrip()->countText(0), QString("2"));
        QVERIFY(b.window->chipBar()->chip("color:red")->isChecked());
    }

    void escClearsSearchThenChipsThenCloses()
    {
        Board b;
        const QList<ScratchSummary> today = b.f->store->list(Tab::Today, test::today());
        b.f->store->setColor(today[0].id, Color::Red);
        b.window->reload();
        b.show();
        QTest::mouseClick(b.window->chipBar()->chip("color:red"), Qt::LeftButton);
        b.window->searchField()->setFocus();
        QTest::keyClicks(b.window->searchField(), "milk");
        QTest::keyClick(b.window->searchField(), Qt::Key_Escape);
        QVERIFY(b.window->searchField()->text().isEmpty());
        QVERIFY(!b.window->model().filter().isEmpty());
        QTest::keyClick(b.window->grid(), Qt::Key_Escape);
        QVERIFY(b.window->model().filter().isEmpty());
        QVERIFY(b.window->isVisible());
        QTest::keyClick(b.window->grid(), Qt::Key_Escape);
        QVERIFY(!b.window->isVisible());
    }

    // Review Focus 4.
    void activeChipWithNoMatchesStays()
    {
        Board b;
        const ScratchId tagged = b.add("pack #trip");
        b.window->reload();
        b.show();
        QTest::mouseClick(b.window->chipBar()->chip("tag:trip"), Qt::LeftButton);
        QCOMPARE(b.window->model().rowCount(), 1);
        b.f->store->updateContent(tagged, test::html({"pack"}));
        b.window->reload();
        QAbstractButton* chip = b.window->chipBar()->chip("tag:trip");
        QVERIFY(chip);
        QVERIFY(chip->isChecked());
        QCOMPARE(chip->text(), QString("#trip 0"));
        QCOMPARE(b.window->emptyText(), QString("Nothing matches"));
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
