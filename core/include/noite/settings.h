#pragma once

#include <QString>
#include <QTime>

#include <optional>

namespace noite {

enum class BackupFrequency { Daily, Weekly };

// Everything the setup wizard asks for (D24), with the spec's defaults. Stored as JSON;
// keys missing from older data fall back to these defaults.
struct Settings {
    QString captureHotkey = "Ctrl+Alt+J";
    QString boardHotkey = "Ctrl+Alt+B";
    int trashRetentionDays = 30;
    QTime dayStart{6, 0};
    std::optional<QString> backupDir;
    BackupFrequency backupFrequency = BackupFrequency::Daily;
    int backupKeep = 14;
    bool startWithWindows = true;
    bool setupCompleted = false;

    bool operator==(const Settings&) const = default;
};

// Throws InvalidSetting: retention must be 1-365 days, backupKeep at least 1.
// Hotkey strings are validated by the app (it registers them), not here.
void validate(const Settings& settings);

} // namespace noite
