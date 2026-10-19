#pragma once

#include "platform/HotkeyManager.h"

#include <noite/settings.h>

#include <QString>

#include <functional>
#include <optional>

namespace noite {
class Store;
}

struct ApplyError {
    QString message;
    int hotkeyId = 0;                       // the hotkey to blame; 0 when the error is not about a hotkey
    HotkeyStatus status = HotkeyStatus::Ok; // why that hotkey failed
};

// The one place a new Settings becomes real: hotkeys, the stored settings, autostart and the
// review schedule. Used by the wizard's finish and the Settings dialog's Save.
class SettingsApplier {
public:
    SettingsApplier(noite::Store& store, HotkeyManager& hotkeys, std::function<void(bool)> applyAutostart,
                    std::function<void()> recheckReview);

    // Hotkeys first (all or nothing, nothing else changes if they fail), then the save (the
    // hotkeys go back if it throws), then autostart and the review recheck if those changed.
    std::optional<ApplyError> apply(const noite::Settings& next);
    // Like apply(), but the hotkeys stay as they are: next's combinations are replaced by the ones
    // the manager holds (the stored ones where a slot holds none) and the manager is not touched.
    // The wizard's fallback when its hotkeys could not be applied.
    std::optional<ApplyError> applyKeepingHotkeys(noite::Settings next);

private:
    // Autostart and the review recheck, for what changed between the two.
    void followUp(const noite::Settings& previous, const noite::Settings& next);

    noite::Store& store_;
    HotkeyManager& hotkeys_;
    std::function<void(bool)> applyAutostart_;
    std::function<void()> recheckReview_;
};
