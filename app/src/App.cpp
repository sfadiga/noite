#include "App.h"

#include "capture/CaptureController.h"
#include "capture/CaptureWindow.h"
#include "capture/LatencyProbe.h"
#include "editor/AttachmentStore.h"
#include "editor/NoteEdit.h"
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

namespace {
constexpr int kCaptureHotkeyId = 1;
}

App::App(AppOptions options, QObject* parent)
    : QObject(parent)
    , options_(std::move(options))
{
}

App::~App()
{
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
    host_ = std::make_unique<NativeHost>(instanceKeyFor(root));

    auto* editor = new NoteEdit; // owned by the window
    attachments_ = std::make_unique<StoreAttachments>(*store_);
    editor->setAttachmentStore(attachments_.get());
    controller_ = std::make_unique<CaptureController>(*store_, *editor, [] { return QDate::currentDate(); });
    window_ = std::make_unique<CaptureWindow>(*controller_, editor);
    window_->winId(); // create the native window now, while hidden (D5)
    probe_ = LatencyProbe::fromEnvironment();
    window_->setLatencyProbe(probe_.get());

    connect(host_.get(), &NativeHost::hotkeyPressed, this, [this](int id) {
        if (id != kCaptureHotkeyId)
            return;
        if (probe_)
            probe_->pressed();
        window_->summon();
    });
    connect(host_.get(), &NativeHost::activationRequested, window_.get(), &CaptureWindow::summon);
    connect(qApp, &QCoreApplication::aboutToQuit, controller_.get(), &CaptureController::flush);

    createTray();

    const QString combo = store_->settings().captureHotkey;
    const std::optional<Hotkey> hotkey = parseHotkey(combo);
    if (!hotkey || !host_->registerHotkey(kCaptureHotkeyId, *hotkey))
        emit hotkeyUnavailable(combo);
    return std::nullopt;
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
    menu->addSeparator();
    menu->addAction(tr("Quit"), qApp, &QCoreApplication::quit);
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            window_->summon();
    });
    connect(this, &App::hotkeyUnavailable, tray_, [this](const QString& combo) {
        tray_->showMessage("noite", tr("%1 is already used by another program. Use the tray icon to open a note.").arg(combo),
                           QSystemTrayIcon::Warning);
    });
    tray_->show();
}
