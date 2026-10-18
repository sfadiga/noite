#include "board/NoteOverlay.h"

#include "DragMover.h"
#include "capture/NoteFooterBar.h"
#include "capture/TagField.h"
#include "editor/NoteEdit.h"

#include <QFrame>
#include <QKeyEvent>

#include <QPainter>
#include <QVBoxLayout>

namespace {
constexpr QSize kPanelSize{640, 520};
constexpr int kBackdropAlpha = 160;
} // namespace

NoteOverlay::NoteOverlay(noite::Store& store, AttachmentStore& attachments, std::function<QDate()> today,
                         QWidget* board)
    : QWidget(board)
    , panel_(new QFrame(this))
    , editor_(new NoteEdit(panel_))
    , controller_(store, *editor_, std::move(today))
{
    editor_->setAttachmentStore(&attachments);
    panel_->setAutoFillBackground(true);
    panel_->setBackgroundRole(QPalette::Base);
    panel_->setFrameShape(QFrame::StyledPanel);
    auto* layout = new QVBoxLayout(panel_);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->addWidget(editor_);
    footer_ = new NoteFooterBar(controller_, panel_); // after the controller it shows
    layout->addWidget(footer_);
    hide();

    new DragMover(panel_, panel_); // by its frame and footer; re-centred on every open
    board->installEventFilter(this);
    editor_->installEventFilter(this);
    footer_->tagField()->installEventFilter(this);
    connect(editor_, &NoteEdit::escapePressed, &controller_, &CaptureController::dismiss);
    connect(&controller_, &CaptureController::finished, this, [this] {
        open_ = false;
        hide();
        emit closed(controller_.currentId());
    });
}

void NoteOverlay::openNote(const noite::ScratchId& id, bool readOnly)
{
    controller_.open(id);
    editor_->setReadOnly(readOnly);
    footer_->setReadOnly(readOnly);
    present();
}

void NoteOverlay::openBlank()
{
    controller_.begin();
    editor_->setReadOnly(false);
    footer_->setReadOnly(false);
    present();
}

void NoteOverlay::closeAndSave()
{
    if (open_)
        controller_.dismiss();
}

QWidget* NoteOverlay::panel() const
{
    return panel_;
}

QString NoteOverlay::footerText() const
{
    return footer_->infoText();
}

void NoteOverlay::present()
{
    open_ = true;
    setGeometry(parentWidget()->rect());
    layoutPanel();
    show();
    raise();
    editor_->setFocus(Qt::OtherFocusReason);
}

void NoteOverlay::layoutPanel()
{
    const QSize size = kPanelSize.boundedTo(rect().size() - QSize(40, 40));
    panel_->setGeometry(QRect(rect().center() - QPoint(size.width() / 2, size.height() / 2), size));
}

void NoteOverlay::paintEvent(QPaintEvent*)
{
    QColor backdrop = palette().color(QPalette::Window);
    backdrop.setAlpha(kBackdropAlpha);
    QPainter(this).fillRect(rect(), backdrop);
}

void NoteOverlay::mousePressEvent(QMouseEvent* event)
{
    if (open_ && !panel_->geometry().contains(event->position().toPoint()))
        controller_.dismiss();
}

bool NoteOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
        layoutPanel();
    }
    if (watched == footer_->tagField() && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        controller_.dismiss(); // as Esc in the text: save and close
        return true;
    }
    if (watched == editor_ && event->type() == QEvent::KeyPress && !editor_->isReadOnly()) {
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
