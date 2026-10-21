#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimer>

#include <functional>

namespace noite {
class Store;
}

// Decides when the morning review opens (D16, M4 R1/R6): once a day, the first check after
// the day-start time while the session is unlocked. A check that finds nothing to review
// still counts the day as done. The app calls check() at start, on unlock and on resume; an
// internal timer also fires at the next day start, for a PC already in use. Each check logs
// one line, "review: <trigger> · <outcome>" (M8d).
class ReviewScheduler : public QObject {
    Q_OBJECT
public:
    ReviewScheduler(noite::Store& store, std::function<QDateTime()> now, std::function<bool()> unlocked,
                    QObject* parent = nullptr);

    // `trigger` names the caller in the log: start, unlock, resume, timer, settings, setup.
    void check(const QString& trigger);
    // Until the next day start: today's if it is still ahead, else tomorrow's.
    int msUntilNextCheck() const;
    void fireTimerForTest() { check(QStringLiteral("timer")); }

signals:
    void reviewDue();

private:
    noite::Store& store_;
    std::function<QDateTime()> now_;
    std::function<bool()> unlocked_;
    QTimer timer_;
};
