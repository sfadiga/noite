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
    connect(&timer_, &QTimer::timeout, this, [this] { check(QStringLiteral("timer")); });
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

void ReviewScheduler::check(const QString& trigger)
{
    const auto rearm = qScopeGuard([this] { timer_.start(msUntilNextCheck()); });
    const auto log = [&](const QString& outcome) {
        qInfo().noquote() << QString::fromUtf8("review: %1 · %2").arg(trigger, outcome);
    };
    try {
        const noite::Settings settings = store_.settings();
        const QDateTime now = now_();
        const QDate today = now.date();
        if (now.time() < settings.dayStart) {
            log(QStringLiteral("before the day start (%1)").arg(settings.dayStart.toString("HH:mm")));
            return;
        }
        if (store_.lastReviewDay() == today) {
            log(QStringLiteral("already shown today"));
            return;
        }
        if (!unlocked_()) {
            log(QStringLiteral("locked, waits for the unlock"));
            return;
        }
        store_.setLastReviewDay(today);
        const QList<noite::ReviewItem> queue = store_.reviewQueue(today, settings.staleDays);
        if (queue.isEmpty()) {
            log(QStringLiteral("nothing to review"));
            return;
        }
        int counts[4] = {}; // by ReviewReason
        for (const noite::ReviewItem& item : queue)
            ++counts[int(item.reason)];
        log(QString::fromUtf8("shown · %1 overdue, %2 due today, %3 stale, %4 undated")
                .arg(counts[0])
                .arg(counts[1])
                .arg(counts[2])
                .arg(counts[3]));
        emit reviewDue();
    } catch (const std::exception& error) {
        qWarning().noquote() << "review:" << trigger << error.what();
    }
}
