#pragma once

// Backups to a folder the user picks, e.g. their Google Drive for Desktop folder (D13).
// Layout under <dest>, one subfolder per installation so several PCs or portable folders
// can share one backup folder without pruning each other:
//   noite-backups/<installId>/snapshots/noite-YYYYMMDDTHHMMSSzzzZ.db   VACUUM INTO copies
// Attachments live in the database, so each snapshot is self-contained (A3).

#include <QString>

#include <atomic>

namespace noite {

class Store;

namespace backup {

struct PruneReport {
    int snapshotsDeleted{};
};

// Writes a snapshot; the .db appears under its final name only once complete.
// `dest` must exist (it is never created): throws BackupDirMissing otherwise.
QString snapshot(Store& store, const QString& dest);

// For this installation only: keeps the `keep` newest snapshots and removes temp files
// older than an hour (a younger one may be a snapshot in progress).
PruneReport prune(const Store& store, const QString& dest, int keep);

// Everything run() needs, as plain values so it can run on any thread.
struct Job {
    QString dbPath;    // the live database, opened read-only on the job's own connection
    QString installId;
    QString dest;
    int keep = 14;
    qint64 nowMs = 0;  // names the snapshot and ages out stale temp files
};

struct Result {
    QString path;      // the finished snapshot
    qint64 bytes = 0;
    int pruned = 0;    // snapshots removed
};

// Snapshot, verify, prune: thread-agnostic and never touches a Store. Throws BackupDirMissing
// like snapshot(), Cancelled once `cancel` turns true (no .db or .tmp is left behind), and
// BackupInvalid, in which case the bad snapshot is deleted.
Result run(const Job& job, const std::atomic<bool>* cancel = nullptr);

// Throws BackupInvalid unless the snapshot passes integrity_check and every attachment it
// references is stored in it.
void verify(const QString& snapshotDb);

} // namespace backup
} // namespace noite
