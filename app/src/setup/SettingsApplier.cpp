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
                              outcome.failedId, outcome.status};
        }
    }

    try {
        store_.saveSettings(next);
    } catch (const std::exception& error) {
        QString message = QString::fromUtf8(error.what());
        if (hotkeysChanged) {
            // Per slot: a slot that held nothing goes back to nothing, and a restore that fails
            // leaves the slot empty rather than holding the unsaved combination. Both are
            // cleared first so a swap can be undone.
            hotkeys_.clear(HotkeyManager::kCaptureId);
            hotkeys_.clear(HotkeyManager::kBoardId);
            const struct {
                int id;
                const QString& combo;
                const char* name;
            } restore[] ={{HotkeyManager::kCaptureId, oldCapture, "new-note"}, {HotkeyManager::kBoardId, oldBoard, "board"}};
            for (const auto& s : restore) {
                if (!s.combo.isEmpty() && hotkeys_.set(s.id, s.combo) != HotkeyStatus::Ok)
                    message += QStringLiteral(" The previous %1 hotkey %2 could not be restored.")
                                   .arg(QString::fromLatin1(s.name), s.combo);
            }
        }
        return ApplyError{message, 0};
    }

    followUp(previous, next);
    return std::nullopt;
}

std::optional<ApplyError> SettingsApplier::applyKeepingHotkeys(noite::Settings next)
{
    const noite::Settings previous = store_.settings();
    const auto held = [this](int id, const QString& stored) {
        return hotkeys_.isRegistered(id) ? hotkeys_.combo(id) : stored;
    };
    next.captureHotkey = held(HotkeyManager::kCaptureId, previous.captureHotkey);
    next.boardHotkey = held(HotkeyManager::kBoardId, previous.boardHotkey);
    try {
        store_.saveSettings(next);
    } catch (const std::exception& error) {
        return ApplyError{QString::fromUtf8(error.what()), 0};
    }
    followUp(previous, next);
    return std::nullopt;
}

void SettingsApplier::followUp(const noite::Settings& previous, const noite::Settings& next)
{
    if (next.startWithWindows != previous.startWithWindows)
        applyAutostart_(next.startWithWindows);
    if (next.dayStart != previous.dayStart || next.staleDays != previous.staleDays)
        recheckReview_();
}
