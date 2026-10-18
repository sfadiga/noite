#include "App.h"

#include "capture/CaptureController.h"
#include "capture/CaptureWindow.h"
#include "capture/KeepWarm.h"
#include "capture/LatencyProbe.h"
#include "editor/AttachmentStore.h"
#include "editor/NoteEdit.h"
#include "editor/OpenFolder.h"
#include "jobs/BackupFolderDialog.h"
#include "jobs/JobScheduler.h"
#include "log/Log.h"
#include "platform/CrashHandler.h"
#include "platform/Hotkey.h"
#include "platform/NativeHost.h"
#include "radar/ReviewScheduler.h"

#include <noite/data_dir.h>
#include <noite/error.h>
#include <noite/store.h>

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QMenu>
#include <QStyle>
#include <QSysInfo>
#include <QSystemTrayIcon>
#include <QTimer>

#include <windows.h>

App::App(AppOptions options, QObject* parent, std::unique_ptr<AutostartRegistry> autostart)
    : QObject(parent)
    , options_(std::move(options))
    , autostart_(std::move(autostart))
    , askFolder_([] { return noite::askBackupFolder(); })
{
}

App::~App()
{
    if (runner_)
        runner_->cancelAndWait(); // before the windows and the store the backup reads
    if (review_) {
        review_->close(); // saves a note being edited in it
        delete review_;   // before the store it uses
    }
    if (board_) {
        board_->close(); // saves a note open in its overlay
        delete board_;   // before the store it uses
    }
    if (controller_)
        controller_->flush();
}

App::LogSession::~LogSession()
{
    if (!open)
        return;
    qInfo() << "noite quit";
    applog::uninstall();
}

std::optional<int> App::start()
{
    const QString root = resolveDataDir(options_, QCoreApplication::applicationDirPath());
    std::optional<noite::DataDir> dir;
    try {
        dir.emplace(noite::DataDir::acquire(root));
    } catch (const noite::Error& e) {
        if (e.code() == noite::ErrorCode::AlreadyRunning) {
            NativeHost::activateRunningInstance(instanceKeyFor(root));
            return 0;
        }
        qCritical() << "cannot use data folder" << root << ":" << e.what();
        return 1;
    }

    // The log only opens once the folder lock is ours: a second instance that just activates
    // the first must never write into its log.
    applog::install(dir->logsDir());
    log_.open = true;
    if (options_.crashHandler)
        crash::install(dir->logsDir());
    qInfo().noquote() << QString::fromUtf8("noite started · data %1 · Qt %2 · Windows %3")
                             .arg(root, QString::fromLatin1(qVersion()), QSysInfo::kernelVersion());

    store_ = noite::Store::open(std::move(*dir), std::make_shared<noite::SystemClock>());
    clearOpenFolder(defaultOpenFolder()); // chip files opened last session (A5)
    host_ = std::make_unique<NativeHost>(instanceKeyFor(root));

    auto* editor = new NoteEdit; // owned by the window
    attachments_ = std::make_unique<StoreAttachments>(*store_);
    editor->setAttachmentStore(attachments_.get());
    controller_ = std::make_unique<CaptureController>(*store_, *editor, [] { return QDate::currentDate(); });
    window_ = std::make_unique<CaptureWindow>(*controller_, editor);
    window_->winId(); // create the native window now, while hidden (D5)
    probe_ = LatencyProbe::fromEnvironment();
    window_->setLatencyProbe(probe_.get());
    if (KeepWarm::enabledByEnvironment()) {
        keepWarm_ = std::make_unique<KeepWarm>(window_.get());
        keepWarm_->start();
    }

    connect(host_.get(), &NativeHost::hotkeyPressed, this, [this](int id) {
        if (id == kBoardHotkeyId) {
            showBoard();
            return;
        }
        if (id != kCaptureHotkeyId)
            return;
        if (probe_)
            probe_->pressed();
        window_->summon();
    });
    connect(host_.get(), &NativeHost::activationRequested, window_.get(), &CaptureWindow::summon);
    connect(qApp, &QCoreApplication::aboutToQuit, controller_.get(), &CaptureController::flush);
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (board_)
            board_->close();
    });
    // A note captured while the board is open shows up on it.
    const auto refreshBoard = [this] {
        if (board_)
            board_->reload();
    };
    connect(controller_.get(), &CaptureController::saved, this, refreshBoard);
    connect(controller_.get(), &CaptureController::finished, this, refreshBoard);

    const noite::Settings settings = store_->settings();
    dataRoot_ = root;
    autostartAction_ = new QAction(tr("Start with Windows"), this);
    autostartAction_->setCheckable(true);
    autostartAction_->setChecked(settings.startWithWindows);
    // Only the portable folder's own data manages Start with Windows: a run on another
    // folder (--data, e.g. a test folder) never re-points or removes the real entry.
    autostartAction_->setEnabled(autostart_ && !options_.dataFromCommandLine);
    connect(autostartAction_, &QAction::toggled, this, [this](bool enabled) {
        try {
            noite::Settings s = store_->settings();
            s.startWithWindows = enabled;
            store_->saveSettings(s);
        } catch (const std::exception& error) {
            qWarning() << "autostart setting:" << error.what();
        }
        updateAutostart(enabled);
    });
    updateAutostart(settings.startWithWindows);

    // The morning review (M4): checked now, on unlock, on resume and at the next day start.
    scheduler_ = std::make_unique<ReviewScheduler>(
        *store_, [] { return QDateTime::currentDateTime(); }, [this] { return host_->isUnlocked(); });
    connect(host_.get(), &NativeHost::sessionUnlocked, scheduler_.get(), &ReviewScheduler::check);
    connect(host_.get(), &NativeHost::resumed, scheduler_.get(), &ReviewScheduler::check);
    connect(scheduler_.get(), &ReviewScheduler::reviewDue, this, &App::showReview);
    QTimer::singleShot(0, scheduler_.get(), &ReviewScheduler::check);

    startJobs();
    createTray();

    hotkeys_ = std::make_unique<HotkeyManager>(*host_);
    if (hotkeys_->set(kCaptureHotkeyId, settings.captureHotkey) != HotkeyStatus::Ok)
        emit hotkeyUnavailable(settings.captureHotkey);
    if (hotkeys_->set(kBoardHotkeyId, settings.boardHotkey) != HotkeyStatus::Ok)
        emit hotkeyUnavailable(settings.boardHotkey);
    return std::nullopt;
}

void App::startJobs()
{
    runner_ = std::make_unique<ThreadBackupRunner>();
    jobs_ = std::make_unique<JobScheduler>(*store_, [] { return QDateTime::currentDateTime(); }, *runner_);
    connect(host_.get(), &NativeHost::resumed, jobs_.get(), &JobScheduler::check);
    connect(jobs_.get(), &JobScheduler::purged, this, [this] {
        if (board_)
            board_->reload();
    });
    connect(jobs_.get(), &JobScheduler::toast, this, [this](const QString& title, const QString& text) {
        if (tray_)
            tray_->showMessage(title, text);
        else
            qInfo().noquote() << "toast:" << text; // no tray: the log is the only trace
        emit toastShown(title, text);
    });

    // Like autostartAction_, made here: the tray itself may not exist (tests run offscreen).
    backupAction_ = new QAction(tr("Back up now"), this);
    connect(backupAction_, &QAction::triggered, this, &App::backUpNow);
    connect(jobs_.get(), &JobScheduler::busyChanged, this, [this](bool busy) {
        backupAction_->setText(busy ? tr("Backing up…") : tr("Back up now"));
        backupAction_->setEnabled(!busy);
    });
    jobs_->startTimers();
}

void App::backUpNow()
{
    if (!jobs_)
        return;
    try {
        noite::Settings settings = store_->settings();
        if (!settings.backupDir) {
            // The dialog is modal but the tray menu is not: a second click must not stack another.
            backupAction_->setEnabled(false);
            const std::optional<QString> folder = askFolder_();
            backupAction_->setEnabled(!jobs_->isBusy());
            if (!folder)
                return;
            settings.backupDir = *folder;
            store_->saveSettings(settings);
        }
    } catch (const std::exception& error) {
        qWarning() << "backup folder setting:" << error.what();
        const QString text = tr("Backup failed: %1").arg(QString::fromUtf8(error.what()));
        if (tray_)
            tray_->showMessage(QStringLiteral("noite"), text);
        emit toastShown(QStringLiteral("noite"), text);
        return;
    }
    jobs_->backUpNow(); // a folder that is gone is reported by its failure toast
}

void App::updateAutostart(bool enabled)
{
    if (!autostart_ || options_.dataFromCommandLine)
        return; // not managed: no registry given (tests), or another data folder
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString command = autostartCommand(QCoreApplication::applicationFilePath(), dataRoot_, exeDir + "/data");
    if (!applyAutostart(*autostart_, enabled, command))
        qWarning() << "autostart: the Run key could not be updated";
}

void App::showReview()
{
    if (!review_) {
        review_ = new ReviewWindow(*store_, *attachments_, [] { return QDate::currentDate(); });
        review_->setAttribute(Qt::WA_DeleteOnClose);
        connect(review_, &ReviewWindow::openBoardRequested, this, [this] {
            if (review_)
                review_->close();
            showBoard();
        });
        review_->start();
    } else if (!review_->isEditing()) {
        review_->start(); // a window left open (since yesterday, say) gets today's queue
    }
    review_->show();
    review_->raise();
    review_->activateWindow();
    SetForegroundWindow(reinterpret_cast<HWND>(review_->winId()));
}

void App::showBoard()
{
    if (!board_) {
        board_ = new BoardWindow(*store_, *attachments_, [] { return QDate::currentDate(); });
        board_->setAttribute(Qt::WA_DeleteOnClose);
        board_->restoreViewState(boardState_);
        connect(board_, &BoardWindow::closing, this, [this](const BoardWindow::ViewState& state) { boardState_ = state; });
    }
    board_->show();
    board_->raise();
    board_->activateWindow();
    SetForegroundWindow(reinterpret_cast<HWND>(board_->winId()));
}

int App::run()
{
    if (const std::optional<int> code = start())
        return *code;
    return QApplication::exec();
}

void App::createTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    tray_ = new QSystemTrayIcon(qApp->style()->standardIcon(QStyle::SP_FileDialogDetailedView), this);
    tray_->setToolTip("noite");
    auto* menu = new QMenu;
    menu->addAction(tr("New note"), window_.get(), &CaptureWindow::summon);
    menu->addAction(tr("Board"), this, &App::showBoard);
    menu->addAction(tr("Morning review"), this, &App::showReview);
    menu->addSeparator();
    menu->addAction(autostartAction_);
    menu->addSeparator();
    menu->addAction(backupAction_);
    menu->addSeparator();
    menu->addAction(tr("Quit"), qApp, &QCoreApplication::quit);
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            showBoard(); // B5
    });
    connect(this, &App::hotkeyUnavailable, tray_, [this](const QString& combo) {
        tray_->showMessage("noite", tr("%1 is already used by another program. Use the tray icon instead.").arg(combo),
                           QSystemTrayIcon::Warning);
    });
    tray_->show();
}
