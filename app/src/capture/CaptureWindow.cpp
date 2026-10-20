#include "capture/CaptureWindow.h"

#include "DragMover.h"
#include "capture/CaptureController.h"
#include "capture/LatencyProbe.h"
#include "capture/NoteFooterBar.h"
#include "capture/TagField.h"
#include "editor/NoteEdit.h"

#include <QGuiApplication>
#include <QKeyEvent>

#include <QScreen>
#include <QVBoxLayout>
#include <QtGui/qscreen_platform.h>

#include <windows.h>

namespace {

// The screen showing the foreground window (D6), else the cursor's, else the primary.
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

CaptureWindow::CaptureWindow(CaptureController& controller, NoteEdit* editor)
    : QWidget(nullptr, Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint)
    , controller_(controller)
    , editor_(editor)
    , footer_(new NoteFooterBar(controller, this))
{
    setWindowTitle("noite");
    resize(520, 420);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->addWidget(editor_);
    layout->addWidget(footer_);

    new DragMover(this, this); // by the margins and footer
    editor_->installEventFilter(this);
    editor_->viewport()->installEventFilter(this);
    footer_->tagField()->installEventFilter(this);
    connect(editor_, &NoteEdit::escapePressed, &controller_, &CaptureController::dismiss);
    connect(&controller_, &CaptureController::finished, this, [this] {
        if (probe_)
            probe_->dismissed(editor_->toPlainText());
        hide();
    });
}

void CaptureWindow::summon()
{
    if (!isVisible())
        controller_.begin();
    showAndFocus();
}

void CaptureWindow::summonNote(const noite::ScratchId& id)
{
    if (!isVisible())
        controller_.open(id);
    showAndFocus();
}

void CaptureWindow::showAndFocus()
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

QString CaptureWindow::footerText() const
{
    return footer_->infoText();
}

bool CaptureWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (probe_ && watched == editor_->viewport() && event->type() == QEvent::Paint && editor_->hasFocus())
        probe_->painted();
    if (watched == footer_->tagField() && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        controller_.dismiss(); // as Esc in the text: save and hide
        return true;
    }
    if (watched == editor_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const auto mods = key->modifiers() & ~Qt::KeypadModifier;
        if (key->key() == Qt::Key_T && mods == Qt::ControlModifier) {
            footer_->focusTags();
            return true;
        }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && mods == Qt::ControlModifier) {
            controller_.markDoneAndClose();
            return true;
        }
        if (key->key() == Qt::Key_C && mods == (Qt::ControlModifier | Qt::ShiftModifier)) {
            controller_.cycleColor();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
