#pragma once

#include "platform/HotkeyManager.h"

#include <noite/data_dir.h>
#include <noite/settings.h>

#include <QDate>
#include <QDialog>
#include <QString>

#include <functional>
#include <optional>

namespace noite {
class Store;
}
class BackupFolderField;
class HotkeyField;
class QCheckBox;
class QLabel;
class QSpinBox;
class QTimeEdit;
class QUrl;
class SettingsApplier;

struct SettingsContext {
    std::optional<noite::SyncProvider> sync;
    bool autostartManaged = true;
    QString logsDir;
    std::function<std::optional<QString>()> askFolder;
    std::function<QDate()> today;
    // Opens the logs folder; QDesktopServices::openUrl when empty (tests replace it).
    std::function<bool(const QUrl&)> openUrl;
};

// The one-page Settings window (D25, M6). Groups Hotkeys, Notes, Backup and System; Save applies
// through SettingsApplier (staying open on error) and Cancel discards and resumes any paused
// hotkeys. The two hotkey fields know each other's combination: one may hold the other's for a
// while (SameAsOther), so they can be swapped, but Save waits until they differ.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(noite::Store& store, HotkeyManager& hotkeys, SettingsApplier& applier, SettingsContext context,
                   QWidget* parent = nullptr);

    noite::Settings edited() const;
    QString errorText() const;

    void save();
    void reject() override;
    void refreshLastBackup();

    HotkeyField* captureField() const { return captureField_; }
    HotkeyField* boardField() const { return boardField_; }
    QSpinBox* trashDaysSpin() const { return trashDays_; }
    QTimeEdit* dayStartEdit() const { return dayStart_; }
    QSpinBox* staleDaysSpin() const { return staleDays_; }
    BackupFolderField* backupField() const { return backupField_; }
    QCheckBox* startWithWindowsCheck() const { return startWithWindows_; }

signals:
    void runSetupRequested();
    void backUpNowRequested();

private:
    void showMessage(const QString& message);

    noite::Store& store_;
    HotkeyManager& hotkeys_;
    SettingsApplier& applier_;
    SettingsContext context_;
    HotkeyField* captureField_ = nullptr;
    HotkeyField* boardField_ = nullptr;
    QSpinBox* trashDays_ = nullptr;
    QTimeEdit* dayStart_ = nullptr;
    QSpinBox* staleDays_ = nullptr;
    BackupFolderField* backupField_ = nullptr;
    QLabel* lastBackupLabel_ = nullptr;
    QCheckBox* startWithWindows_ = nullptr;
    QLabel* errorLabel_ = nullptr;
};
