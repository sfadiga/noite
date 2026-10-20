#pragma once

#include <QCursor>
#include <QObject>
#include <QPoint>
#include <QRect>

class QWidget;

// Lets the user resize the frameless `window` by dragging any edge or corner (a thin band
// along its border), with the matching resize cursor on hover; never below its minimumSize().
// Made after a DragMover on the same window, so the band is taken first and the rest of the
// margin still moves it.
class EdgeResizer : public QObject {
    Q_OBJECT
public:
    explicit EdgeResizer(QWidget* window);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    Qt::Edges edgesAt(QPoint pos) const;

    QWidget* window_;
    QCursor idleCursor_; // the window's own cursor off the band
    Qt::Edges edges_;    // being dragged; none while not resizing
    QPoint pressGlobal_;
    QRect startGeometry_;
};
