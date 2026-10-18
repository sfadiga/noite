#include "setup/SettingsApplier.h"

#include "platform/HotkeyManager.h"

#include <noite/store.h>

#include <exception>

namespace {

// Whether `wanted` is a valid combination other than the one the manager holds. An invalid
// one counts as different, so the manager gets to name the problem.
bool differs(const QString& wanted, const QString& held)
{
    const auto parsed = parseHotkey(wanted);
    return !parsed || parsed != parseHotkey(held);
}

} // namespace

SettingsApplier::SettingsApplier(noite::Store& store, HotkeyManager& hotkeys, std::function<void(bool)> applyAutostart,
                                 std::function<void()> recheckReview)
    : store_(store)
    , hotkeys_(hotkeys)
    , applyAutostart_(std::move(applyAutostart))
    , recheckReview_(std::move(recheckReview))
{
}

std::optional<ApplyError> SettingsApplier::apply(const noite::Settings& next)
{
    const noite::Settings previous = store_.settings();
    const QString oldCapture = hotkeys_.combo(HotkeyManager::kCaptureId);
    const QString oldBoard = hotkeys_.combo(HotkeyManager::kBoardId);

    const bool hotkeysChanged = differs(next.captureHotkey, oldCapture) || differs(next.boardHotkey, oldBoard);
    if (hotkeysChanged) {
        const HotkeyOutcome outcome = hotkeys_.apply(next.captureHotkey, next.boardHotkey);
        if (outcome.status != HotkeyStatus::Ok) {
            const bool capture = outcome.failedId == HotkeyManager::kCaptureId;
            const int otherId = capture ? HotkeyManager::kBoardId : HotkeyManager::kCaptureId;
            return ApplyError{hotkeyMessage(outcome.status, capture ? next.captureHotkey : next.boardHotkey, otherId),
                              outcome.failedId};
        }
    }

    try {
        store_.saveSettings(next);
    } catch (const std::exception& error) {
        if (hotkeysChanged)
            hotkeys_.apply(oldCapture, oldBoard);
        return ApplyError{QString::fromUtf8(error.what()), 0};
    }

    if (next.startWithWindows != previous.startWithWindows)
        applyAutostart_(next.startWithWindows);
    if (next.dayStart != previous.dayStart || next.staleDays != previous.staleDays)
        recheckReview_();
    return std::nullopt;
}
