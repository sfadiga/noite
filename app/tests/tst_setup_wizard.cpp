#include "registry.h"

#include "fake_hotkeys.h"
#include "setup/BackupFolderField.h"
#include "setup/HotkeyField.h"
#include "setup/SetupTexts.h"
#include "setup/SetupWizard.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QLabel>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <QTimeEdit>

using namespace noite;

namespace {

constexpr int kCap = HotkeyManager::kCaptureId;
constexpr int kBrd = HotkeyManager::kBoardId;
constexpr auto kCtrlAlt = Qt::ControlModifier | Qt::AltModifier;

WizardContext defaultContext()
{
    return {QString("D:/noite/data"), std::nullopt, true, [] { return std::optional<QString>(); }};
}

void advanceToPage(SetupWizard& wizard, int pageNumber)
{
    // Pages are 1-indexed in the spec (1..8), corresponding to 0..7 steps from start.
    for (int i = 1; i < pageNumber; ++i)
        wizard.next();
}

} // namespace

class SetupWizardTest : public QObject {
    Q_OBJECT

private slots:
    void prefilledFromSettings()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        Settings initial;
        initial.captureHotkey = "Ctrl+Alt+J";
        initial.boardHotkey = "Ctrl+Alt+B";
        initial.trashRetentionDays = 40;
        initial.dayStart = QTime(7, 30);
        initial.staleDays = 9;
        initial.backupDir = QString("G:/x");
        initial.backupFrequency = BackupFrequency::Weekly;
        initial.backupKeep = 12;
        initial.startWithWindows = false;

        SetupWizard wizard(initial, manager, defaultContext());
        wizard.show();
        QCOMPARE(wizard.windowTitle(), QString("Set up noite"));
        QCOMPARE(wizard.wizardStyle(), QWizard::ModernStyle);

        // Page 1: welcome text includes data dir
        auto* page1Label = wizard.currentPage()->findChild<QLabel*>();
        QVERIFY(page1Label);
        QCOMPARE(page1Label->text(), QString("Your notes live in this folder: D:/noite/data. Nothing leaves your PC."));

        // Page 2: capture hotkey
        wizard.next();
        auto* capField = wizard.currentPage()->findChild<HotkeyField*>();
        QVERIFY(capField);
        QCOMPARE(capField->combo(), QString("Ctrl+Alt+J"));

        // Page 3: board hotkey
        wizard.next();
        auto* brdField = wizard.currentPage()->findChild<HotkeyField*>();
        QVERIFY(brdField);
        QCOMPARE(brdField->combo(), QString("Ctrl+Alt+B"));

        // Page 4: trash retention
        wizard.next();
        auto* trashSpin = wizard.currentPage()->findChild<QSpinBox*>();
        QVERIFY(trashSpin);
        QCOMPARE(trashSpin->value(), 40);
        QCOMPARE(trashSpin->minimum(), 1);
        QCOMPARE(trashSpin->maximum(), 365);

        // Page 5: dayStart and staleDays
        wizard.next();
        auto* dayEdit = wizard.currentPage()->findChild<QTimeEdit*>();
        auto* staleSpin = wizard.currentPage()->findChild<QSpinBox*>();
        QVERIFY(dayEdit);
        QVERIFY(staleSpin);
        QCOMPARE(dayEdit->time(), QTime(7, 30));
        QCOMPARE(staleSpin->value(), 9);
        QCOMPARE(staleSpin->minimum(), 1);
        QCOMPARE(staleSpin->maximum(), 90);

        // Page 6: backup
        wizard.next();
        auto* backupField = wizard.currentPage()->findChild<BackupFolderField*>();
        QVERIFY(backupField);
        QCOMPARE(backupField->values().folder, std::optional<QString>("G:/x"));
        QVERIFY(backupField->values().frequency == BackupFrequency::Weekly);
        QCOMPARE(backupField->values().keep, 12);

        // Page 7: start with Windows
        wizard.next();
        auto* autostartCheck = wizard.currentPage()->findChild<QCheckBox*>();
        QVERIFY(autostartCheck);
        QCOMPARE(autostartCheck->text(), QString("Start noite with Windows"));
        QVERIFY(!autostartCheck->isChecked());
        QVERIFY(autostartCheck->isEnabled());

        QCOMPARE(wizard.collected(), initial);
    }

    void page2NeedsAcceptedHotkey()
    {
        FakeBackend backend;
        backend.taken.insert("Ctrl+Alt+J");
        backend.taken.insert("Ctrl+Alt+N");
        HotkeyManager manager(backend);
        Settings initial;
        initial.captureHotkey = "Ctrl+Alt+J";

        SetupWizard wizard(initial, manager, defaultContext());
        wizard.show();
        QVERIFY(QTest::qWaitForWindowActive(&wizard));
        wizard.next(); // page 2

        auto* field = wizard.currentPage()->findChild<HotkeyField*>();
        QVERIFY(field);
        QVERIFY(!field->isAccepted());
        QCOMPARE(field->statusText(), QString("Ctrl+Alt+J is used by another program. Try another."));
        QVERIFY(!wizard.button(QWizard::NextButton)->isEnabled());

        QTest::mouseClick(field, Qt::LeftButton);
        QTest::keyClick(field, Qt::Key_K, kCtrlAlt);
        QVERIFY(field->isAccepted());
        QVERIFY(wizard.button(QWizard::NextButton)->isEnabled());

        // Clicking again starts recording and disables Next until accepted or Esc.
        QTest::mouseClick(field, Qt::LeftButton);
        QVERIFY(field->isRecording());
        QVERIFY(!wizard.button(QWizard::NextButton)->isEnabled());
        QTest::keyClick(field, Qt::Key_N, kCtrlAlt); // taken
        QVERIFY(!wizard.button(QWizard::NextButton)->isEnabled());
        QTest::keyClick(field, Qt::Key_Escape); // restores Ctrl+Alt+K
        QVERIFY(!field->isRecording());
        QVERIFY(wizard.button(QWizard::NextButton)->isEnabled());

        wizard.next(); // leaves page 2 -> page 3
        QCOMPARE(manager.combo(kCap), QString("Ctrl+Alt+K"));
        QVERIFY(backend.has(kCap, "Ctrl+Alt+K"));
    }

    void backFromPage3UpdatesOtherAndManager()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        SetupWizard wizard(Settings{}, manager, defaultContext());
        wizard.show();
        QVERIFY(QTest::qWaitForWindowActive(&wizard));

        wizard.next(); // page 2
        wizard.next(); // page 3 (registers capture Ctrl+Alt+J)

        auto* brdField = wizard.currentPage()->findChild<HotkeyField*>();
        QVERIFY(brdField);
        QTest::mouseClick(brdField, Qt::LeftButton);
        QTest::keyClick(brdField, Qt::Key_L, kCtrlAlt);
        QCOMPARE(brdField->combo(), QString("Ctrl+Alt+L"));

        wizard.back(); // back to page 2
        QCOMPARE(manager.combo(kBrd), QString("Ctrl+Alt+L"));
        auto* capField = wizard.currentPage()->findChild<HotkeyField*>();
        QVERIFY(capField);
        QTest::mouseClick(capField, Qt::LeftButton);
        QTest::keyClick(capField, Qt::Key_L, kCtrlAlt);
        QCOMPARE(capField->statusText(), QString("That's already the board hotkey."));
        QVERIFY(!capField->isAccepted());
        QTest::keyClick(capField, Qt::Key_Escape);
        QVERIFY(capField->isAccepted());
    }

    void skipOfferedExceptPage2()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        SetupWizard wizard(Settings{}, manager, defaultContext());
        wizard.show();

        QVERIFY(wizard.testOption(QWizard::NoCancelButton));
        QVERIFY(!wizard.button(QWizard::CancelButton)->isVisible());

        QAbstractButton* skipBtn = wizard.button(QWizard::CustomButton1);
        QVERIFY(skipBtn);
        QCOMPARE(skipBtn->text(), QString("Skip setup"));

        // Page 1: visible
        QVERIFY(skipBtn->isVisible());

        // Page 2: hidden
        wizard.next();
        QVERIFY(!skipBtn->isVisible());

        // Pages 3..8: visible
        for (int page = 3; page <= 8; ++page) {
            wizard.next();
            QVERIFY2(skipBtn->isVisible(), qPrintable(QString("page %1").arg(page)));
        }
    }

    void skipFinishesWithShownValues()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        SetupWizard wizard(Settings{}, manager, defaultContext());
        wizard.show();

        QSignalSpy finishedSpy(&wizard, &SetupWizard::finished);
        QSignalSpy abandonedSpy(&wizard, &SetupWizard::abandoned);

        advanceToPage(wizard, 4);
        auto* trashSpin = wizard.currentPage()->findChild<QSpinBox*>();
        QVERIFY(trashSpin);
        trashSpin->setValue(50);

        wizard.skip();
        QCOMPARE(finishedSpy.count(), 1);
        QCOMPARE(abandonedSpy.count(), 0);
        const auto args = finishedSpy.first();
        const auto out = args.at(0).value<Settings>();
        const bool viaHotkey = args.at(1).toBool();
        QCOMPARE(out.trashRetentionDays, 50);
        QVERIFY(!out.setupCompleted);
        QVERIFY(!viaHotkey);
        QVERIFY(!wizard.isVisible());
    }

    void skipFromPage1JumpsToPage2WhenCaptureTaken()
    {
        FakeBackend backend;
        backend.taken.insert("Ctrl+Alt+J");
        HotkeyManager manager(backend);
        SetupWizard wizard(Settings{}, manager, defaultContext());
        wizard.show();

        QSignalSpy finishedSpy(&wizard, &SetupWizard::finished);
        wizard.skip();
        QCOMPARE(finishedSpy.count(), 0);
        QVERIFY(wizard.isVisible());
        auto* field = wizard.currentPage()->findChild<HotkeyField*>();
        QVERIFY(field);
        QVERIFY(!field->isAccepted());
        QVERIFY(!wizard.button(QWizard::NextButton)->isEnabled());
    }

    void captureHotkeyIgnoredBeforeLastPage()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        SetupWizard wizard(Settings{}, manager, defaultContext());
        wizard.show();

        QSignalSpy finishedSpy(&wizard, &SetupWizard::finished);
        advanceToPage(wizard, 3);
        const int page3Id = wizard.currentId();

        wizard.captureHotkeyPressed();
        QCOMPARE(finishedSpy.count(), 0);
        QCOMPARE(wizard.currentId(), page3Id);
        QVERIFY(wizard.isVisible());
    }

    void captureHotkeyOnLastPageFinishes()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        SetupWizard wizard(Settings{}, manager, defaultContext());
        wizard.show();

        QSignalSpy finishedSpy(&wizard, &SetupWizard::finished);
        advanceToPage(wizard, 8);

        auto* label = wizard.currentPage()->findChild<QLabel*>();
        QVERIFY(label);
        QCOMPARE(label->text(), QString("Press Ctrl+Alt+J now."));

        wizard.captureHotkeyPressed();
        QCOMPARE(finishedSpy.count(), 1);
        const auto args = finishedSpy.first();
        QCOMPARE(args.at(0).value<Settings>(), Settings{});
        QVERIFY(args.at(1).toBool());
        QVERIFY(!wizard.isVisible());
    }

    void finishButtonFinishes()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        SetupWizard wizard(Settings{}, manager, defaultContext());
        wizard.show();

        QSignalSpy finishedSpy(&wizard, &SetupWizard::finished);
        advanceToPage(wizard, 8);

        wizard.button(QWizard::FinishButton)->click();
        QCOMPARE(finishedSpy.count(), 1);
        const auto args = finishedSpy.first();
        QCOMPARE(args.at(0).value<Settings>(), Settings{});
        QVERIFY(!args.at(1).toBool());
        QVERIFY(!wizard.isVisible());
    }

    void escAbandons()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        SetupWizard wizard(Settings{}, manager, defaultContext());
        wizard.show();

        QSignalSpy finishedSpy(&wizard, &SetupWizard::finished);
        QSignalSpy abandonedSpy(&wizard, &SetupWizard::abandoned);

        advanceToPage(wizard, 4);
        auto* trashSpin = wizard.currentPage()->findChild<QSpinBox*>();
        QVERIFY(trashSpin);
        trashSpin->setValue(50);

        wizard.reject();
        QCOMPARE(finishedSpy.count(), 0);
        QCOMPARE(abandonedSpy.count(), 1);
        const auto out = abandonedSpy.first().at(0).value<Settings>();
        QCOMPARE(out.trashRetentionDays, 50);
        QVERIFY(!wizard.isVisible());
    }

    void syncWarningOnlyWithProvider()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);

        {
            SetupWizard wizard(Settings{}, manager, defaultContext());
            wizard.show();
            const auto labels = wizard.currentPage()->findChildren<QLabel*>();
            QCOMPARE(labels.size(), 1);
        }
        {
            WizardContext ctx = defaultContext();
            ctx.sync = SyncProvider::GoogleDrive;
            SetupWizard wizard(Settings{}, manager, ctx);
            wizard.show();
            const auto labels = wizard.currentPage()->findChildren<QLabel*>();
            QCOMPARE(labels.size(), 2);
            QCOMPARE(labels.at(1)->text(), syncWarningText(SyncProvider::GoogleDrive));
            QVERIFY(labels.at(1)->isVisible());
        }
    }

    void autostartDisabledForDataRun()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        WizardContext ctx = defaultContext();
        ctx.autostartManaged = false;

        SetupWizard wizard(Settings{}, manager, ctx);
        wizard.show();
        advanceToPage(wizard, 7);

        auto* check = wizard.currentPage()->findChild<QCheckBox*>();
        QVERIFY(check);
        QVERIFY(!check->isEnabled());
    }
};

NOITE_TEST(SetupWizardTest)
#include "tst_setup_wizard.moc"
