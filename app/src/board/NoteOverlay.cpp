#include "board/NoteOverlay.h"

#include "capture/NoteFooter.h"
#include "editor/NoteEdit.h"

#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
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
    , footer_(new QLabel(panel_))
    , controller_(store, *editor_, std::move(today))
{
    editor_->setAttachmentStore(&attachments);
    panel_->setAutoFillBackground(true);
    panel_->setBackgroundRole(QPalette::Base);
    panel_->setFrameShape(QFrame::StyledPanel);
    auto* layout = new QVBoxLayout(panel_);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->addWidget(editor_);
    layout->addWidget(footer_);
    footer_->setVisible(false);
    hide();

    board->installEventFilter(this);
    editor_->installEventFilter(this);
    connect(editor_, &NoteEdit::escapePressed, &controller_, &CaptureController::dismiss);
    connect(&controller_, &CaptureController::finished, this, [this] {
        open_ = false;
        hide();
        emit closed(controller_.currentId());
    });
    connect(&controller_, &CaptureController::dueChanged, this, &NoteOverlay::updateFooter);
    connect(&controller_, &CaptureController::colorChanged, this, &NoteOverlay::updateFooter);
}

void NoteOverlay::openNote(const noite::ScratchId& id)
{
    controller_.open(id);
    present();
}

void NoteOverlay::openBlank()
{
    controller_.begin();
    present();
}

void NoteOverlay::closeAndSave()
{
    if (open_)
        controller_.dismiss();
}

QString NoteOverlay::footerText() const
{
    return footer_->text();
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

void NoteOverlay::updateFooter()
{
    footer_->setText(noteFooterText(controller_.due(), controller_.color()));
    footer_->setVisible(!footer_->text().isEmpty());
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
    if (watched == editor_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const auto mods = key->modifiers() & ~Qt::KeypadModifier;
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
