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

QString formatHotkey(Hotkey hotkey)
{
    QString text;
    if (hotkey.modifiers & MOD_CONTROL)
        text += QStringLiteral("Ctrl+");
    if (hotkey.modifiers & MOD_ALT)
        text += QStringLiteral("Alt+");
    if (hotkey.modifiers & MOD_SHIFT)
        text += QStringLiteral("Shift+");
    if (hotkey.modifiers & MOD_WIN)
        text += QStringLiteral("Win+");
    if (hotkey.vk == VK_SPACE)
        return text + QStringLiteral("Space");
    if (hotkey.vk >= VK_F1 && hotkey.vk <= VK_F24)
        return text + QStringLiteral("F%1").arg(hotkey.vk - VK_F1 + 1);
    return text + QChar(char16_t(hotkey.vk));
}

namespace {

bool isHotkeyVk(unsigned vk)
{
    return (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') || (vk >= VK_F1 && vk <= VK_F24) || vk == VK_SPACE;
}

std::optional<QString> comboFromVk(unsigned vk, Qt::KeyboardModifiers mods)
{
    if (!isHotkeyVk(vk))
        return std::nullopt;
    Hotkey hotkey;
    hotkey.vk = vk;
    if (!(mods & (Qt::ControlModifier | Qt::AltModifier)))
        return std::nullopt;
    if (mods & Qt::ControlModifier)
        hotkey.modifiers |= MOD_CONTROL;
    if (mods & Qt::AltModifier)
        hotkey.modifiers |= MOD_ALT;
    if (mods & Qt::ShiftModifier)
        hotkey.modifiers |= MOD_SHIFT;
    return formatHotkey(hotkey);
}

} // namespace

std::optional<QString> comboFromKey(int qtKey, Qt::KeyboardModifiers mods)
{
    unsigned vk = 0;
    if ((qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) || (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9))
        vk = unsigned(qtKey); // Qt's A-Z and 0-9 codes are the ASCII (and VK) codes
    else if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24)
        vk = unsigned(VK_F1 + qtKey - Qt::Key_F1);
    else if (qtKey == Qt::Key_Space)
        vk = VK_SPACE;
    else
        return std::nullopt;
    return comboFromVk(vk, mods);
}

std::optional<QString> comboFromNativeKey(quint32 vk, Qt::KeyboardModifiers mods)
{
    return comboFromVk(vk, mods);
}
