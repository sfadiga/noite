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
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <QTimeEdit>

#include <sqlite3.h>

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

QLabel* findLabelStartingWith(QWidget& root, const QString& prefix)
{
    for (auto* label : root.findChildren<QLabel*>()) {
        if (label->text().startsWith(prefix))
            return label;
    }
    return nullptr;
}

// Runs `sql` on the fixture's database through a second connection (to make the store fail).
void execSql(const test::Fixture& f, const char* sql)
{
    sqlite3* db = nullptr;
    QVERIFY(sqlite3_open((f.root() + "/noite.db").toUtf8().constData(), &db) == SQLITE_OK);
    sqlite3_busy_timeout(db, 5000);
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    sqlite3_close(db);
    QCOMPARE(rc, SQLITE_OK);
}

using State = HotkeyField::State;
constexpr auto kCtrlAlt = Qt::ControlModifier | Qt::AltModifier;

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

    // Startup could not register the board hotkey: its field says so instead of ✓.
    void unregisteredHotkeyShowsTaken()
    {
        Rig r;
        const Settings s = r.f->store->settings();
        r.manager.clear(kBrd);
        r.backend.taken.insert(s.boardHotkey);
        QVERIFY(r.manager.set(kBrd, s.boardHotkey) == HotkeyStatus::Taken);

        SettingsDialog dialog(*r.f->store, r.manager, r.applier, r.context());
        dialog.show();
        QVERIFY(dialog.boardField()->state() == State::Taken);
        QCOMPARE(dialog.boardField()->combo(), s.boardHotkey);
        QCOMPARE(dialog.boardField()->statusText(), QString("%1 is used by another program. Try another.").arg(s.boardHotkey));
        QVERIFY(dialog.captureField()->isAccepted());
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
        QVERIFY(QTest::qWaitForWindowActive(&dialog));

        // Record the board's current shortcut into the capture field first.
        QTest::mouseClick(dialog.captureField(), Qt::LeftButton);
        QTest::keyClick(dialog.captureField(), Qt::Key_B, kCtrlAlt);
        QCOMPARE(dialog.captureField()->combo(), QString("Ctrl+Alt+B"));
        QCOMPARE(dialog.captureField()->statusText(), QString("That's already the board hotkey."));
        QVERIFY(dialog.captureField()->state() == State::SameAsOther);
        QVERIFY(!dialog.captureField()->isAccepted());

        // Recording into the board without a new combination: still the same pair.
        QTest::mouseClick(dialog.boardField(), Qt::LeftButton);
        QTest::keyClick(dialog.boardField(), Qt::Key_Escape);
        QVERIFY(dialog.boardField()->state() == State::Accepted);
        QVERIFY(dialog.captureField()->state() == State::SameAsOther);

        // Now record the old capture shortcut into the board field to complete the swap. The
        // capture field is accepted without a probe (the manager still holds Ctrl+Alt+B).
        QTest::mouseClick(dialog.boardField(), Qt::LeftButton);
        QTest::keyClick(dialog.boardField(), Qt::Key_J, kCtrlAlt);
        QCOMPARE(dialog.boardField()->combo(), QString("Ctrl+Alt+J"));
        QVERIFY(dialog.boardField()->state() == State::Accepted);
        QVERIFY(dialog.captureField()->state() == State::Accepted);
        QCOMPARE(dialog.captureField()->statusText(), QString("✓ Ctrl+Alt+B"));

        dialog.save();

        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(r.f->store->settings().captureHotkey, before.boardHotkey);
        QCOMPARE(r.f->store->settings().boardHotkey, before.captureHotkey);
        QCOMPARE(r.manager.combo(kCap), before.boardHotkey);
        QCOMPARE(r.manager.combo(kBrd), before.captureHotkey);
    }

    // The fields know each other's combination: the second one recorded is the duplicate.
    void sameComboInBothFieldsBlocksSave()
    {
        Rig r;
        const Settings before = r.f->store->settings();
        SettingsDialog dialog(*r.f->store, r.manager, r.applier, r.context());
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));

        QTest::mouseClick(dialog.captureField(), Qt::LeftButton);
        QTest::keyClick(dialog.captureField(), Qt::Key_L, kCtrlAlt);
        QVERIFY(dialog.captureField()->state() == State::Accepted);
        QTest::mouseClick(dialog.boardField(), Qt::LeftButton);
        QTest::keyClick(dialog.boardField(), Qt::Key_L, kCtrlAlt);
        QVERIFY(dialog.boardField()->state() == State::SameAsOther);
        QCOMPARE(dialog.boardField()->statusText(), QString("That's already the new-note hotkey."));

        dialog.save();

        QVERIFY(dialog.isVisible());
        QVERIFY(dialog.result() != int(QDialog::Accepted));
        QVERIFY(dialog.boardField()->state() == State::SameAsOther);
        QCOMPARE(dialog.boardField()->statusText(), QString("That's already the new-note hotkey."));
        QVERIFY(dialog.captureField()->state() == State::Accepted);
        QVERIFY(dialog.errorText().isEmpty());
        QCOMPARE(r.f->store->settings(), before);
        QCOMPARE(r.manager.combo(kCap), before.captureHotkey);
        QCOMPARE(r.manager.combo(kBrd), before.boardHotkey);
        QCOMPARE(r.backend.registered.size(), 2);
        QVERIFY(r.backend.has(kCap, before.captureHotkey));
        QVERIFY(r.backend.has(kBrd, before.boardHotkey));
    }

    void swapStepThenCancelChangesNothing()
    {
        Rig r;
        const Settings before = r.f->store->settings();
        SettingsDialog dialog(*r.f->store, r.manager, r.applier, r.context());
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));

        QTest::mouseClick(dialog.captureField(), Qt::LeftButton);
        QTest::keyClick(dialog.captureField(), Qt::Key_B, kCtrlAlt);
        QVERIFY(dialog.captureField()->state() == State::SameAsOther);
        findButton(dialog, "Cancel")->click();

        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(r.f->store->settings(), before);
        QVERIFY(!r.manager.isPaused());
        QCOMPARE(r.manager.combo(kCap), before.captureHotkey);
        QCOMPARE(r.manager.combo(kBrd), before.boardHotkey);
        QCOMPARE(r.backend.registered.size(), 2);
        QVERIFY(r.backend.has(kCap, before.captureHotkey));
        QVERIFY(r.backend.has(kBrd, before.boardHotkey));
    }

    void captureTakenOnSaveShowsOnCaptureField()
    {
        Rig r;
        const Settings before = r.f->store->settings();
        SettingsDialog dialog(*r.f->store, r.manager, r.applier, r.context());
        dialog.show();

        dialog.captureField()->setCombo("Ctrl+Alt+K");
        r.backend.taken.insert("Ctrl+Alt+K");
        dialog.save();

        QVERIFY(dialog.isVisible());
        QVERIFY(dialog.captureField()->state() == State::Taken);
        QCOMPARE(dialog.captureField()->statusText(), QString("Ctrl+Alt+K is used by another program. Try another."));
        QVERIFY(dialog.boardField()->state() == State::Accepted);
        QVERIFY(dialog.errorText().isEmpty());
        QCOMPARE(r.f->store->settings(), before);
        QVERIFY(r.backend.has(kCap, before.captureHotkey));
    }

    void storeFailureOnSaveShowsError()
    {
        Rig r;
        const Settings before = r.f->store->settings();
        SettingsDialog dialog(*r.f->store, r.manager, r.applier, r.context());
        dialog.show();

        dialog.trashDaysSpin()->setValue(50);
        execSql(*r.f, "CREATE TRIGGER no_save BEFORE INSERT ON settings WHEN NEW.key = 'settings' "
                      "BEGIN SELECT RAISE(ABORT, 'disk full'); END");
        dialog.save();

        QVERIFY(dialog.isVisible());
        QVERIFY(dialog.result() != int(QDialog::Accepted));
        QVERIFY2(dialog.errorText().contains("disk full"), qPrintable(dialog.errorText()));
        QCOMPARE(r.f->store->settings(), before);
        QVERIFY(r.backend.has(kCap, before.captureHotkey));
        QVERIFY(r.backend.has(kBrd, before.boardHotkey));
    }

    // A store that can't even be read: Save reports it instead of throwing out of the slot.
    void unreadableStoreOnSaveShowsError()
    {
        Rig r;
        SettingsDialog dialog(*r.f->store, r.manager, r.applier, r.context());
        dialog.show();

        execSql(*r.f, "ALTER TABLE settings RENAME TO settings_gone");
        QVERIFY_THROWS_NO_EXCEPTION(dialog.save());
        execSql(*r.f, "ALTER TABLE settings_gone RENAME TO settings");

        QVERIFY(dialog.isVisible());
        QVERIFY2(dialog.errorText().contains("no such table"), qPrintable(dialog.errorText()));
    }

    void lastBackupNeverWhenJobStateFails()
    {
        Rig r;
        JobState js;
        js.lastBackupDay = QDate(2026, 10, 18);
        r.f->store->saveJobState(js);
        SettingsDialog dialog(*r.f->store, r.manager, r.applier, r.context());
        dialog.show();
        QVERIFY(findLabelStartingWith(dialog, "Last backup: today"));

        execSql(*r.f, "ALTER TABLE settings RENAME TO settings_gone");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^settings: last backup: .*no such table"));
        QVERIFY_THROWS_NO_EXCEPTION(dialog.refreshLastBackup());
        execSql(*r.f, "ALTER TABLE settings_gone RENAME TO settings");

        QLabel* label = findLabelStartingWith(dialog, "Last backup: ");
        QVERIFY(label);
        QCOMPARE(label->text(), QString("Last backup: never"));
    }

    void openLogsFolderCreatesItAndReportsFailure()
    {
        Rig r;
        SettingsContext ctx = r.context();
        ctx.logsDir = r.f->root() + "/missing/logs";
        QList<QUrl> opened;
        bool opens = false;
        ctx.openUrl = [&](const QUrl& url) {
            opened.append(url);
            return opens;
        };
        SettingsDialog dialog(*r.f->store, r.manager, r.applier, ctx);
        dialog.show();
        QPushButton* btn = findButton(dialog, "Open logs folder");
        QVERIFY(btn);

        btn->click();
        QVERIFY(QDir(ctx.logsDir).exists());
        QCOMPARE(opened, QList<QUrl>{QUrl::fromLocalFile(ctx.logsDir)});
        QCOMPARE(dialog.errorText(),
                 QString("The logs folder could not be opened: %1").arg(QDir::toNativeSeparators(ctx.logsDir)));

        opens = true;
        btn->click();
        QCOMPARE(opened.size(), 2);
        QVERIFY(dialog.errorText().isEmpty());
        QVERIFY(dialog.isVisible());
    }
};

NOITE_TEST(SettingsDialogTest)
#include "tst_settings_dialog.moc"
