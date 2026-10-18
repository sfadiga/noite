#include "platform/HotkeyManager.h"

HotkeyManager::HotkeyManager(HotkeyBackend& backend)
    : backend_(backend)
{
}

void HotkeyManager::release(int id)
{
    Slot& s = slot(id);
    if (s.registered)
        backend_.unregisterHotkey(id);
    s.registered = false;
}

// Registers `combo` on `id` (which must be released); records it only when it succeeds.
bool HotkeyManager::registerSlot(int id, const QString& combo)
{
    const std::optional<Hotkey> hotkey = parseHotkey(combo);
    if (!hotkey || !backend_.registerHotkey(id, *hotkey))
        return false;
    Slot& s = slot(id);
    s.combo = formatHotkey(*hotkey);
    s.registered = true;
    return true;
}

HotkeyStatus HotkeyManager::set(int id, const QString& combo)
{
    const std::optional<Hotkey> hotkey = parseHotkey(combo);
    if (!hotkey)
        return HotkeyStatus::Invalid;
    const int otherId = id == kCaptureId ? kBoardId : kCaptureId;
    if (parseHotkey(slot(otherId).combo) == hotkey)
        return HotkeyStatus::SameAsOther;

    if (paused_) {
        // Nothing may be registered now: check the combination, keep it for resume().
        if (probe(combo) != HotkeyStatus::Ok)
            return HotkeyStatus::Taken;
        slot(id).combo = formatHotkey(*hotkey);
        return HotkeyStatus::Ok;
    }

    const QString old = slot(id).combo;
    const bool wasRegistered = slot(id).registered;
    release(id); // the same host cannot register the same id twice
    if (registerSlot(id, combo))
        return HotkeyStatus::Ok;
    if (wasRegistered)
        registerSlot(id, old);
    return HotkeyStatus::Taken;
}

HotkeyOutcome HotkeyManager::apply(const QString& capture, const QString& board)
{
    const std::optional<Hotkey> captureKey = parseHotkey(capture);
    const std::optional<Hotkey> boardKey = parseHotkey(board);
    if (!captureKey)
        return {HotkeyStatus::Invalid, kCaptureId};
    if (!boardKey)
        return {HotkeyStatus::Invalid, kBoardId};
    if (captureKey == boardKey)
        return {HotkeyStatus::SameAsOther, kBoardId};

    if (paused_) {
        for (const auto& [id, combo] : {std::pair{kCaptureId, capture}, std::pair{kBoardId, board}}) {
            if (probe(combo) != HotkeyStatus::Ok)
                return {HotkeyStatus::Taken, id};
        }
        capture_.combo = formatHotkey(*captureKey);
        board_.combo = formatHotkey(*boardKey);
        return {};
    }

    const QString oldCapture = capture_.combo;
    const QString oldBoard = board_.combo;
    const bool hadCapture = capture_.registered;
    const bool hadBoard = board_.registered;
    release(kCaptureId);
    release(kBoardId);

    int failed = 0;
    if (!registerSlot(kCaptureId, capture))
        failed = kCaptureId;
    else if (!registerSlot(kBoardId, board))
        failed = kBoardId;
    if (!failed)
        return {};

    release(kCaptureId);
    release(kBoardId);
    if (hadCapture)
        registerSlot(kCaptureId, oldCapture);
    if (hadBoard)
        registerSlot(kBoardId, oldBoard);
    return {HotkeyStatus::Taken, failed};
}

HotkeyStatus HotkeyManager::probe(const QString& combo)
{
    const std::optional<Hotkey> hotkey = parseHotkey(combo);
    if (!hotkey)
        return HotkeyStatus::Invalid;
    if (!backend_.registerHotkey(kProbeId, *hotkey))
        return HotkeyStatus::Taken;
    backend_.unregisterHotkey(kProbeId);
    return HotkeyStatus::Ok;
}

void HotkeyManager::pause()
{
    if (paused_)
        return;
    paused_ = true;
    release(kCaptureId);
    release(kBoardId);
}

void HotkeyManager::resume()
{
    if (!paused_)
        return;
    paused_ = false;
    for (const int id : {kCaptureId, kBoardId}) {
        if (!slot(id).combo.isEmpty())
            registerSlot(id, slot(id).combo);
    }
}

QString hotkeyMessage(HotkeyStatus status, const QString& combo, int otherId)
{
    switch (status) {
    case HotkeyStatus::Ok:
        return QStringLiteral("✓ ") + combo;
    case HotkeyStatus::Taken:
        return QStringLiteral("%1 is used by another program. Try another.").arg(combo);
    case HotkeyStatus::Invalid:
        return QStringLiteral("Use Ctrl or Alt with a key.");
    case HotkeyStatus::SameAsOther:
        return otherId == HotkeyManager::kBoardId ? QStringLiteral("That's already the board hotkey.")
                                                  : QStringLiteral("That's already the new-note hotkey.");
    }
    return {};
}
