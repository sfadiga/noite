#include "platform/Hotkey.h"

#include <QStringList>

#include <windows.h>

namespace {

std::optional<unsigned> modifierFor(const QString& token)
{
    if (token == "ctrl" || token == "control")
        return MOD_CONTROL;
    if (token == "alt")
        return MOD_ALT;
    if (token == "shift")
        return MOD_SHIFT;
    if (token == "win" || token == "meta")
        return MOD_WIN;
    return std::nullopt;
}

std::optional<unsigned> keyFor(const QString& token)
{
    if (token == "space")
        return VK_SPACE;
    if (token.size() == 1) {
        const QChar c = token[0];
        if (c >= 'a' && c <= 'z')
            return unsigned(c.toUpper().unicode());
        if (c >= '0' && c <= '9')
            return unsigned(c.unicode());
        return std::nullopt;
    }
    if (token.startsWith('f')) {
        bool ok = false;
        const int n = token.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 24)
            return unsigned(VK_F1 + n - 1);
    }
    return std::nullopt;
}

} // namespace

std::optional<Hotkey> parseHotkey(const QString& text)
{
    Hotkey hotkey;
    bool haveKey = false;
    for (const QString& raw : text.split('+')) {
        const QString token = raw.trimmed().toLower();
        if (token.isEmpty())
            return std::nullopt;
        if (const auto mod = modifierFor(token)) {
            if (hotkey.modifiers & *mod)
                return std::nullopt;
            hotkey.modifiers |= *mod;
        } else if (const auto key = keyFor(token); key && !haveKey) {
            hotkey.vk = *key;
            haveKey = true;
        } else {
            return std::nullopt;
        }
    }
    if (!haveKey || !(hotkey.modifiers & (MOD_CONTROL | MOD_ALT | MOD_WIN)))
        return std::nullopt;
    return hotkey;
}
