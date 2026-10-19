#include "jobs/JobScheduler.h"

#include <noite/store.h>

#include <QDebug>
#include <QDir>
#include <QLocale>

using namespace noite;

JobScheduler::JobScheduler(Store& store, std::function<QDateTime()> now, BackupRunner& runner, QObject* parent)
    : QObject(parent)
    , store_(store)
    , now_(std::move(now))
    , runner_(runner)
{
    firstCheck_.setSingleShot(true);
    connect(&firstCheck_, &QTimer::timeout, this, &JobScheduler::check);
    hourly_.setInterval(kCheckIntervalMs);
    connect(&hourly_, &QTimer::timeout, this, &JobScheduler::check);
    connect(&runner_, &BackupRunner::finished, this, &JobScheduler::onFinished);
    connect(&runner_, &BackupRunner::failed, this, [this](ErrorCode code, const QString& message) { onFailed(code, message); });
    retry_.setSingleShot(true);
    retry_.setInterval(kMissingFolderRetryMs);
    connect(&retry_, &QTimer::timeout, this, &JobScheduler::check);
}

void JobScheduler::startTimers()
{
    firstCheck_.start(kFirstCheckMs);
    hourly_.start();
}

void JobScheduler::setHeld(bool held)
{
    held_ = held;
    if (!held_ && skippedWhileHeld_) {
        skippedWhileHeld_ = false;
        check();
    }
}

void JobScheduler::setBusy(bool busy)
{
    if (busy_ == busy)
        return;
    busy_ = busy;
    emit busyChanged(busy);
}

void JobScheduler::save(const JobState& state)
{
    try {
        store_.saveJobState(state);
    } catch (const std::exception& error) {
        qWarning().noquote() << "jobs: saving state:" << error.what();
    }
}

void JobScheduler::check()
{
    if (held_) {
        skippedWhileHeld_ = true;
        return;
    }
    try {
        const QDate today = now_().date();
        // The purge's WAL checkpoint waits on the backup's read snapshot (up to the busy timeout),
        // which would freeze the UI; it retries at the next check.
        if (busy_)
            return;
        purgeIfDue(today);
        const Settings settings = store_.settings();
        if (!settings.backupDir) {
            if (skipLoggedDay_ != today) {
                skipLoggedDay_ = today;
                qInfo() << "backup: skipped, no folder";
            }
            return;
        }
        if (backupDue(settings, store_.jobState(), today))
            startBackup(settings, false);
    } catch (const std::exception& error) {
        qWarning().noquote() << "jobs:" << error.what();
    }
}

void JobScheduler::purgeIfDue(const QDate& today)
{
    try {
        JobState state = store_.jobState();
        if (state.lastPurgeDay == today)
            return;
        const PurgeReport report = store_.purgeTrash(store_.settings().trashRetentionDays);
        state.lastPurgeDay = today;
        save(state);
        qInfo().noquote() << QStringLiteral("purge: %1 notes, %2 attachments")
                                 .arg(report.scratchesDeleted)
                                 .arg(report.attachmentsDeleted);
        emit purged(report);
    } catch (const std::exception& error) {
        qWarning().noquote() << QStringLiteral("purge: failed: %1").arg(QString::fromUtf8(error.what()));
    }
}

bool JobScheduler::backupDue(const Settings& settings, const JobState& state, const QDate& today)
{
    if (!settings.backupDir || state.lastBackupDay == today)
        return false;
    if (settings.backupFrequency == BackupFrequency::Daily || !state.lastBackupDay)
        return true;
    const qint64 days = state.lastBackupDay->daysTo(today);
    return days >= 7 || days < 0;
}

void JobScheduler::startBackup(const Settings& settings, bool manual)
{
    const backup::Job job{store_.dataDir().dbPath(), store_.installId(), *settings.backupDir, settings.backupKeep,
                          now_().toMSecsSinceEpoch()};
    manual_ = manual;
    startedDay_ = now_().date();
    startedMs_ = QDateTime::currentMSecsSinceEpoch();
    setBusy(true);
    runner_.start(job);
}

bool JobScheduler::backUpNow()
{
    if (busy_)
        return false;
    try {
        const Settings settings = store_.settings();
        if (!settings.backupDir)
            return false;
        startBackup(settings, true);
        return true;
    } catch (const std::exception& error) {
        qWarning().noquote() << "jobs:" << error.what();
        return false;
    }
}

void JobScheduler::onFinished(const backup::Result& result)
{
    const QString size = QString::number(result.bytes / 1048576.0, 'f', 1);
    const QString seconds = QString::number((QDateTime::currentMSecsSinceEpoch() - startedMs_) / 1000.0, 'f', 1);
    qInfo().noquote() << QStringLiteral("backup: ok %1 · %2 MB · %3 s · pruned %4")
                             .arg(QDir::toNativeSeparators(result.path), size, seconds)
                             .arg(result.pruned);
    missingSinceMs_.reset();
    retry_.stop();
    try {
        JobState state = store_.jobState();
        state.lastBackupDay = startedDay_;
        state.backupFailingSinceMs.reset();
        state.backupAlertDay.reset();
        save(state);
    } catch (const std::exception& error) {
        qWarning().noquote() << "jobs: state:" << error.what();
    }
    setBusy(false);
    if (manual_)
        emit toast(QStringLiteral("noite"), QStringLiteral("Backed up · %1 MB").arg(size));
}

void JobScheduler::onFailed(ErrorCode code, const QString& message)
{
    qWarning().noquote() << QStringLiteral("backup: failed: %1").arg(message);
    QString alert;
    try {
        const QDateTime now = now_();
        const qint64 nowMs = now.toMSecsSinceEpoch();
        if (!manual_ && code == ErrorCode::BackupDirMissing) {
            if (!missingSinceMs_)
                missingSinceMs_ = nowMs;
            if (nowMs - *missingSinceMs_ < kMissingFolderRetryWindowMs)
                retry_.start();
        } else if (!manual_) {
            missingSinceMs_.reset();
            retry_.stop();
        }
        JobState state = store_.jobState();
        if (!state.backupFailingSinceMs)
            state.backupFailingSinceMs = nowMs;
        if (!manual_ && nowMs - *state.backupFailingSinceMs >= kAlertAfterMs && state.backupAlertDay != now.date()) {
            state.backupAlertDay = now.date();
            const QDate since = QDateTime::fromMSecsSinceEpoch(*state.backupFailingSinceMs).date();
            alert = QStringLiteral("Backups have been failing since %1: %2")
                        .arg(QLocale(QLocale::English).toString(since, "MMM d"), message);
        }
        save(state);
    } catch (const std::exception& error) {
        qWarning().noquote() << "jobs: state:" << error.what();
    }
    setBusy(false);
    if (manual_)
        emit toast(QStringLiteral("noite"), QStringLiteral("Backup failed: %1").arg(message));
    else if (!alert.isEmpty())
        emit toast(QStringLiteral("noite"), alert);
}
