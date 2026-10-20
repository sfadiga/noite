#include "EdgeResizer.h"

#include <QMouseEvent>
#include <QWidget>

#include <algorithm>

namespace {

constexpr int kBand = 6; // px along the border; inside the window's layout margins

Qt::CursorShape cursorFor(Qt::Edges edges)
{
    if (edges == (Qt::TopEdge | Qt::LeftEdge) || edges == (Qt::BottomEdge | Qt::RightEdge))
        return Qt::SizeFDiagCursor;
    if (edges == (Qt::TopEdge | Qt::RightEdge) || edges == (Qt::BottomEdge | Qt::LeftEdge))
        return Qt::SizeBDiagCursor;
    if (edges & (Qt::LeftEdge | Qt::RightEdge))
        return Qt::SizeHorCursor;
    return Qt::SizeVerCursor;
}

} // namespace

EdgeResizer::EdgeResizer(QWidget* window)
    : QObject(window)
    , window_(window)
    , idleCursor_(window->cursor())
{
    window->installEventFilter(this);
    window->setMouseTracking(true); // hover moves, for the cursor
}

Qt::Edges EdgeResizer::edgesAt(QPoint pos) const
{
    Qt::Edges edges;
    if (pos.x() < kBand)
        edges |= Qt::LeftEdge;
    else if (pos.x() >= window_->width() - kBand)
        edges |= Qt::RightEdge;
    if (pos.y() < kBand)
        edges |= Qt::TopEdge;
    else if (pos.y() >= window_->height() - kBand)
        edges |= Qt::BottomEdge;
    return edges;
}

bool EdgeResizer::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        const Qt::Edges edges = edgesAt(mouse->position().toPoint());
        if (mouse->button() != Qt::LeftButton || !edges)
            break;
        edges_ = edges;
        pressGlobal_ = mouse->globalPosition().toPoint();
        startGeometry_ = window_->geometry();
        return true;
    }
    case QEvent::MouseMove: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (!edges_) {
            if (mouse->buttons() == Qt::NoButton) {
                const Qt::Edges hovered = edgesAt(mouse->position().toPoint());
                window_->setCursor(hovered ? QCursor(cursorFor(hovered)) : idleCursor_);
            }
            break;
        }
        const QPoint delta = mouse->globalPosition().toPoint() - pressGlobal_;
        const QSize min = window_->minimumSize();
        QRect g = startGeometry_;
        if (edges_ & Qt::LeftEdge)
            g.setLeft(std::min(g.left() + delta.x(), g.right() + 1 - min.width()));
        if (edges_ & Qt::RightEdge)
            g.setRight(std::max(g.right() + delta.x(), g.left() - 1 + min.width()));
        if (edges_ & Qt::TopEdge)
            g.setTop(std::min(g.top() + delta.y(), g.bottom() + 1 - min.height()));
        if (edges_ & Qt::BottomEdge)
            g.setBottom(std::max(g.bottom() + delta.y(), g.top() - 1 + min.height()));
        window_->setGeometry(g);
        return true;
    }
    case QEvent::MouseButtonRelease:
        if (!edges_)
            break;
        edges_ = {};
        return true;
    case QEvent::Leave:
        if (!edges_)
            window_->setCursor(idleCursor_);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}
