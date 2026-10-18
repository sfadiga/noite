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
#include <optional>

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
    // A scheduled backup that finds its folder missing (Drive not mounted yet) retries every 5 min
    // for an hour after the first such failure, then falls back to the hourly checks.
    static constexpr int kMissingFolderRetryMs = 300'000;
    static constexpr qint64 kMissingFolderRetryWindowMs = 3'600'000;

    JobScheduler(noite::Store& store, std::function<QDateTime()> now, BackupRunner& runner, QObject* parent = nullptr);

    void check();
    // The 30 s single shot plus the hourly timer; tests call check() instead.
    void startTimers();
    // Ignores the schedule and always toasts the result. False while busy or without a backup folder.
    bool backUpNow();
    bool isBusy() const { return busy_; }
    bool retryPendingForTest() const { return retry_.isActive(); }
    int retryIntervalForTest() const { return retry_.interval(); }
    void fireRetryForTest()
    {
        retry_.stop(); // as the expiring single shot would
        check();
    }

signals:
    void toast(QString title, QString text);
    void busyChanged(bool busy);
    void purged(noite::PurgeReport report);

private:
    void purgeIfDue(const QDate& today);
    static bool backupDue(const noite::Settings& settings, const noite::JobState& state, const QDate& today);
    void startBackup(const noite::Settings& settings, bool manual);
    void onFinished(const noite::backup::Result& result);
    void onFailed(noite::ErrorCode code, const QString& message);
    void setBusy(bool busy);
    void save(const noite::JobState& state);

    noite::Store& store_;
    std::function<QDateTime()> now_;
    BackupRunner& runner_;
    QTimer firstCheck_, hourly_, retry_;
    bool busy_ = false;
    bool manual_ = false;
    qint64 startedMs_ = 0;
    QDate startedDay_;                       // local day the run in flight started (it may finish after midnight)
    std::optional<qint64> missingSinceMs_;   // first BackupDirMissing of the current streak (store clock)
    QDate skipLoggedDay_;
};
