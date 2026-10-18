#include "helpers.h"
#include "registry.h"
#include "widget_helpers.h"

#include "board/BoardModel.h"
#include "board/BoardWindow.h"
#include "board/NoteOverlay.h"
#include "capture/CaptureController.h"
#include "capture/NoteFooterBar.h"
#include "capture/TagField.h"
#include "editor/AttachmentStore.h"
#include "editor/NoteEdit.h"

#include <noite/content.h>
#include <noite/store.h>

#include <QListView>
#include <QTest>

using namespace noite;

namespace {

// Note A "alpha", due today and green, on a shown board.
struct Board {
    std::unique_ptr<test::Fixture> f = test::fixture();
    StoreAttachments attachments{*f->store};
    ScratchId a;
    std::unique_ptr<BoardWindow> window;

    Board()
    {
        a = f->store->createScratch(test::html({"alpha"})).id;
        f->store->setDue(a, test::today());
        f->store->setColor(a, Color::Green);
        window = std::make_unique<BoardWindow>(*f->store, attachments, [] { return test::today(); });
        window->show();
        window->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(window.get()));
    }
    Store& store() { return *f->store; }
    NoteOverlay& overlay() { return window->overlay(); }
    QString text(const ScratchId& id) { return content::plainText(store().get(id).html); }
    void openA()
    {
        window->grid()->setCurrentIndex(window->model().index(window->model().rowOf(a)));
        QTest::keyClick(window->grid(), Qt::Key_Return);
        QVERIFY(overlay().isOpen());
    }
};

} // namespace

class NoteOverlayTest : public QObject {
    Q_OBJECT

private slots:
    void enterOpensSelectedNote()
    {
        Board b;
        b.openA();
        QVERIFY(b.overlay().isVisible());
        QCOMPARE(b.overlay().editor()->toPlainText(), QString("alpha"));
        QVERIFY(b.overlay().footerText().contains("green"));
        QVERIFY(b.overlay().editor()->hasFocus());
    }

    void editThenEscSaves()
    {
        Board b;
        b.openA();
        QTest::keyClicks(b.overlay().editor(), " beta");
        QTest::keyClick(b.overlay().editor(), Qt::Key_Escape); // inside the autosave debounce
        QVERIFY(!b.overlay().isOpen());
        QVERIFY(!b.overlay().isVisible());
        QVERIFY(b.window->isVisible()); // Esc closed the note, not the board
        QCOMPARE(b.text(b.a), QString("alpha beta"));
    }

    void ctrlEnterMarksDone()
    {
        Board b;
        b.openA();
        QTest::keyClick(b.overlay().editor(), Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(!b.overlay().isOpen());
        QCOMPARE(b.store().get(b.a).status, Status::Done);
        QCOMPARE(b.window->model().rowOf(b.a), -1);
    }

    void emptiedNoteIsDeleted()
    {
        Board b;
        b.openA();
        b.overlay().editor()->selectAll();
        QTest::keyClick(b.overlay().editor(), Qt::Key_Delete);
        QTest::keyClick(b.overlay().editor(), Qt::Key_Escape);
        QCOMPARE(b.store().list(Tab::All, test::today()).size(), 0);
        QCOMPARE(b.window->model().rowCount(), 0);
    }

    void nOpensBlankAndCreates()
    {
        Board b;
        QTest::keyClick(b.window->grid(), Qt::Key_N);
        QVERIFY(b.overlay().isOpen());
        QVERIFY(b.overlay().editor()->toPlainText().isEmpty());
        QTest::keyClicks(b.overlay().editor(), "new one");
        QTest::keyClick(b.overlay().editor(), Qt::Key_Escape);
        QCOMPARE(b.window->model().rowCount(), 2);
        const int row = b.window->grid()->currentIndex().row();
        QCOMPARE(b.window->model().summary(row).title, QString("new one"));
    }

    void clickOpensCard()
    {
        Board b;
        const QRect rect = b.window->grid()->visualRect(b.window->model().index(0));
        QTest::mouseClick(b.window->grid()->viewport(), Qt::LeftButton, {}, rect.center());
        QVERIFY(b.overlay().isOpen());
        QCOMPARE(b.overlay().editor()->toPlainText(), QString("alpha"));
    }

    void dragOnPanelFrameMovesItAndRecentresOnReopen()
    {
        Board b;
        b.openA();
        QWidget* panel = b.overlay().panel();
        const QPoint centred = panel->pos();
        test::drag(panel, QPoint(4, 4), QPoint(40, 20));
        QCOMPARE(panel->pos(), centred + QPoint(40, 20));
        QVERIFY(b.overlay().isOpen()); // a drag isn't a backdrop click

        test::drag(panel, QPoint(4, 4), QPoint(-5000, -5000)); // stays on the board
        QVERIFY(b.overlay().rect().contains(panel->geometry()));

        QTest::keyClick(b.overlay().editor(), Qt::Key_Escape);
        b.openA();
        QCOMPARE(panel->pos(), centred);
    }

    void backdropClickCloses()
    {
        Board b;
        b.openA();
        QTest::mouseClick(&b.overlay(), Qt::LeftButton, {}, QPoint(5, 5));
        QVERIFY(!b.overlay().isOpen());
    }

    // Review Focus 4.
    void closingBoardWithOverlayOpenSaves()
    {
        Board b;
        b.openA();
        QTest::keyClicks(b.overlay().editor(), " gamma");
        b.window->close();
        QCOMPARE(b.text(b.a), QString("alpha gamma"));
    }

    // Final review: the store rejects edits to trashed notes, so typing there would be lost.
    void trashedNoteOpensReadOnly()
    {
        Board b;
        b.store().trash(b.a);
        b.window->setTab(Tab::Trash);
        b.openA();
        QVERIFY(b.overlay().editor()->isReadOnly());
        QTest::keyClicks(b.overlay().editor(), "x");
        QTest::keyClick(b.overlay().editor(), Qt::Key_Escape);
        QCOMPARE(b.text(b.a), QString("alpha"));
        QVERIFY(b.window->statusText().isEmpty()); // no failed save

        b.store().restore(b.a);
        b.window->setTab(Tab::Today);
        b.openA();
        QVERIFY(!b.overlay().editor()->isReadOnly());
    }

    void overlayEditsTags()
    {
        Board b;
        b.openA();
        TagField* tags = b.overlay().footerBar()->tagField();
        QVERIFY(tags->text().isEmpty());
        QTest::keyClick(b.overlay().editor(), Qt::Key_T, Qt::ControlModifier);
        QVERIFY(tags->hasFocus());
        QTest::keyClicks(tags, "home");
        QTest::keyClick(tags, Qt::Key_Escape);
        QVERIFY(!b.overlay().isOpen());
        QVERIFY(b.window->model().summary(b.window->model().rowOf(b.a)).tags.contains("home"));
    }

    void boardReloadsAfterClose()
    {
        Board b;
        b.openA();
        QTest::keyClick(b.overlay().editor(), Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(b.overlay().footerText().contains("blue"));
        QTest::keyClick(b.overlay().editor(), Qt::Key_Escape);
        const int row = b.window->model().rowOf(b.a);
        QCOMPARE(b.window->model().summary(row).color, std::optional<Color>(Color::Blue));
        QCOMPARE(b.window->grid()->currentIndex().row(), row);
    }
};

NOITE_TEST(NoteOverlayTest)
#include "tst_note_overlay.moc"
