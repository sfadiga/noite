#include "helpers.h"
#include "registry.h"

#include "capture/CaptureController.h"
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

    // D6: clicking away saves and hides.
    void deactivationDismisses()
    {
        Window w;
        w.window.summon();
        QTest::keyClicks(w.edit, "typed then clicked away");
        QEvent deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(&w.window, &deactivate);
        QVERIFY(!w.window.isVisible());
        QCOMPARE(w.count(Tab::All), 1);
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
