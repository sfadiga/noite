#include "HotkeyFilter.h"

#include <windows.h>

namespace {
constexpr int kHotkeyId = 1;
}

HotkeyFilter::~HotkeyFilter()
{
    if (registered_)
        UnregisterHotKey(nullptr, kHotkeyId);
}

bool HotkeyFilter::registerHotkey(unsigned mods, unsigned vk)
{
    registered_ = RegisterHotKey(nullptr, kHotkeyId, mods | MOD_NOREPEAT, vk);
    return registered_;
}

bool HotkeyFilter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*)
{
    if (eventType != "windows_generic_MSG")
        return false;
    const auto* msg = static_cast<const MSG*>(message);
    if (msg->message != WM_HOTKEY || msg->wParam != kHotkeyId)
        return false;
    emit pressed();
    return true;
}
