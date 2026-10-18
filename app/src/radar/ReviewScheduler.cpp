#include "radar/ReviewScheduler.h"

#include <noite/store.h>

#include <QDebug>
#include <QScopeGuard>

ReviewScheduler::ReviewScheduler(noite::Store& store, std::function<QDateTime()> now, std::function<bool()> unlocked,
                                 QObject* parent)
    : QObject(parent)
    , store_(store)
    , now_(std::move(now))
    , unlocked_(std::move(unlocked))
{
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &ReviewScheduler::check);
}

int ReviewScheduler::msUntilNextCheck() const
{
    QTime dayStart(6, 0);
    try {
        dayStart = store_.settings().dayStart;
    } catch (const std::exception& error) {
        qWarning() << "review: settings:" << error.what();
    }
    const QDateTime now = now_();
    QDateTime next(now.date(), dayStart);
    if (next <= now)
        next = QDateTime(now.date().addDays(1), dayStart);
    return int(now.msecsTo(next));
}

void ReviewScheduler::check()
{
    const auto rearm = qScopeGuard([this] { timer_.start(msUntilNextCheck()); });
    try {
        const noite::Settings settings = store_.settings();
        const QDateTime now = now_();
        const QDate today = now.date();
        if (now.time() < settings.dayStart)
            return;
        if (store_.lastReviewDay() == today)
            return;
        if (!unlocked_())
            return; // the unlock will check again
        store_.setLastReviewDay(today);
        if (store_.reviewQueue(today, settings.staleDays).isEmpty())
            return;
        emit reviewDue();
    } catch (const std::exception& error) {
        qWarning() << "review:" << error.what();
    }
}
