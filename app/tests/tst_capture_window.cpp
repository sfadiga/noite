#include "helpers.h"
#include "registry.h"
#include "widget_helpers.h"

#include "capture/CaptureController.h"
#include "capture/NoteFooterBar.h"
#include "capture/TagField.h"
#include "capture/CaptureWindow.h"
#include "capture/LatencyProbe.h"
#include "editor/AttachmentStore.h"
#include "editor/NoteEdit.h"

#include <noite/store.h>

#include <QApplication>
#include <QFile>
#include <QMouseEvent>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace noite;

namespace {

struct Window {
    std::unique_ptr<test::Fixture> f = test::fixture();
    NoteEdit* edit = new NoteEdit; // owned by the window
    StoreAttachments attachments{*f->store};
    CaptureController controller{*f->store, *edit, [] { return QDate(2026, 10, 15); }};
    CaptureWindow window{controller, edit};

    Window() { edit->setAttachmentStore(&attachments); }
    qsizetype count(Tab tab) { return f->store->list(tab, QDate(2026, 10, 15)).size(); }
};

} // namespace

class CaptureWindowTest : public QObject {
    Q_OBJECT

private slots:
    void escDismissesAndHides()
    {
        Window w;
        w.window.summon();
        QVERIFY(w.window.isVisible());
        QTest::keyClicks(w.edit, "note");
        QTest::keyClick(w.edit, Qt::Key_Escape);
        QVERIFY(!w.window.isVisible());
        QCOMPARE(w.count(Tab::All), 1);
    }

    void summonNoteOpensExisting()
    {
        Window w;
        const Scratch made = w.f->store->createScratch("hello");
        w.window.summonNote(made.id);
        QVERIFY(w.window.isVisible());
        QCOMPARE(w.edit->toPlainText(), QString("hello"));
        QTest::keyClicks(w.edit, " world");
        QTest::keyClick(w.edit, Qt::Key_Escape);
        QVERIFY(!w.window.isVisible());
        QVERIFY(w.f->store->get(made.id).html.contains("hello world"));
    }

    void ctrlEnterMarksDone()
    {
        Window w;
        w.window.summon();
        QTest::keyClicks(w.edit, "done thing");
        QTest::keyClick(w.edit, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(!w.window.isVisible());
        QCOMPARE(w.count(Tab::Completed), 1);
        QCOMPARE(w.edit->toPlainText(), QString("done thing")); // no newline inserted
    }

    // D6 (1.0.1): only Esc closes it; clicking away leaves it open, text and all.
    void deactivationKeepsItOpen()
    {
        Window w;
        QSignalSpy finished(&w.controller, &CaptureController::finished);
        w.window.summon();
        QTest::keyClicks(w.edit, "typed then clicked away");
        QEvent deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(&w.window, &deactivate);
        QVERIFY(w.window.isVisible());
        QCOMPARE(finished.count(), 0);
        QCOMPARE(w.edit->toPlainText(), QString("typed then clicked away"));
    }

    void colourKeyCycles()
    {
        Window w;
        w.window.summon();
        QTest::keyClick(w.edit, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(w.controller.color() == Color::Red);
        QVERIFY(w.window.footerText().contains("red", Qt::CaseInsensitive));
    }

    void dueShowsInFooter()
    {
        Window w;
        w.window.summon();
        QTest::keyClicks(w.edit, "pay @tomorrow ");
        QVERIFY(w.window.footerText().contains(QLocale().toString(QDate(2026, 10, 16), QLocale::ShortFormat)));
    }

    void summonWhileVisibleKeepsText()
    {
        Window w;
        w.window.summon();
        QTest::keyClicks(w.edit, "abc");
        w.window.summon();
        QCOMPARE(w.edit->toPlainText(), QString("abc"));
    }

    void windowFeedsTheLatencyProbe()
    {
        QTemporaryDir tmp;
        LatencyProbe probe(tmp.filePath("trials.csv"));
        Window w;
        w.window.setLatencyProbe(&probe);
        probe.pressed();
        w.window.summon();
        QTRY_VERIFY(!probe.pending());
        QVERIFY(probe.lastReadyMs() >= 0);
        QTest::keyClicks(w.edit, "hello");
        QTest::keyClick(w.edit, Qt::Key_Escape);
        QFile csv(tmp.filePath("trials.csv"));
        QVERIFY(csv.open(QIODevice::ReadOnly | QIODevice::Text));
        QVERIFY(QString::fromUtf8(csv.readAll()).contains(",\"hello\""));
    }

    void summonReportsPhasesInOrder()
    {
        LatencyProbe probe{QString()};
        Window w;
        w.window.setLatencyProbe(&probe);
        probe.pressed();
        w.window.summon();
        QTRY_VERIFY(!probe.pending());
        const LatencyProbe::Phases& p = probe.lastPhases();
        QVERIFY(p.shownMs >= 0);
        QVERIFY(p.shownMs <= p.activatedMs);
        QVERIFY(p.activatedMs <= p.paintedMs);
    }

    // Dragging the frame moves the window, so text behind it can be read.
    void dragOnFrameMovesWindow()
    {
        Window w;
        w.window.summon();
        const QPoint start = w.window.pos();
        test::drag(&w.window, QPoint(9, 9), QPoint(50, 30)); // inside the margin, off the resize edge
        QCOMPARE(w.window.pos(), start + QPoint(50, 30));
        QVERIFY(w.window.isVisible());

        test::drag(w.edit->viewport(), QPoint(20, 20), QPoint(40, 40)); // selects text instead
        QCOMPARE(w.window.pos(), start + QPoint(50, 30));
    }

    // Any edge or corner resizes it.
    void dragOnCornerResizes()
    {
        Window w;
        w.window.summon();
        const QRect start = w.window.geometry();
        test::drag(&w.window, QPoint(start.width() - 2, start.height() - 2), QPoint(40, 30));
        QCOMPARE(w.window.geometry(), QRect(start.topLeft(), start.size() + QSize(40, 30)));
    }

    void dragOnLeftEdgeResizesFromThatSide()
    {
        Window w;
        w.window.summon();
        const QRect start = w.window.geometry();
        test::drag(&w.window, QPoint(2, start.height() / 2), QPoint(-40, 25));
        QCOMPARE(w.window.geometry(), start.adjusted(-40, 0, 0, 0));
    }

    void resizeStopsAtTheMinimum()
    {
        Window w;
        w.window.summon();
        const QRect start = w.window.geometry();
        test::drag(&w.window, QPoint(2, 2), QPoint(5000, 5000));
        QCOMPARE(w.window.size(), w.window.minimumSize());
        QCOMPARE(w.window.geometry().bottomRight(), start.bottomRight());
    }

    void edgeShowsTheResizeCursor()
    {
        Window w;
        w.window.summon();
        const QSize size = w.window.size();
        auto hover = [&](QPoint at) {
            QMouseEvent move(QEvent::MouseMove, at, w.window.mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(&w.window, &move);
            return w.window.cursor().shape();
        };
        QCOMPARE(hover(QPoint(size.width() - 2, size.height() - 2)), Qt::SizeFDiagCursor);
        QCOMPARE(hover(QPoint(size.width() - 2, 2)), Qt::SizeBDiagCursor);
        QCOMPARE(hover(QPoint(2, size.height() / 2)), Qt::SizeHorCursor);
        QCOMPARE(hover(QPoint(size.width() / 2, size.height() - 2)), Qt::SizeVerCursor);
        QCOMPARE(hover(QPoint(9, 9)), Qt::SizeAllCursor); // the margin still moves it
    }

    // Kept for the session: the next capture opens at the size the last one had.
    void sizeKeptForTheNextCapture()
    {
        Window w;
        w.window.summon();
        const QSize start = w.window.size();
        test::drag(&w.window, QPoint(start.width() - 2, start.height() - 2), QPoint(-60, -40));
        QTest::keyClick(w.edit, Qt::Key_Escape);
        w.window.summon();
        QCOMPARE(w.window.size(), start - QSize(60, 40));
    }

    void sizeFitsTheScreenItOpensOn()
    {
        Window w;
        w.window.resize(5000, 5000); // left from a larger monitor
        w.window.summon();
        const QRect area = w.window.screen()->availableGeometry();
        QVERIFY(w.window.width() <= area.width());
        QVERIFY(w.window.height() <= area.height());
    }

    void ctrlTFocusesTagFieldAndSaves()
    {
        Window w;
        w.window.summon();
        QTest::keyClicks(w.edit, "note");
        QTest::keyClick(w.edit, Qt::Key_T, Qt::ControlModifier);
        TagField* tags = w.window.footerBar()->tagField();
        QCOMPARE(w.window.focusWidget(), tags); // offscreen, the Tool window is never active
        QTest::keyClicks(tags, "#Trip");
        QTest::keyClick(tags, Qt::Key_Escape);
        QVERIFY(!w.window.isVisible());
        const QList<ScratchSummary> all = w.f->store->list(Tab::All, QDate(2026, 10, 15));
        QCOMPARE(all.size(), 1);
        QCOMPARE(w.f->store->get(all[0].id).manualTags, QStringList({"trip"}));
    }

    void invalidTagShowsErrorAndSavesNone()
    {
        Window w;
        w.window.summon();
        QTest::keyClicks(w.edit, "x");
        QTest::keyClick(w.edit, Qt::Key_T, Qt::ControlModifier);
        QTest::keyClicks(w.window.footerBar()->tagField(), "#1abc");
        QCOMPARE(w.window.footerBar()->errorText(), QString("Invalid tag: 1abc"));
        QTest::keyClick(w.window.footerBar()->tagField(), Qt::Key_Escape);
        const QList<ScratchSummary> all = w.f->store->list(Tab::All, QDate(2026, 10, 15));
        QCOMPARE(all.size(), 1);
        QVERIFY(w.f->store->get(all[0].id).manualTags.isEmpty());
    }

    void summonAfterDismissStartsBlank()
    {
        Window w;
        w.window.summon();
        QTest::keyClicks(w.edit, "first");
        QTest::keyClick(w.edit, Qt::Key_Escape);
        w.window.summon();
        QVERIFY(w.edit->toPlainText().isEmpty());
    }
};

NOITE_TEST(CaptureWindowTest)
#include "tst_capture_window.moc"
