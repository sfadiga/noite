#pragma once

// Backups to a folder the user picks, e.g. their Google Drive for Desktop folder (D13).
// Layout under <dest>, one subfolder per installation so several PCs or portable folders
// can share one backup folder without pruning each other:
//   noite-backups/<installId>/snapshots/noite-YYYYMMDDTHHMMSSzzzZ.db   VACUUM INTO copies
//   noite-backups/<installId>/attachments/<id>                         copied once

#include <QString>

namespace noite {

class Store;

namespace backup {

struct PruneReport {
    int snapshotsDeleted{};
    int attachmentsDeleted{};
};

// Writes a snapshot plus any attachments it references that the backup lacks. The .db
// appears under its final name last, so a visible snapshot always has its attachments.
// `dest` must exist (it is never created): throws BackupDirMissing otherwise.
QString snapshot(Store& store, const QString& dest);

// For this installation only: keeps the `keep` newest snapshots, removes temp files older
// than an hour (a younger one may be a snapshot in progress), then deletes backup
// attachments no remaining snapshot references.
PruneReport prune(const Store& store, const QString& dest, int keep);

// Throws BackupInvalid unless the snapshot passes integrity_check and every attachment it
// references exists in the backup's attachments folder.
void verify(const QString& snapshotDb);

} // namespace backup
} // namespace noite
