#include "setup/HotkeyField.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>

HotkeyField::HotkeyField(HotkeyManager& manager, int id, QWidget* parent)
    : QWidget(parent), manager_(manager), id_(id), box_(new QLabel), status_(new QLabel)
{
    setFocusPolicy(Qt::StrongFocus);
    box_->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    box_->setMinimumWidth(150);
    box_->setMargin(3);
    box_->setAttribute(Qt::WA_TransparentForMouseEvents); // clicks land on the field itself
    status_->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(box_);
    layout->addWidget(status_, 1);
    display(QString(), QString());
}

HotkeyField::~HotkeyField()
{
    if (recording_)
        manager_.resume();
}

int HotkeyField::otherId() const
{
    return id_ == HotkeyManager::kCaptureId ? HotkeyManager::kBoardId : HotkeyManager::kCaptureId;
}

void HotkeyField::display(const QString& boxText, const QString& status)
{
    box_->setText(boxText);
    status_->setText(status);
}

QString HotkeyField::statusText() const
{
    return status_->text();
}

void HotkeyField::setCombo(const QString& combo)
{
    endRecording(false);
    combo_ = combo;
    state_ = combo.isEmpty() ? State::Empty : State::Accepted;
    display(combo, isAccepted() ? hotkeyMessage(HotkeyStatus::Ok, combo, otherId()) : QString());
}

void HotkeyField::showError(const QString& message, HotkeyStatus why)
{
    const bool wasAccepted = isAccepted();
    endRecording(false);
    switch (why) {
    case HotkeyStatus::Invalid:
        state_ = State::Invalid;
        break;
    case HotkeyStatus::SameAsOther:
        state_ = State::SameAsOther;
        break;
    default:
        state_ = State::Taken;
        break;
    }
    display(combo_, message);
    if (wasAccepted)
        emit changed();
}

void HotkeyField::setOther(const QString& combo)
{
    other_ = combo;
}

void HotkeyField::cancelRecording()
{
    endRecording(true);
}

void HotkeyField::startRecording()
{
    if (recording_)
        return;
    recording_ = true;
    savedStatus_ = status_->text();
    manager_.pause();
    display(tr("Press the shortcut…"), QString());
    if (state_ == State::Accepted)
        emit changed();
}

// Leaves recording and resumes the hotkeys; `restore` brings back the previous combination and
// status (Esc, focus-out), otherwise the new one is already in place.
void HotkeyField::endRecording(bool restore)
{
    if (!recording_)
        return;
    recording_ = false;
    manager_.resume();
    if (restore) {
        display(combo_, savedStatus_);
        if (state_ == State::Accepted)
            emit changed();
    }
}

void HotkeyField::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    startRecording(); // also when the field already has focus after an accepted combination
    event->accept();
}

void HotkeyField::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    switch (event->reason()) {
    case Qt::MouseFocusReason:
    case Qt::TabFocusReason:
    case Qt::BacktabFocusReason:
    case Qt::ShortcutFocusReason:
        startRecording();
        break;
    default:
        break;
    }
}

void HotkeyField::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    endRecording(true);
}

// While recording, shortcuts must not steal the key (Ctrl+Alt+K may be a menu shortcut).
bool HotkeyField::event(QEvent* event)
{
    if (recording_ && event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    return QWidget::event(event);
}

void HotkeyField::keyPressEvent(QKeyEvent* event)
{
    if (!recording_) {
        QWidget::keyPressEvent(event);
        return;
    }
    switch (event->key()) {
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Shift:
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
        event->accept(); // still building the combination: wait for a real key
        return;
    case Qt::Key_Escape:
        event->accept();
        endRecording(true);
        return;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
            event->ignore(); // plain Tab moves focus, which ends recording
            QWidget::keyPressEvent(event);
            return;
        }
        break;
    default:
        break;
    }
    event->accept();

    // The virtual key is layout independent; synthetic events carry none, so fall back to Qt's key.
    const quint32 vk = event->nativeVirtualKey();
    const auto combo = vk ? comboFromNativeKey(vk, event->modifiers()) : comboFromKey(event->key(), event->modifiers());
    if (!combo) {
        status_->setText(hotkeyMessage(HotkeyStatus::Invalid, QString(), otherId()));
        return;
    }
    const auto parsed = parseHotkey(*combo);
    const auto other = parseHotkey(other_);
    HotkeyStatus status = HotkeyStatus::Ok;
    if (parsed && other && *parsed == *other)
        status = HotkeyStatus::SameAsOther;
    else
        status = manager_.probe(*combo);

    status_->setText(hotkeyMessage(status, *combo, otherId()));
    const bool keep = status == HotkeyStatus::SameAsOther && keepsSameAsOther_;
    if (status != HotkeyStatus::Ok && !keep)
        return;
    combo_ = *combo;
    state_ = keep ? State::SameAsOther : State::Accepted;
    box_->setText(combo_);
    endRecording(false);
    emit changed();
}
