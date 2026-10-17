#include <noite/clock.h>

#include <QDateTime>

namespace noite {

qint64 SystemClock::nowMs() const
{
    return QDateTime::currentMSecsSinceEpoch();
}

} // namespace noite
