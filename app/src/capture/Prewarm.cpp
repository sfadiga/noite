#include "capture/Prewarm.h"

#include <QPixmap>
#include <QWidget>

void prewarmCapture(QWidget* window)
{
    window->winId();                          // the native window, created while hidden
    const QPixmap rendered = window->grab(); // paints the widget tree offscreen, then dropped
    Q_UNUSED(rendered);
}
