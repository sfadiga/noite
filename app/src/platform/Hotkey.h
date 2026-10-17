#pragma once

#include <QString>

#include <optional>

// A global hotkey in Win32 terms: MOD_* flags (without MOD_NOREPEAT) and a virtual-key code.
struct Hotkey {
    unsigned modifiers{};
    unsigned vk{};
    bool operator==(const Hotkey&) const = default;
};

// Parses "Ctrl+Alt+J" style text (case-insensitive). Modifiers: Ctrl/Control, Alt, Shift,
// Win/Meta, each at most once; exactly one key: A-Z, 0-9, F1-F24 or Space. At least one of
// Ctrl, Alt or Win is required, so a hotkey can never swallow ordinary typing.
std::optional<Hotkey> parseHotkey(const QString& text);
