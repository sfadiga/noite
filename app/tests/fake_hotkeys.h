#pragma once

#include "platform/HotkeyManager.h"

#include <QMap>
#include <QSet>
#include <QString>

// Records registrations; `taken` holds combinations "another program" owns.
struct FakeBackend : HotkeyBackend {
    QSet<QString> taken; // normalised: "Ctrl+Alt+K"
    QMap<int, Hotkey> registered;

    bool registerHotkey(int id, Hotkey hotkey) override
    {
        for (const QString& combo : std::as_const(taken)) {
            if (parseHotkey(combo) == hotkey)
                return false;
        }
        for (const Hotkey& held : std::as_const(registered)) {
            if (held == hotkey)
                return false; // the OS refuses a combination this window already holds
        }
        if (registered.contains(id))
            return false; // like Win32: an id registers once per window
        registered.insert(id, hotkey);
        return true;
    }
    void unregisterHotkey(int id) override { registered.remove(id); }

    bool has(int id, const QString& combo) const { return registered.value(id) == parseHotkey(combo); }
};
