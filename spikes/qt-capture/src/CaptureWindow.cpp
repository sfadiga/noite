#include "CaptureWindow.h"

#include "NoteEdit.h"
#include "Trials.h"

#include <QGuiApplication>
#include <QScreen>
#include <QVBoxLayout>
#include <QtGui/qscreen_platform.h>

#include <windows.h>

namespace {

// The screen showing the foreground window (D6), falling back to the cursor's screen.
QScreen* foregroundScreen()
{
    const HMONITOR monitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONEAREST);
    for (QScreen* screen : QGuiApplication::screens()) {
        auto* native = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
        if (native && native->handle() == monitor)
            return screen;
    }
    if (QScreen* screen = QGuiApplication::screenAt(QCursor::pos()))
        return screen;
    return QGuiApplication::primaryScreen();
}

} // namespace

CaptureWindow::CaptureWindow(Trials* trials)
    : QWidget(nullptr, Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint)
    , editor_(new NoteEdit(this))
{
    setWindowTitle("noite-capture");
    resize(520, 420);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addWidget(editor_);
    editor_->setTrials(trials);
    connect(editor_, &NoteEdit::escapePressed, this, &CaptureWindow::dismiss);
}

void CaptureWindow::summon()
{
    if (!isVisible()) {
        const QRect area = foregroundScreen()->availableGeometry();
        move(area.center() - QPoint(width() / 2, height() / 2));
        show();
    }
    raise();
    activateWindow();
    SetForegroundWindow(reinterpret_cast<HWND>(winId()));
    editor_->setFocus(Qt::ActiveWindowFocusReason);
}

void CaptureWindow::dismiss()
{
    const QString text = editor_->toPlainText();
    Trials::writeDiagnostics(editor_->toHtml(), editor_->takeKeyLog());
    emit dismissed(text);
    editor_->clear();
    hide();
}
