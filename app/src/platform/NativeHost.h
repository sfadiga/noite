#pragma once

#include "platform/Hotkey.h"

#include <QObject>
#include <QSet>
#include <QString>

// Stable per-data-folder key: first 16 hex chars of SHA-1 of the cleaned, lowercased path.
// Two noite instances on the same data folder share it (D27).
QString instanceKeyFor(const QString& dataRoot);

// A Win32 message-only window. Global hotkeys are registered against it (not as thread
// messages), so they still arrive while a modal loop such as a drag or a dialog is
// running. A second noite launch finds it by instance key and asks it to open a capture.
class NativeHost : public QObject {
    Q_OBJECT
public:
    explicit NativeHost(const QString& instanceKey, QObject* parent = nullptr);
    ~NativeHost() override; // unregisters every hotkey and destroys the window

    quintptr nativeHandle() const { return hwnd_; }

    // False if another program (or another host) already holds the combination.
    bool registerHotkey(int id, Hotkey hotkey);
    void unregisterHotkey(int id);

    // Posts an activation request to the instance owning `instanceKey`; false if none runs.
    static bool activateRunningInstance(const QString& instanceKey);

    // False between a session lock and the next unlock; true at start (we run in the session).
    bool isUnlocked() const { return unlocked_; }

signals:
    void hotkeyPressed(int id);
    void activationRequested();
    // Session and power events (M4: the morning review checks again on unlock and resume).
    void sessionLocked();
    void sessionUnlocked();
    void resumed();

private:
    friend struct NativeHostAccess;

    quintptr hwnd_ = 0;
    QSet<int> hotkeys_;
    bool unlocked_ = true;
};
