#include "App.h"

#include "capture/CaptureController.h"
#include "capture/CaptureWindow.h"
#include "capture/KeepWarm.h"
#include "capture/LatencyProbe.h"
#include "capture/Prewarm.h"
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
#include "setup/BackupFolderField.h"
#include "setup/SettingsApplier.h"
#include "setup/SetupTexts.h"

#include <noite/data_dir.h>
#include <noite/error.h>
#include <noite/store.h>
#include <noite_version.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDebug>
#include <QMenu>
#include <QSignalBlocker>
#include <QStyle>
#include <QSysInfo>
#include <QSystemTrayIcon>
#include <QTimer>

#include <utility>

#include <windows.h>

App::App(AppOptions options, QObject* parent, std::unique_ptr<AutostartRegistry> autostart,
         HotkeyBackend* hotkeyBackend)
    : QObject(parent)
    , options_(std::move(options))
    , hotkeyBackend_(hotkeyBackend)
    , autostart_(std::move(autostart))
    , askFolder_([] { return noite::askBackupFolder(); })
{
}

App::~App()
{
    if (runner_)
        runner_->cancelAndWait(); // before the windows and the store the backup reads
    if (wizard_) {
        wizard_->disconnect(this);
        wizard_->close();
        delete wizard_;
    }
    if (settingsDialog_) {
        settingsDialog_->disconnect(this);
        settingsDialog_->close();
        delete settingsDialog_;
    }
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
    delete tray_;
    tray_ = nullptr;
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

    syncWarning_ = dir->syncWarning();
    logsDir_ = dir->logsDir();

    // The log only opens once the folder lock is ours: a second instance that just activates
    // the first must never write into its log.
    applog::install(logsDir_);
    log_.open = true;
    if (options_.crashHandler)
        crash::install(logsDir_);
    qInfo().noquote() << QString::fromUtf8("noite %1 started · data %2 · Qt %3 · Windows %4")
                             .arg(QString::fromLatin1(NOITE_VERSION), root, QString::fromLatin1(qVersion()),
                                  QSysInfo::kernelVersion());

    store_ = noite::Store::open(std::move(*dir), std::make_shared<noite::SystemClock>());
    clearOpenFolder(defaultOpenFolder()); // chip files opened last session (A5)
    host_ = std::make_unique<NativeHost>(instanceKeyFor(root));
    hotkeys_ = std::make_unique<HotkeyManager>(hotkeyBackend_ ? *hotkeyBackend_ : *host_);

    auto* editor = new NoteEdit; // owned by the window
    attachments_ = std::make_unique<StoreAttachments>(*store_);
    editor->setAttachmentStore(attachments_.get());
    controller_ = std::make_unique<CaptureController>(*store_, *editor, [] { return QDate::currentDate(); });
    window_ = std::make_unique<CaptureWindow>(*controller_, editor);
    if (const std::optional<QString> error = prewarmCapture(window_.get(), {}))
        qWarning() << "prewarm:" << *error;
    probe_ = LatencyProbe::fromEnvironment();
    window_->setLatencyProbe(probe_.get());
    if (KeepWarm::enabledByEnvironment()) {
        keepWarm_ = std::make_unique<KeepWarm>(window_.get());
        keepWarm_->start();
    }

    connect(host_.get(), &NativeHost::hotkeyPressed, this, [this](int id) {
        if (wizard_) {
            if (id == kCaptureHotkeyId)
                wizard_->captureHotkeyPressed();
            return;
        }
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
    connect(host_.get(), &NativeHost::activationRequested, this, [this] {
        if (wizard_)
            raiseWizard(); // a second start while setting up: back to the wizard
        else
            window_->summon();
    });
    connect(qApp, &QCoreApplication::aboutToQuit, controller_.get(), &CaptureController::flush);
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (board_)
            board_->close();
        if (wizard_) {
            wizard_->disconnect(this); // quitting discards the wizard's partial settings
            wizard_->close();
        }
        if (settingsDialog_) {
            settingsDialog_->disconnect(this);
            settingsDialog_->close();
        }
    });
    // A note captured while the board is open shows up on it.
    const auto refreshBoard = [this] {
        if (board_)
            board_->reload();
    };
    connect(controller_.get(), &CaptureController::saved, this, refreshBoard);
    connect(controller_.get(), &CaptureController::finished, this, refreshBoard);

    const noite::Settings settings = store_->settings();
    setupPending_ = !settings.setupCompleted;
    dataRoot_ = root;
    autostartAction_ = new QAction(tr("Start with Windows"), this);
    autostartAction_->setCheckable(true);
    autostartAction_->setChecked(settings.startWithWindows);
    // Only the portable folder's own data manages Start with Windows: a run on another
    // folder (--data, e.g. a test folder) never re-points or removes the real entry.
    autostartAction_->setEnabled(autostart_ && !options_.dataFromCommandLine);
    // The tray's own toggle; a change saved elsewhere goes through syncAutostart.
    connect(autostartAction_, &QAction::toggled, this, [this](bool enabled) {
        try {
            noite::Settings s = store_->settings();
            s.startWithWindows = enabled;
            store_->saveSettings(s);
        } catch (const std::exception& error) {
            qWarning() << "autostart setting:" << error.what();
        }
        updateAutostart(enabled);
        if (settingsDialog_) // its Save must not undo the toggle
            settingsDialog_->startWithWindowsCheck()->setChecked(enabled);
    });
    updateAutostart(settings.startWithWindows);

    // The morning review (M4): checked now, on unlock, on resume and at the next day start. A
    // review due while a re-run wizard is open waits for it to close (S6): the check has already
    // counted the day. The first run checks only once it closes, so quitting mid-setup keeps
    // today's review (R7).
    scheduler_ = std::make_unique<ReviewScheduler>(
        *store_, [] { return QDateTime::currentDateTime(); }, [this] { return host_->isUnlocked(); });
    const auto checkReview = [this] {
        if (!setupPending_)
            scheduler_->check();
    };
    connect(host_.get(), &NativeHost::sessionUnlocked, this, checkReview);
    connect(host_.get(), &NativeHost::resumed, this, checkReview);
    connect(scheduler_.get(), &ReviewScheduler::reviewDue, this, [this] {
        if (reviewWaits())
            reviewHeld_ = true;
        else
            showReview();
    });

    applier_ = std::make_unique<SettingsApplier>(
        *store_, *hotkeys_, [this](bool enabled) { syncAutostart(enabled); }, checkReview);

    startJobs();
    createTray();

    if (settings.setupCompleted) {
        if (hotkeys_->set(kCaptureHotkeyId, settings.captureHotkey) != HotkeyStatus::Ok)
            emit hotkeyUnavailable(settings.captureHotkey);
        if (hotkeys_->set(kBoardHotkeyId, settings.boardHotkey) != HotkeyStatus::Ok)
            emit hotkeyUnavailable(settings.boardHotkey);
        QTimer::singleShot(0, scheduler_.get(), &ReviewScheduler::check);
    } else {
        showWizard();
    }
    return std::nullopt;
}

void App::wizardClosed(bool captureOpen)
{
    if (wizard_) {
        wizard_->deleteLater();
        wizard_ = nullptr;
    }
    if (setupPending_) {
        setupPending_ = false;
        jobs_->startTimers();
        checkPending_ = true;
    }
    jobs_->setHeld(false); // runs a check skipped while the wizard was open
    // The review would take the focus from the Try-it capture (S2).
    if (captureOpen && !captureHoldsReview_) {
        captureHoldsReview_ = true;
        connect(
            controller_.get(), &CaptureController::finished, this,
            [this] {
                captureHoldsReview_ = false;
                releaseReview();
            },
            Qt::SingleShotConnection);
    }
    releaseReview();
}

void App::releaseReview()
{
    if (reviewWaits())
        return; // the next wizardClosed() or the capture's finish comes back here
    if (std::exchange(checkPending_, false))
        QTimer::singleShot(0, scheduler_.get(), &ReviewScheduler::check);
    if (std::exchange(reviewHeld_, false))
        showReview();
}

void App::startJobs()
{
    runner_ = std::make_unique<ThreadBackupRunner>();
    jobs_ = std::make_unique<JobScheduler>(*store_, [] { return QDateTime::currentDateTime(); }, *runner_);
    connect(host_.get(), &NativeHost::resumed, jobs_.get(), &JobScheduler::check); // held while the wizard is open
    connect(jobs_.get(), &JobScheduler::purged, this, [this] {
        if (board_)
            board_->reload();
    });
    connect(jobs_.get(), &JobScheduler::toast, this, [this](const QString& title, const QString& text) {
        if (tray_)
            tray_->showMessage(title, text);
        else
            qInfo().noquote() << "toast:" << text; // no tray: the log is the only trace
        if (settingsDialog_)
            settingsDialog_->refreshLastBackup();
        emit toastShown(title, text);
    });

    // Like autostartAction_, made here: the tray itself may not exist (tests run offscreen).
    backupAction_ = new QAction(tr("Back up now"), this);
    connect(backupAction_, &QAction::triggered, this, &App::backUpNow);
    connect(jobs_.get(), &JobScheduler::busyChanged, this, [this](bool busy) {
        backupAction_->setText(busy ? tr("Backing up…") : tr("Back up now"));
        backupAction_->setEnabled(!busy);
    });
    if (!setupPending_)
        jobs_->startTimers();
}

void App::backUpNow()
{
    if (!jobs_)
        return;
    try {
        noite::Settings settings = store_->settings();
        if (!settings.backupDir) {
            std::optional<QString> folder;
            // Like the tray's, the dialog's Back up now saves its folder now: Cancel doesn't undo it.
            if (settingsDialog_ && settingsDialog_->backupField()->values().folder) {
                folder = settingsDialog_->backupField()->values().folder;
            } else {
                // The dialog is modal but the tray menu is not: a second click must not stack another.
                backupAction_->setEnabled(false);
                folder = askFolder_();
                backupAction_->setEnabled(!jobs_->isBusy());
            }
            if (!folder)
                return;
            settings.backupDir = *folder;
            store_->saveSettings(settings);
            if (settingsDialog_) {
                BackupFolderField::Values values = settingsDialog_->backupField()->values();
                values.folder = *folder;
                settingsDialog_->backupField()->setValues(values);
            }
        }
    } catch (const std::exception& error) {
        qWarning() << "backup folder setting:" << error.what();
        toast(tr("Backup failed: %1").arg(QString::fromUtf8(error.what())));
        return;
    }
    jobs_->backUpNow(); // a folder that is gone is reported by its failure toast
}

void App::toast(const QString& text)
{
    if (tray_)
        tray_->showMessage(QStringLiteral("noite"), text);
    emit toastShown(QStringLiteral("noite"), text);
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

void App::syncAutostart(bool enabled)
{
    if (autostartAction_) {
        const QSignalBlocker blocker(autostartAction_);
        autostartAction_->setChecked(enabled);
    }
    updateAutostart(enabled);
}

void App::registerMissingHotkeys(const QString& reported)
{
    noite::Settings current;
    try {
        current = store_->settings();
    } catch (const std::exception& error) {
        qWarning() << "hotkeys:" << error.what();
        return;
    }
    for (const auto& [id, combo] : {std::pair{kCaptureHotkeyId, current.captureHotkey},
                                    std::pair{kBoardHotkeyId, current.boardHotkey}}) {
        if (!hotkeys_->isRegistered(id) && hotkeys_->set(id, combo) != HotkeyStatus::Ok
            && !(parseHotkey(combo) && parseHotkey(combo) == parseHotkey(reported))) {
            emit hotkeyUnavailable(combo);
        }
    }
}

void App::raiseWizard()
{
    wizard_->show();
    wizard_->raise();
    wizard_->activateWindow();
    SetForegroundWindow(reinterpret_cast<HWND>(wizard_->winId()));
}

void App::showWizard()
{
    if (settingsDialog_)
        settingsDialog_->reject();
    if (!wizard_) {
        noite::Settings initial;
        try {
            initial = store_->settings();
        } catch (const std::exception& error) {
            qWarning() << "setup:" << error.what();
            toast(tr("Setup could not be opened: %1").arg(QString::fromUtf8(error.what())));
            return;
        }
        const bool autostartManaged = autostart_ && !options_.dataFromCommandLine;
        wizard_ = new SetupWizard(initial, *hotkeys_,
                                  WizardContext{dataRoot_, syncWarning_, autostartManaged, [this] { return askFolder_(); }});
        wizard_->setAttribute(Qt::WA_DeleteOnClose);
        jobs_->setHeld(true); // until wizardClosed()
        connect(wizard_, &SetupWizard::finished, this, [this](const noite::Settings& collected, bool viaHotkey) {
            noite::Settings s = collected;
            s.setupCompleted = true;
            QString notice;     // the one toast, if any
            QString takenCombo; // a hotkey that notice already names
            try {
                if (const auto err = applier_->apply(s)) {
                    qWarning() << "wizard apply:" << err->message;
                    notice = tr("Setup could not be saved: %1").arg(err->message);
                    if (err->hotkeyId != 0) {
                        // R10: a hotkey alone still completes setup; the rest is saved and the
                        // hotkeys stay as they are.
                        if (const auto saveErr = applier_->applyKeepingHotkeys(s)) {
                            // Not completed: the wizard returns next start.
                            qWarning() << "wizard save:" << saveErr->message;
                            notice = tr("Setup could not be saved: %1").arg(saveErr->message);
                        } else if (err->status == HotkeyStatus::Taken) {
                            takenCombo = err->hotkeyId == kCaptureHotkeyId ? s.captureHotkey : s.boardHotkey;
                            notice = tr("%1 is used by another program. Change it in Settings.").arg(takenCombo);
                        } else {
                            notice = tr("%1 Change it in Settings.").arg(err->message);
                        }
                    }
                }
            } catch (const std::exception& error) {
                qWarning() << "wizard apply:" << error.what();
                notice = tr("Setup could not be saved: %1").arg(QString::fromUtf8(error.what()));
            }
            registerMissingHotkeys(takenCombo);
            if (!notice.isEmpty())
                toast(notice);
            if (viaHotkey) {
                if (window_->isVisible()) {
                    window_->summon(); // a capture already open: just bring it back
                } else {
                    try {
                        const noite::Scratch scratch = store_->createScratch(starterNoteHtml(s));
                        window_->summonNote(scratch.id);
                    } catch (const std::exception& error) {
                        qWarning() << "starter note:" << error.what();
                        window_->summon();
                    }
                }
            }
            wizardClosed(viaHotkey && window_->isVisible());
        });
        connect(wizard_, &SetupWizard::abandoned, this, [this](const noite::Settings& partial) {
            try {
                if (const auto err = applier_->apply(partial)) {
                    qWarning() << "wizard abandon:" << err->message;
                    // When the hotkeys can't be applied, the rest is still saved; the hotkeys stay as they are.
                    if (err->hotkeyId != 0) {
                        if (const auto saveErr = applier_->applyKeepingHotkeys(partial))
                            qWarning() << "wizard save:" << saveErr->message;
                    }
                }
            } catch (const std::exception& error) {
                qWarning() << "wizard abandon:" << error.what();
                toast(tr("Setup could not be saved: %1").arg(QString::fromUtf8(error.what())));
            }
            registerMissingHotkeys();
            wizardClosed();
        });
    }
    raiseWizard();
}

void App::showSettings()
{
    if (wizard_) {
        raiseWizard();
        return;
    }
    if (!settingsDialog_) {
        const bool autostartManaged = autostart_ && !options_.dataFromCommandLine;
        try {
            settingsDialog_ = new SettingsDialog(
                *store_, *hotkeys_, *applier_,
                SettingsContext{syncWarning_, autostartManaged, logsDir_, [this] { return askFolder_(); },
                                [] { return QDate::currentDate(); }});
        } catch (const std::exception& error) {
            qWarning() << "settings:" << error.what();
            toast(tr("Settings could not be opened: %1").arg(QString::fromUtf8(error.what())));
            return;
        }
        settingsDialog_->setAttribute(Qt::WA_DeleteOnClose);
        connect(settingsDialog_, &QDialog::finished, this, [this] {
            if (settingsDialog_) {
                settingsDialog_->deleteLater();
                settingsDialog_ = nullptr;
            }
        });
        connect(settingsDialog_, &SettingsDialog::runSetupRequested, this, [this] {
            if (settingsDialog_)
                settingsDialog_->reject();
            showWizard();
        });
        connect(settingsDialog_, &SettingsDialog::backUpNowRequested, this, &App::backUpNow);
    }
    settingsDialog_->show();
    settingsDialog_->raise();
    settingsDialog_->activateWindow();
    SetForegroundWindow(reinterpret_cast<HWND>(settingsDialog_->winId()));
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
        connect(review_, &ReviewWindow::aboutToChange, this, &App::closeCaptureHolding);
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
        connect(board_, &BoardWindow::aboutToChange, this, &App::closeCaptureHolding);
    }
    board_->show();
    board_->raise();
    board_->activateWindow();
    SetForegroundWindow(reinterpret_cast<HWND>(board_->winId()));
}

void App::closeCaptureHolding(const noite::ScratchId& id)
{
    if (window_->isVisible() && controller_->currentId() == id)
        controller_->dismiss();
}

int App::run()
{
    if (const std::optional<int> code = start())
        return *code;
    return QApplication::exec();
}

QString App::trayToolTip() const
{
    return QString("noite %1").arg(QString::fromLatin1(NOITE_VERSION));
}

void App::createTray()
{
    settingsAction_ = new QAction(tr("Settings"), this);
    connect(settingsAction_, &QAction::triggered, this, &App::showSettings);

    trayMenu_ = std::make_unique<QMenu>();
    trayMenu_->addAction(tr("New note"), window_.get(), &CaptureWindow::summon);
    trayMenu_->addAction(tr("Board"), this, &App::showBoard);
    trayMenu_->addAction(tr("Morning review"), this, &App::showReview);
    trayMenu_->addSeparator();
    trayMenu_->addAction(autostartAction_);
    trayMenu_->addAction(settingsAction_);
    trayMenu_->addSeparator();
    trayMenu_->addAction(backupAction_);
    trayMenu_->addSeparator();
    trayMenu_->addAction(tr("Quit"), qApp, &QCoreApplication::quit);

    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    tray_ = new QSystemTrayIcon(qApp->style()->standardIcon(QStyle::SP_FileDialogDetailedView), this);
    tray_->setToolTip(trayToolTip());
    tray_->setContextMenu(trayMenu_.get());
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
