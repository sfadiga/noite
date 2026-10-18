#pragma once

#include <QApplication>
#include <QMouseEvent>
#include <QWidget>

namespace noite::test {

// A left-button drag on `widget`: press at `from` (widget coordinates), move by `delta`,
// release. Sent directly, so it doesn't depend on the platform's mouse state.
inline void drag(QWidget* widget, QPoint from, QPoint delta)
{
    const QPoint global = widget->mapToGlobal(from);
    auto send = [widget](QEvent::Type type, QPoint at, Qt::MouseButtons buttons) {
        QMouseEvent event(type, widget->mapFromGlobal(at), at, Qt::LeftButton, buttons, Qt::NoModifier);
        QApplication::sendEvent(widget, &event);
    };
    send(QEvent::MouseButtonPress, global, Qt::LeftButton);
    send(QEvent::MouseMove, global + delta / 2, Qt::LeftButton);
    send(QEvent::MouseMove, global + delta, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, global + delta, Qt::NoButton);
}

} // namespace noite::test
