#pragma once

#include <QString>
#include <Qt>

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

// The canonical text of a hotkey: modifiers in the order Ctrl, Alt, Shift, Win, then the key
// ("Ctrl+Alt+J"). parseHotkey(formatHotkey(h)) == h for every parsed h.
QString formatHotkey(Hotkey hotkey);

// The parseHotkey text for a recorded key press ("Ctrl+Alt+J", modifier order Ctrl, Alt, Shift;
// Win is never produced), or nothing if the press is not a valid hotkey: a modifier key alone,
// a key parseHotkey does not know, or neither Ctrl nor Alt held.
std::optional<QString> comboFromKey(int qtKey, Qt::KeyboardModifiers mods);

// The same rules, from a Windows virtual-key code (QKeyEvent::nativeVirtualKey): independent of
// the keyboard layout, so Ctrl+Shift+7 is "Ctrl+Shift+7" even where Qt reports a symbol key.
std::optional<QString> comboFromNativeKey(quint32 vk, Qt::KeyboardModifiers mods);
