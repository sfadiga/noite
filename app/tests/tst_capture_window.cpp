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

    // Dragging the frame moves the window, so text behind it can be read.
    void dragOnFrameMovesWindow()
    {
        Window w;
        w.window.summon();
        const QPoint start = w.window.pos();
        test::drag(&w.window, QPoint(4, 4), QPoint(50, 30));
        QCOMPARE(w.window.pos(), start + QPoint(50, 30));
        QVERIFY(w.window.isVisible());

        test::drag(w.edit->viewport(), QPoint(20, 20), QPoint(40, 40)); // selects text instead
        QCOMPARE(w.window.pos(), start + QPoint(50, 30));
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
