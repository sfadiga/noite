#include "platform/NativeHost.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include <windows.h>
#include <wtsapi32.h>

namespace {

constexpr wchar_t kWindowClass[] = L"NoiteHostWindow";

UINT activateMessage()
{
    static const UINT message = RegisterWindowMessageW(L"noite.activate");
    return message;
}

std::wstring windowTitle(const QString& instanceKey)
{
    return (QStringLiteral("noite-host-") + instanceKey).toStdWString();
}

} // namespace

struct NativeHostAccess {
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* host = reinterpret_cast<NativeHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (host && message == WM_HOTKEY) {
            emit host->hotkeyPressed(int(wParam));
            return 0;
        }
        if (host && message == activateMessage()) {
            emit host->activationRequested();
            return 0;
        }
        if (host && message == WM_WTSSESSION_CHANGE) {
            if (wParam == WTS_SESSION_LOCK) {
                host->unlocked_ = false;
                emit host->sessionLocked();
            } else if (wParam == WTS_SESSION_UNLOCK) {
                host->unlocked_ = true;
                emit host->sessionUnlocked();
            }
            return 0;
        }
        if (host && message == WM_POWERBROADCAST && wParam == PBT_APMRESUMEAUTOMATIC) {
            emit host->resumed();
            return TRUE;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
};

QString instanceKeyFor(const QString& dataRoot)
{
    const QString path = QDir::cleanPath(QFileInfo(QDir::fromNativeSeparators(dataRoot)).absoluteFilePath()).toLower();
    return QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

NativeHost::NativeHost(const QString& instanceKey, QObject* parent)
    : QObject(parent)
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &NativeHostAccess::wndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc); // fails harmlessly with ERROR_CLASS_ALREADY_EXISTS after the first host

    const std::wstring title = windowTitle(instanceKey);
    HWND hwnd = CreateWindowExW(0, kWindowClass, title.c_str(), 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (hwnd) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        // Lock/unlock arrive as WM_WTSSESSION_CHANGE (the morning review waits for an unlock).
        if (!WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION))
            qWarning() << "native host: no session notifications:" << GetLastError();
    }
    hwnd_ = reinterpret_cast<quintptr>(hwnd);
}

NativeHost::~NativeHost()
{
    const auto hwnd = reinterpret_cast<HWND>(hwnd_);
    for (int id : std::as_const(hotkeys_))
        UnregisterHotKey(hwnd, id);
    if (hwnd) {
        WTSUnRegisterSessionNotification(hwnd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        DestroyWindow(hwnd);
    }
}

bool NativeHost::registerHotkey(int id, Hotkey hotkey)
{
    if (!RegisterHotKey(reinterpret_cast<HWND>(hwnd_), id, hotkey.modifiers | MOD_NOREPEAT, hotkey.vk))
        return false;
    hotkeys_.insert(id);
    return true;
}

void NativeHost::unregisterHotkey(int id)
{
    if (hotkeys_.remove(id))
        UnregisterHotKey(reinterpret_cast<HWND>(hwnd_), id);
}

bool NativeHost::activateRunningInstance(const QString& instanceKey)
{
    const std::wstring title = windowTitle(instanceKey);
    HWND target = FindWindowExW(HWND_MESSAGE, nullptr, kWindowClass, title.c_str());
    if (!target)
        return false;
    // This (just launched) process may own the foreground; let the running one take it.
    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);
    AllowSetForegroundWindow(pid);
    return PostMessageW(target, activateMessage(), 0, 0) != 0;
}
