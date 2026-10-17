#include "DragMover.h"

#include <QMouseEvent>
#include <QWidget>

#include <algorithm>

DragMover::DragMover(QWidget* handle, QWidget* target)
    : QObject(handle)
    , target_(target)
{
    handle->installEventFilter(this);
    handle->setCursor(Qt::SizeAllCursor);
}

bool DragMover::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton)
            break;
        dragging_ = true;
        pressGlobal_ = mouse->globalPosition().toPoint();
        startPos_ = target_->pos();
        return true;
    }
    case QEvent::MouseMove: {
        if (!dragging_)
            break;
        QPoint pos = startPos_ + static_cast<QMouseEvent*>(event)->globalPosition().toPoint() - pressGlobal_;
        if (!target_->isWindow()) {
            const QRect area = target_->parentWidget()->rect();
            pos.setX(std::clamp(pos.x(), 0, std::max(0, area.width() - target_->width())));
            pos.setY(std::clamp(pos.y(), 0, std::max(0, area.height() - target_->height())));
        }
        target_->move(pos);
        return true;
    }
    case QEvent::MouseButtonRelease:
        if (!dragging_)
            break;
        dragging_ = false;
        return true;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}
