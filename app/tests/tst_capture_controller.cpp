#include "helpers.h"
#include "registry.h"

#include "capture/CaptureController.h"
#include "editor/AttachmentStore.h"
#include "editor/NoteEdit.h"

#include <noite/store.h>

#include <QBuffer>
#include <QMimeData>
#include <QSignalSpy>
#include <QTest>

using namespace noite;
using noite::test::Fixture;

namespace {

struct Capture {
    std::unique_ptr<Fixture> f = noite::test::fixture();
    NoteEdit edit;
    StoreAttachments attachments{*f->store};
    CaptureController controller{*f->store, edit, [] { return QDate(2026, 10, 15); }};

    Capture() { edit.setAttachmentStore(&attachments); }
    Store& store() { return *f->store; }
    qsizetype count(Tab tab) { return store().list(tab, QDate(2026, 10, 15)).size(); }
};

} // namespace

class CaptureControllerTest : public QObject {
    Q_OBJECT

private slots:
    void noScratchUntilFirstEdit()
    {
        Capture c;
        c.controller.begin();
        QTest::qWait(CaptureController::kAutosaveMs + 150);
        QVERIFY(!c.controller.currentId());
        QCOMPARE(c.count(Tab::All), 0);
    }

    void typingSavesAfterDebounce()
    {
        Capture c;
        c.controller.begin();
        QTest::keyClicks(&c.edit, "hello");
        QCOMPARE(c.count(Tab::All), 0); // nothing written while typing
        QTRY_VERIFY_WITH_TIMEOUT(c.controller.currentId().has_value(), 2000);
        QCOMPARE(c.store().get(*c.controller.currentId()).title, QString("hello"));
    }

    // Review Focus 1: closing inside the debounce window must not lose the latest text.
    void dismissFlushesPendingEdit()
    {
        Capture c;
        QSignalSpy finished(&c.controller, &CaptureController::finished);
        c.controller.begin();
        QTest::keyClicks(&c.edit, "first");
        QTRY_VERIFY_WITH_TIMEOUT(c.controller.currentId().has_value(), 2000);
        QTest::keyClicks(&c.edit, " second");
        c.controller.dismiss();
        QCOMPARE(c.store().get(*c.controller.currentId()).title, QString("first second"));
        QCOMPARE(finished.count(), 1);
    }

    void dismissDiscardsEmptied()
    {
        Capture c;
        c.controller.begin();
        QTest::keyClicks(&c.edit, "x");
        QTRY_VERIFY_WITH_TIMEOUT(c.controller.currentId().has_value(), 2000);
        c.edit.selectAll();
        QTest::keyClick(&c.edit, Qt::Key_Delete);
        c.controller.dismiss();
        QCOMPARE(c.count(Tab::All), 0);
        QVERIFY(!c.controller.currentId());
    }

    void markDoneAndClose()
    {
        Capture c;
        QSignalSpy finished(&c.controller, &CaptureController::finished);
        c.controller.begin();
        QTest::keyClicks(&c.edit, "call bob");
        c.controller.markDoneAndClose(); // still inside the debounce window
        QCOMPARE(c.count(Tab::All), 0);
        QCOMPARE(c.count(Tab::Completed), 1);
        QCOMPARE(finished.count(), 1);
    }

    void dueTokenBeforeFirstSaveIsApplied()
    {
        Capture c;
        QSignalSpy dueChanged(&c.controller, &CaptureController::dueChanged);
        c.controller.begin();
        QTest::keyClicks(&c.edit, "pay @tomorrow rent");
        QTRY_VERIFY_WITH_TIMEOUT(c.controller.currentId().has_value(), 2000);
        QVERIFY(c.store().get(*c.controller.currentId()).due == QDate(2026, 10, 16));
        QVERIFY(dueChanged.count() >= 1);
    }

    void colourCycles()
    {
        Capture c;
        c.controller.begin();
        c.controller.cycleColor(); // before the scratch exists: kept pending
        QTest::keyClicks(&c.edit, "x");
        c.controller.flush();
        const ScratchId id = *c.controller.currentId();
        QVERIFY(c.store().get(id).color == Color::Red);
        for (Color expected : {Color::Orange, Color::Yellow, Color::Green, Color::Blue, Color::Purple}) {
            c.controller.cycleColor();
            QVERIFY(c.store().get(id).color == expected);
        }
        c.controller.cycleColor();
        QVERIFY(!c.store().get(id).color);
    }

    void imageOnlyNoteIsKept()
    {
        Capture c;
        c.controller.begin();
        QImage img(4, 4, QImage::Format_RGB32);
        img.fill(Qt::red);
        QMimeData mime;
        mime.setImageData(img);
        c.edit.insertFromMimeData(&mime);
        c.controller.dismiss();
        QCOMPARE(c.count(Tab::All), 1);
    }

    void failedSaveKeepsTextAndReports()
    {
        Capture c;
        QSignalSpy errors(&c.controller, &CaptureController::errorOccurred);
        c.controller.begin();
        QTest::keyClicks(&c.edit, "a");
        QTRY_VERIFY_WITH_TIMEOUT(c.controller.currentId().has_value(), 2000);
        c.store().trash(*c.controller.currentId()); // trashed notes are read-only
        QTest::keyClicks(&c.edit, "b");
        c.controller.flush();
        QCOMPARE(errors.count(), 1);
        QCOMPARE(c.edit.toPlainText(), QString("ab"));
    }

    void beginStartsAFreshCapture()
    {
        Capture c;
        c.controller.begin();
        QTest::keyClicks(&c.edit, "one");
        c.controller.dismiss();
        c.controller.begin();
        QVERIFY(!c.controller.currentId());
        QVERIFY(c.edit.toPlainText().isEmpty());
        QTest::keyClicks(&c.edit, "two");
        c.controller.dismiss();
        QCOMPARE(c.count(Tab::All), 2);
    }
};

NOITE_TEST(CaptureControllerTest)
#include "tst_capture_controller.moc"
