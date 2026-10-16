#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>

// Global hotkey via RegisterHotKey on the GUI thread; WM_HOTKEY arrives as a
// thread message and is picked up here.
class HotkeyFilter : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    ~HotkeyFilter() override;

    bool registerHotkey(unsigned mods, unsigned vk);
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void pressed();

private:
    bool registered_ = false;
};
