#pragma once

#include "platform/HotkeyManager.h"

#include <noite/data_dir.h>
#include <noite/settings.h>

#include <QString>
#include <QWizard>

#include <functional>
#include <optional>

class BackupFolderField;
class HotkeyField;
class QCheckBox;
class QLabel;
class QSpinBox;
class QTimeEdit;

struct WizardContext {
    QString dataDir;
    std::optional<noite::SyncProvider> sync;
    bool autostartManaged = true;
    std::function<std::optional<QString>()> askFolder;
};

// The 8-page first-run setup wizard (D24, M6). Registers the capture hotkey on leaving page 2 and
// the board hotkey on leaving page 3 so page 8 ("Try it") can detect the real press.
class SetupWizard : public QWizard {
    Q_OBJECT

public:
    enum PageId {
        kWelcomePage = 0,
        kCapturePage = 1,
        kBoardPage = 2,
        kTrashPage = 3,
        kMorningsPage = 4,
        kBackupPage = 5,
        kAutostartPage = 6,
        kTryItPage = 7,
    };

    SetupWizard(noite::Settings initial, HotkeyManager& hotkeys, WizardContext context, QWidget* parent = nullptr);

    // The values on all pages, with setupCompleted untouched.
    noite::Settings collected() const;

    // Acts only on the last page, where it emits finished(collected(), true) and closes.
    void captureHotkeyPressed();
    // What "Skip setup" does; only enabled off page 2.
    void skip();

    void accept() override;
    void reject() override;

signals:
    void finished(noite::Settings settings, bool viaHotkey);
    void abandoned(noite::Settings partial);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void finish(bool viaHotkey);

    noite::Settings initial_;
    HotkeyManager& hotkeys_;
    WizardContext context_;
    HotkeyField* captureField_ = nullptr;
    HotkeyField* boardField_ = nullptr;
    QSpinBox* trashDays_ = nullptr;
    QTimeEdit* dayStart_ = nullptr;
    QSpinBox* staleDays_ = nullptr;
    BackupFolderField* backupField_ = nullptr;
    QCheckBox* startWithWindows_ = nullptr;
    QLabel* tryItLabel_ = nullptr;
    bool closed_ = false;
};
