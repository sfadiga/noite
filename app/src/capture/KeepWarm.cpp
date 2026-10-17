#include "capture/KeepWarm.h"

#include <QPixmap>

KeepWarm::KeepWarm(QWidget* window, std::chrono::milliseconds interval, QObject* parent)
    : QObject(parent)
    , window_(window)
{
    timer_.setInterval(interval);
    connect(&timer_, &QTimer::timeout, this, &KeepWarm::tick);
}

bool KeepWarm::enabledByEnvironment()
{
    return qEnvironmentVariable("NOITE_KEEP_WARM") != "0";
}

void KeepWarm::tick()
{
    if (!window_ || window_->isVisible())
        return;
    const QPixmap rendered = window_->grab(); // paints the whole widget tree offscreen, then dropped
    Q_UNUSED(rendered);
    ++ticks_;
}
