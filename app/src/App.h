#pragma once

#include "AppOptions.h"
#include "board/BoardWindow.h"
#include "platform/Autostart.h"
#include "radar/ReviewWindow.h"

#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>
#include <optional>

namespace noite {
class Store;
}
class CaptureController;
class CaptureWindow;
class BackupRunner;
class JobScheduler;
class KeepWarm;
class LatencyProbe;
class NativeHost;
class QAction;
class QSystemTrayIcon;
class ReviewScheduler;
class StoreAttachments;

// The tray application: owns the data folder, the store, the native host, the capture window
// and, while it is open, the board (created on open, destroyed on close: B6).
class App : public QObject {
    Q_OBJECT
public:
    static constexpr int kCaptureHotkeyId = 1;
    static constexpr int kBoardHotkeyId = 2;

    // `autostart`: where "Start with Windows" is written (main passes the Run key). Without
    // one, autostart is not managed, so tests never touch the real registry.
    explicit App(AppOptions options, QObject* parent = nullptr, std::unique_ptr<AutostartRegistry> autostart = nullptr);
    ~App() override;

    // All startup. Returns an exit code only if the process should end right away:
    // 0 when another noite already runs on this data folder (it is asked to open a capture).
    std::optional<int> start();
    // start(), then the event loop.
    int run();

    noite::Store* store() const { return store_.get(); }
    NativeHost* host() const { return host_.get(); }
    CaptureWindow* captureWindow() const { return window_.get(); }
    // Opens the board (creating it, back on the tab and search it was closed with) and brings
    // it to the front.
    void showBoard();
    BoardWindow* boardWindow() const { return board_; } // null while closed
    // The morning review (M4): opened by the scheduler once a day, or from the tray.
    void showReview();
    ReviewWindow* reviewWindow() const { return review_; } // null while closed
    ReviewScheduler* scheduler() const { return scheduler_.get(); }
    QAction* autostartAction() const { return autostartAction_; }
    // Background jobs (M5): the trash purge and the backups.
    JobScheduler* jobs() const { return jobs_.get(); }
    QAction* backupAction() const { return backupAction_; }
    // Tray "Back up now". Without a backup folder it asks for one first (the Drive dialog).
    void backUpNow();
    // Replaces the folder dialog; tests only.
    void setBackupFolderAsker(std::function<std::optional<QString>()> asker) { askFolder_ = std::move(asker); }

signals:
    // A hotkey (capture or board) could not be registered (another program holds it). noite
    // keeps running; the tray still opens a capture and the board.
    void hotkeyUnavailable(QString combo);
    // A job result shown in the tray (there is no tray offscreen, so tests listen here).
    void toastShown(QString title, QString text);

private:
    void createTray();
    void registerHotkey(int id, const QString& combo);
    void updateAutostart(bool enabled);
    void startJobs();

    AppOptions options_;
    std::unique_ptr<noite::Store> store_;
    std::unique_ptr<NativeHost> host_;
    std::unique_ptr<StoreAttachments> attachments_;
    std::unique_ptr<CaptureController> controller_;
    std::unique_ptr<CaptureWindow> window_;
    std::unique_ptr<LatencyProbe> probe_;
    std::unique_ptr<KeepWarm> keepWarm_;
    QPointer<BoardWindow> board_; // deletes itself on close
    BoardWindow::ViewState boardState_;
    QSystemTrayIcon* tray_ = nullptr;
    std::unique_ptr<AutostartRegistry> autostart_;
    std::unique_ptr<ReviewScheduler> scheduler_;
    QPointer<ReviewWindow> review_; // deletes itself on close
    QAction* autostartAction_ = nullptr;
    QString dataRoot_;
    std::unique_ptr<BackupRunner> runner_;
    std::unique_ptr<JobScheduler> jobs_;
    QAction* backupAction_ = nullptr;
    std::function<std::optional<QString>()> askFolder_;
    bool logInstalled_ = false; // this App opened the log, so it closes it
};
