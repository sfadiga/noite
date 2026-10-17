#pragma once

#include "AppOptions.h"

#include <QObject>

#include <memory>
#include <optional>

namespace noite {
class Store;
}
class CaptureController;
class CaptureWindow;
class NativeHost;
class QSystemTrayIcon;
class StoreAttachments;

// The tray application: owns the data folder, the store, the native host and the capture window.
class App : public QObject {
    Q_OBJECT
public:
    explicit App(AppOptions options, QObject* parent = nullptr);
    ~App() override;

    // All startup. Returns an exit code only if the process should end right away:
    // 0 when another noite already runs on this data folder (it is asked to open a capture).
    std::optional<int> start();
    // start(), then the event loop.
    int run();

    noite::Store* store() const { return store_.get(); }
    NativeHost* host() const { return host_.get(); }
    CaptureWindow* captureWindow() const { return window_.get(); }

signals:
    // The capture hotkey could not be registered (another program holds it). noite keeps
    // running; the tray's "New note" still opens a capture.
    void hotkeyUnavailable(QString combo);

private:
    void createTray();

    AppOptions options_;
    std::unique_ptr<noite::Store> store_;
    std::unique_ptr<NativeHost> host_;
    std::unique_ptr<StoreAttachments> attachments_;
    std::unique_ptr<CaptureController> controller_;
    std::unique_ptr<CaptureWindow> window_;
    QSystemTrayIcon* tray_ = nullptr;
};
