#include "helpers.h"
#include "registry.h"

#include "fake_hotkeys.h"
#include "setup/BackupFolderField.h"
#include "setup/HotkeyField.h"
#include "setup/SettingsApplier.h"
#include "setup/SettingsDialog.h"
#include "setup/SetupTexts.h"

#include <noite/store.h>

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <QTimeEdit>

using namespace noite;

namespace {

constexpr int kCap = HotkeyManager::kCaptureId;
constexpr int kBrd = HotkeyManager::kBoardId;

struct Rig {
    std::unique_ptr<test::Fixture> f = test::fixture();
    FakeBackend backend;
    HotkeyManager manager{backend};
    int autostartCalls = 0;
    bool autostartValue = false;
    int recheckCalls = 0;
    SettingsApplier applier;

    Rig()
        : applier(
              *f->store, manager,
              [this](bool on) {
                  ++autostartCalls;
                  autostartValue = on;
              },
              [this] { ++recheckCalls; })
    {
        const Settings s = f->store->settings();
        manager.set(kCap, s.captureHotkey);
        manager.set(kBrd, s.boardHotkey);
    }

    SettingsContext context() const
    {
        return {std::nullopt, true, f->root() + "/logs", [] { return std::optional<QString>(); },
                [] { return QDate(2026, 10, 18); }};
    }
};

QPushButton* findButton(QWidget& root, const QString& text)
{
    for (auto* btn : root.findChildren<QPushButton*>()) {
        if (btn->text() == text)
            return btn;
    }
    return nullptr;
}

} // namespace

class SettingsDialogTest : public QObject {
    Q_OBJECT

private slots:
    void showsCurrentSettings()
    {
        Rig r;
        Settings custom = r.f->store->settings();
        custom.captureHotkey = "Ctrl+Alt+J";
        custom.boardHotkey = "Ctrl+Alt+L";
        custom.trashRetentionDays = 40;
        custom.dayStart = QTime(7, 30);
        custom.staleDays = 9;
        custom.backupDir = QString("G:/Backups");
        custom.backupFrequency = BackupFrequency::Weekly;
        custom.backupKeep = 5;
        custom.startWithWindows = false;
        r.f->store->saveSettings(custom);
        r.manager.set(kCap, custom.captureHotkey);
        r.manager.set(kBrd, custom.boardHotkey);

        SettingsDialog dialog(*r.f->store, r.manager, r.applier, r.context());
        dialog.show();

        QCOMPARE(dialog.windowTitle(), QString("noite settings"));
        QCOMPARE(dialog.edited(), custom);
        QCOMPARE(dialog.captureField()->combo(), QString("Ctrl+Alt+J"));
        QCOMPARE(dialog.boardField()->combo(), QString("Ctrl+Alt+L"));
        QCOMPARE(dialog.backupField()->values().folder, std::optional<QString>("G:/Backups"));
        QVERIFY(dialog.backupField()->values().frequency == BackupFrequency::Weekly);
        QCOMPARE(dialog.backupField()->values().keep, 5);
    }

    void saveAppliesAndCloses()
    {
        Rig r;
        SettingsDialog dialog(*r.f->store, r.manager, r.applier, r.context());
        dialog.show();

        auto* trashSpin = dialog.trashDaysSpin();
        QVERIFY(trashSpin);
        trashSpin->setValue(50);

        dialog.save();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(!dialog.isVisible());
        QCOMPARE(r.f->store->settings().trashRetentionDays, 50);
    }

    void failedSaveStaysOpen()
    {
        Rig r;
        const Settings before = r.f->store->settings();
        SettingsDialog dialog(*r.f->store, r.manager, r.applier, r.context());
        dialog.show();

        dialog.boardField()->setCombo("Ctrl+Alt+K");
        QVERIFY(dialog.boardField()->isAccepted());

        r.backend.taken.insert("Ctrl+Alt+K");
        dialog.save();

        QVERIFY(dialog.isVisible());
        QCOMPARE(dialog.boardField()->statusText(),
                 QString("Ctrl+Alt+K is used by another program. Try another."));
        QVERIFY(!dialog.boardField()->isAccepted());
        QCOMPARE(r.f->store->settings(), before);
        QCOMPARE(r.manager.combo(kBrd), before.boardHotkey);
    }

    void cancelChangesNothing()
    {
        Rig r;
        const Settings before = r.f->store->settings();
        SettingsDialog dialog(*r.f->store, r.manager, r.applier, r.context());
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));

        dialog.trashDaysSpin()->setValue(99);
        // Start recording on capture field so the manager is paused, then click Cancel.
        QTest::mouseClick(dialog.captureField(), Qt::LeftButton);
        QVERIFY(r.manager.isPaused());

        QPushButton* cancelBtn = findButton(dialog, "Cancel");
        QVERIFY(cancelBtn);
        cancelBtn->click();

        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QVERIFY(!dialog.isVisible());
        QCOMPARE(r.f->store->settings(), before);
        QVERIFY(!r.manager.isPaused());
        QVERIFY(r.backend.has(kCap, before.captureHotkey));
        QVERIFY(r.backend.has(kBrd, before.boardHotkey));
    }

    void lastBackupLine()
    {
        Rig r;
        const QDate today(2026, 10, 18);
        JobState js;
        js.lastBackupDay = today;
        r.f->store->saveJobState(js);

        SettingsDialog dialog(*r.f->store, r.manager, r.applier, r.context());
        dialog.show();

        bool found = false;
        for (auto* label : dialog.findChildren<QLabel*>()) {
            if (label->text() == QString("Last backup: today")) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }

    void runSetupAgainEmits()
    {
        Rig r;
        SettingsDialog dialog(*r.f->store, r.manager, r.applier, r.context());
        dialog.show();

        QSignalSpy spy(&dialog, &SettingsDialog::runSetupRequested);
        QPushButton* btn = findButton(dialog, "Run setup again");
        QVERIFY(btn);
        btn->click();

        QCOMPARE(spy.count(), 1);
        QVERIFY(!dialog.isVisible());
    }

    void backUpNowEmits()
    {
        Rig r;
        SettingsDialog dialog(*r.f->store, r.manager, r.applier, r.context());
        dialog.show();

        QSignalSpy spy(&dialog, &SettingsDialog::backUpNowRequested);
        QPushButton* btn = findButton(dialog, "Back up now");
        QVERIFY(btn);
        btn->click();

        QCOMPARE(spy.count(), 1);
        QVERIFY(dialog.isVisible());
    }

    void syncWarningAndAutostartManaged()
    {
        Rig r;
        {
            SettingsDialog dialog(*r.f->store, r.manager, r.applier, r.context());
            dialog.show();
            for (auto* label : dialog.findChildren<QLabel*>())
                QVERIFY(label->text() != syncWarningText(SyncProvider::OneDrive));
            QVERIFY(dialog.startWithWindowsCheck()->isEnabled());
        }
        {
            SettingsContext ctx = r.context();
            ctx.sync = SyncProvider::OneDrive;
            ctx.autostartManaged = false;
            SettingsDialog dialog(*r.f->store, r.manager, r.applier, ctx);
            dialog.show();
            bool foundWarning = false;
            for (auto* label : dialog.findChildren<QLabel*>()) {
                if (label->text() == syncWarningText(SyncProvider::OneDrive)) {
                    foundWarning = label->isVisible();
                    break;
                }
            }
            QVERIFY(foundWarning);
            QVERIFY(!dialog.startWithWindowsCheck()->isEnabled());
        }
    }

    void swapHotkeysAndSave()
    {
        Rig r;
        const Settings before = r.f->store->settings();
        SettingsDialog dialog(*r.f->store, r.manager, r.applier, r.context());
        dialog.show();

        dialog.captureField()->setCombo(before.boardHotkey);
        dialog.boardField()->setCombo(before.captureHotkey);
        dialog.save();

        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(r.f->store->settings().captureHotkey, before.boardHotkey);
        QCOMPARE(r.f->store->settings().boardHotkey, before.captureHotkey);
        QCOMPARE(r.manager.combo(kCap), before.boardHotkey);
        QCOMPARE(r.manager.combo(kBrd), before.captureHotkey);
    }
};

NOITE_TEST(SettingsDialogTest)
#include "tst_settings_dialog.moc"
