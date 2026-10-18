#pragma once

#include "jobs/BackupRunner.h"

#include <noite/clock.h>
#include <noite/model.h>
#include <noite/settings.h>

#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QTimer>

#include <functional>

namespace noite {
class Store;
}

Q_DECLARE_METATYPE(noite::PurgeReport)

// Decides when the trash purge and the backups run (M5, B3/B6): each at most once per local
// day, a failed backup retried at every check, and a toast once backups have been failing for
// three days. The app calls startTimers(), and check() on resume. Store errors are logged,
// never thrown out of a slot.
class JobScheduler : public QObject {
    Q_OBJECT
public:
    static constexpr int kFirstCheckMs = 30'000;
    static constexpr int kCheckIntervalMs = 3'600'000;
    static constexpr qint64 kAlertAfterMs = 3 * noite::kDayMs;

    JobScheduler(noite::Store& store, std::function<QDateTime()> now, BackupRunner& runner, QObject* parent = nullptr);

    void check();
    // The 30 s single shot plus the hourly timer; tests call check() instead.
    void startTimers();
    // Ignores the schedule and always toasts the result. False while busy or without a backup folder.
    bool backUpNow();
    bool isBusy() const { return busy_; }

signals:
    void toast(QString title, QString text);
    void busyChanged(bool busy);
    void purged(noite::PurgeReport report);

private:
    void purgeIfDue(const QDate& today);
    static bool backupDue(const noite::Settings& settings, const noite::JobState& state, const QDate& today);
    void startBackup(const noite::Settings& settings, bool manual);
    void onFinished(const noite::backup::Result& result);
    void onFailed(const QString& message);
    void setBusy(bool busy);
    void save(const noite::JobState& state);

    noite::Store& store_;
    std::function<QDateTime()> now_;
    BackupRunner& runner_;
    QTimer firstCheck_, hourly_;
    bool busy_ = false;
    bool manual_ = false;
    qint64 startedMs_ = 0;
    QDate skipLoggedDay_;
};
