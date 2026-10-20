#include "capture/Prewarm.h"

#include <QPixmap>
#include <QWidget>

std::optional<QString> prewarmCapture(QWidget* window, PrewarmOptions options)
{
    Q_UNUSED(options);
    window->winId();                          // the native window, created while hidden
    const QPixmap rendered = window->grab(); // paints the widget tree offscreen, then dropped
    Q_UNUSED(rendered);
    return std::nullopt;
}
