#pragma once

#include "AppOptions.h"
#include "board/BoardWindow.h"
#include "platform/Autostart.h"
#include "platform/HotkeyManager.h"
#include "radar/ReviewWindow.h"
#include "setup/SettingsDialog.h"
#include "setup/SetupWizard.h"

#include <noite/data_dir.h>

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
class QMenu;
class QSystemTrayIcon;
class ReviewScheduler;
class SettingsApplier;
class StoreAttachments;

// The tray application: owns the data folder, the store, the native host, the capture window
// and, while it is open, the board (created on open, destroyed on close: B6).
class App : public QObject {
    Q_OBJECT
public:
    static constexpr int kCaptureHotkeyId = HotkeyManager::kCaptureId;
    static constexpr int kBoardHotkeyId = HotkeyManager::kBoardId;

    // `autostart`: where "Start with Windows" is written (main passes the Run key). Without
    // one, autostart is not managed, so tests never touch the real registry. `hotkeyBackend`
    // overrides `host_` for hotkey registration in tests.
    explicit App(AppOptions options, QObject* parent = nullptr, std::unique_ptr<AutostartRegistry> autostart = nullptr,
                 HotkeyBackend* hotkeyBackend = nullptr);
    ~App() override;

    // All startup. Returns an exit code only if the process should end right away:
    // 0 when another noite already runs on this data folder (it is asked to open a capture).
    std::optional<int> start();
    // start(), then the event loop.
    int run();

    noite::Store* store() const { return store_.get(); }
    NativeHost* host() const { return host_.get(); }
    // The two global hotkeys; null before start().
    HotkeyManager* hotkeys() const { return hotkeys_.get(); }
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
    // First-run setup wizard and Settings window (M6).
    void showWizard();
    SetupWizard* wizard() const { return wizard_; } // null while closed
    void showSettings();
    SettingsDialog* settingsDialog() const { return settingsDialog_; } // null while closed
    QAction* settingsAction() const { return settingsAction_; }
    bool setupPending() const { return setupPending_; }
    QMenu* trayMenu() const { return trayMenu_.get(); }
    // The tray icon's tooltip: "noite <version>" (available without a tray).
    QString trayToolTip() const;

signals:
    // A hotkey (capture or board) could not be registered (another program holds it). noite
    // keeps running; the tray still opens a capture and the board.
    void hotkeyUnavailable(QString combo);
    // A job result shown in the tray (there is no tray offscreen, so tests listen here).
    void toastShown(QString title, QString text);

private:
    void createTray();
    // A "noite" message in the tray, and toastShown.
    void toast(const QString& text);
    void updateAutostart(bool enabled);
    // A saved Start with Windows: the tray action follows (without saving again), then the Run key.
    void syncAutostart(bool enabled);
    void startJobs();
    // While it is open (S6) the review waits and the job checks are skipped; both run on close.
    bool wizardOpen() const { return setupPending_ || wizard_; }
    // A review coming due now is held: a wizard is open, or the Try-it capture it opened.
    bool reviewWaits() const { return wizardOpen() || captureHoldsReview_; }
    void raiseWizard();
    // After the wizard's settings are applied: a slot left empty gets the stored combination. A
    // combination that fails and equals `reported` (already toasted) is not reported again.
    void registerMissingHotkeys(const QString& reported = {});
    // The wizard finished or was abandoned: ends a pending first run and releases what it held.
    // `captureOpen`: its Try-it hotkey left a capture open; the review waits for it to finish.
    void wizardClosed(bool captureOpen = false);
    // Once nothing holds it any more: the first run's review check and a held review.
    void releaseReview();
    // The board or the review is about to change note `id`: an open capture of it saves and closes.
    void closeCaptureHolding(const noite::ScratchId& id);

    // Closes the log (after "noite quit") once every member below is gone, the store included.
    // applog's install/uninstall have a single owner: Apps may only overlap if the later one
    // never gets the data folder lock (it then never opens the log), as in tests.
    struct LogSession {
        bool open = false;
        ~LogSession();
    };

    AppOptions options_;
    LogSession log_;
    std::unique_ptr<noite::Store> store_;
    std::unique_ptr<NativeHost> host_;
    HotkeyBackend* hotkeyBackend_ = nullptr;
    std::unique_ptr<HotkeyManager> hotkeys_;
    std::unique_ptr<StoreAttachments> attachments_;
    std::unique_ptr<CaptureController> controller_;
    std::unique_ptr<CaptureWindow> window_;
    std::unique_ptr<LatencyProbe> probe_;
    std::unique_ptr<KeepWarm> keepWarm_;
    QPointer<BoardWindow> board_; // deletes itself on close
    BoardWindow::ViewState boardState_;
    std::unique_ptr<QMenu> trayMenu_;
    QSystemTrayIcon* tray_ = nullptr;
    std::unique_ptr<AutostartRegistry> autostart_;
    std::unique_ptr<ReviewScheduler> scheduler_;
    QPointer<ReviewWindow> review_; // deletes itself on close
    QAction* autostartAction_ = nullptr;
    QString dataRoot_;
    std::optional<noite::SyncProvider> syncWarning_;
    QString logsDir_;
    std::unique_ptr<BackupRunner> runner_;
    std::unique_ptr<JobScheduler> jobs_;
    QAction* backupAction_ = nullptr;
    std::function<std::optional<QString>()> askFolder_;
    std::unique_ptr<SettingsApplier> applier_;
    QPointer<SetupWizard> wizard_;
    QPointer<SettingsDialog> settingsDialog_;
    QAction* settingsAction_ = nullptr;
    bool setupPending_ = false;
    bool reviewHeld_ = false;         // a review came due while the wizard (or its capture) was open
    bool checkPending_ = false;       // the first run's review check, waiting for the Try-it capture
    bool captureHoldsReview_ = false; // the Try-it capture is open (S2: the review would hide it)
};
