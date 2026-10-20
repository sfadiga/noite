#include "capture/Prewarm.h"

#include "platform/WorkingSetFloor.h"

#include <QPixmap>
#include <QWidget>

std::optional<QString> prewarmCapture(QWidget* window, PrewarmOptions options)
{
    window->winId();                          // the native window, created while hidden
    const QPixmap rendered = window->grab(); // paints the widget tree offscreen, then dropped
    Q_UNUSED(rendered);
    if (options.floor)
        return applyWorkingSetFloor(); // taken now: the warmed pages plus a margin for show()
    return std::nullopt;
}
