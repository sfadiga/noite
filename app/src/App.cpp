#include "App.h"

#include "capture/CaptureController.h"
#include "capture/CaptureWindow.h"
#include "capture/KeepWarm.h"
#include "capture/LatencyProbe.h"
#include "editor/AttachmentStore.h"
#include "editor/NoteEdit.h"
#include "editor/OpenFolder.h"
#include "platform/Hotkey.h"
#include "platform/NativeHost.h"

#include <noite/data_dir.h>
#include <noite/error.h>
#include <noite/store.h>

#include <QApplication>
#include <QDebug>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>

#include <windows.h>

App::App(AppOptions options, QObject* parent)
    : QObject(parent)
    , options_(std::move(options))
{
}

App::~App()
{
    if (board_) {
        board_->close(); // saves a note open in its overlay
        delete board_;   // before the store it uses
    }
    if (controller_)
        controller_->flush();
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

    createTray();

    const noite::Settings settings = store_->settings();
    registerHotkey(kCaptureHotkeyId, settings.captureHotkey);
    registerHotkey(kBoardHotkeyId, settings.boardHotkey);
    return std::nullopt;
}

void App::registerHotkey(int id, const QString& combo)
{
    const std::optional<Hotkey> hotkey = parseHotkey(combo);
    if (!hotkey || !host_->registerHotkey(id, *hotkey))
        emit hotkeyUnavailable(combo);
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
