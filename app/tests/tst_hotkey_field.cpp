#include "registry.h"

#include "fake_hotkeys.h"
#include "setup/HotkeyField.h"

#include <QDialog>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTest>
#include <QVBoxLayout>

namespace {

constexpr int kCap = HotkeyManager::kCaptureId;
constexpr int kBrd = HotkeyManager::kBoardId;
constexpr auto kCtrlAlt = Qt::ControlModifier | Qt::AltModifier;

// A field and a plain line edit (a place for focus to go) in a shown, active dialog.
struct Rig {
    FakeBackend backend;
    HotkeyManager manager{backend};
    QDialog dialog;
    HotkeyField* field = nullptr;
    QLineEdit* other = nullptr;

    explicit Rig(int id = kCap)
    {
        auto* layout = new QVBoxLayout(&dialog);
        field = new HotkeyField(manager, id);
        other = new QLineEdit;
        layout->addWidget(field);
        layout->addWidget(other);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        other->setFocus();
    }
};

} // namespace

class HotkeyFieldTest : public QObject {
    Q_OBJECT

private slots:
    void acceptsFreeCombo()
    {
        Rig rig;
        QSignalSpy changed(rig.field, &HotkeyField::changed);
        QTest::mouseClick(rig.field, Qt::LeftButton);
        QVERIFY(rig.field->isRecording());
        QVERIFY(rig.manager.isPaused());
        QTest::keyClick(rig.field, Qt::Key_K, kCtrlAlt);
        QVERIFY(rig.field->isAccepted());
        QCOMPARE(rig.field->combo(), QString("Ctrl+Alt+K"));
        QCOMPARE(rig.field->statusText(), QString::fromUtf8("✓ Ctrl+Alt+K"));
        QVERIFY(!rig.field->isRecording());
        QVERIFY(!rig.manager.isPaused());
        QCOMPARE(changed.count(), 1);
    }

    void rejectsTakenAndInvalid()
    {
        Rig rig;
        rig.backend.taken.insert("Ctrl+Alt+N");
        QTest::mouseClick(rig.field, Qt::LeftButton);
        QTest::keyClick(rig.field, Qt::Key_N, kCtrlAlt);
        QCOMPARE(rig.field->statusText(), QString("Ctrl+Alt+N is used by another program. Try another."));
        QVERIFY(!rig.field->isAccepted());
        QVERIFY(rig.field->isRecording());
        QTest::keyClick(rig.field, Qt::Key_J, Qt::ShiftModifier);
        QCOMPARE(rig.field->statusText(), QString("Use Ctrl or Alt with a key."));
        QVERIFY(rig.field->isRecording());
    }

    void rejectsOther()
    {
        Rig rig(kCap);
        rig.field->setOther("Ctrl+Alt+B");
        QTest::mouseClick(rig.field, Qt::LeftButton);
        QTest::keyClick(rig.field, Qt::Key_B, kCtrlAlt);
        QCOMPARE(rig.field->statusText(), QString("That's already the board hotkey."));
        QVERIFY(!rig.field->isAccepted());
        QVERIFY(rig.field->isRecording());
    }

    void otherFieldNamesNewNote()
    {
        Rig rig(kBrd);
        rig.field->setOther("Ctrl+Alt+J");
        QTest::mouseClick(rig.field, Qt::LeftButton);
        QTest::keyClick(rig.field, Qt::Key_J, kCtrlAlt);
        QCOMPARE(rig.field->statusText(), QString("That's already the new-note hotkey."));
    }

    void stateFollowsTheField()
    {
        using State = HotkeyField::State;
        Rig rig;
        QVERIFY(rig.field->state() == State::Empty);
        rig.field->setCombo("Ctrl+Alt+J");
        QVERIFY(rig.field->state() == State::Accepted);
        QTest::mouseClick(rig.field, Qt::LeftButton);
        QVERIFY(rig.field->state() == State::Recording);
        QTest::keyClick(rig.field, Qt::Key_Escape);
        QVERIFY(rig.field->state() == State::Accepted);

        rig.field->showError("taken");
        QVERIFY(rig.field->state() == State::Taken);
        rig.field->showError("invalid", HotkeyStatus::Invalid);
        QVERIFY(rig.field->state() == State::Invalid);
        rig.field->showError("same", HotkeyStatus::SameAsOther);
        QVERIFY(rig.field->state() == State::SameAsOther);
        QCOMPARE(rig.field->combo(), QString("Ctrl+Alt+J"));
        QVERIFY(!rig.field->isAccepted());
    }

    // Settings keeps a duplicate so the two hotkeys can be swapped before Save.
    void keepsSameAsOtherWhenAsked()
    {
        Rig rig(kCap);
        rig.field->setKeepsSameAsOther(true);
        rig.field->setOther("Ctrl+Alt+B");
        QSignalSpy changed(rig.field, &HotkeyField::changed);
        QTest::mouseClick(rig.field, Qt::LeftButton);
        QTest::keyClick(rig.field, Qt::Key_B, kCtrlAlt);
        QVERIFY(rig.field->state() == HotkeyField::State::SameAsOther);
        QCOMPARE(rig.field->combo(), QString("Ctrl+Alt+B"));
        QCOMPARE(rig.field->statusText(), QString("That's already the board hotkey."));
        QVERIFY(!rig.field->isRecording());
        QVERIFY(!rig.manager.isPaused());
        QCOMPARE(changed.count(), 1);
    }

    void modifierAloneKeepsWaiting()
    {
        Rig rig;
        QTest::mouseClick(rig.field, Qt::LeftButton);
        const QString before = rig.field->statusText();
        QTest::keyPress(rig.field, Qt::Key_Control, Qt::ControlModifier);
        QTest::keyPress(rig.field, Qt::Key_Alt, kCtrlAlt);
        QVERIFY(rig.field->isRecording());
        QCOMPARE(rig.field->statusText(), before);
        QTest::keyClick(rig.field, Qt::Key_K, kCtrlAlt);
        QVERIFY(rig.field->isAccepted());
    }

    void escCancelsRecording()
    {
        Rig rig;
        rig.field->setCombo("Ctrl+Alt+J");
        const QString status = rig.field->statusText();
        QTest::mouseClick(rig.field, Qt::LeftButton);
        QVERIFY(rig.manager.isPaused());
        QTest::keyClick(rig.field, Qt::Key_Escape);
        QCOMPARE(rig.field->combo(), QString("Ctrl+Alt+J"));
        QVERIFY(rig.field->isAccepted());
        QCOMPARE(rig.field->statusText(), status);
        QVERIFY(!rig.field->isRecording());
        QVERIFY(!rig.manager.isPaused());
        QVERIFY(rig.dialog.isVisible()); // the field took the Esc; the dialog stays open
    }

    void focusOutResumesHotkeys()
    {
        Rig rig;
        rig.field->setCombo("Ctrl+Alt+J");
        QTest::mouseClick(rig.field, Qt::LeftButton);
        QVERIFY(rig.manager.isPaused());
        rig.other->setFocus();
        QVERIFY(!rig.field->isRecording());
        QVERIFY(!rig.manager.isPaused());
        QCOMPARE(rig.field->combo(), QString("Ctrl+Alt+J"));
        QVERIFY(rig.field->isAccepted());
    }

    void destroyedWhileRecordingResumes()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        {
            QDialog dialog;
            auto* field = new HotkeyField(manager, kCap, &dialog);
            dialog.show();
            QVERIFY(QTest::qWaitForWindowActive(&dialog));
            QTest::mouseClick(field, Qt::LeftButton);
            QVERIFY(manager.isPaused());
        }
        QVERIFY(!manager.isPaused());
    }

    void recordsDigitsByNativeKey()
    {
        Rig rig;
        QTest::mouseClick(rig.field, Qt::LeftButton);
        // Qt may report a shifted symbol for Shift+7; the virtual key says it is the 7 key.
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Slash, Qt::ControlModifier | Qt::ShiftModifier, 0x08, '7', 0, "/");
        QApplication::sendEvent(rig.field, &press);
        QCOMPARE(rig.field->combo(), QString("Ctrl+Shift+7"));
        QVERIFY(rig.field->isAccepted());
    }
};

NOITE_TEST(HotkeyFieldTest)
#include "tst_hotkey_field.moc"
