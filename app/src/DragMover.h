#pragma once

#include <QObject>
#include <QPoint>

class QWidget;

// Lets the user drag `target` around by pressing the left button on `handle`: on its own
// area and on children that leave the mouse to it (labels, margins), not on ones that use it
// (the editor). A child target stays inside its parent; a window moves freely.
class DragMover : public QObject {
    Q_OBJECT
public:
    DragMover(QWidget* handle, QWidget* target);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* target_;
    bool dragging_ = false;
    QPoint pressGlobal_;
    QPoint startPos_;
};
