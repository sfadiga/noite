#pragma once

// Backups to a folder the user picks, e.g. their Google Drive for Desktop folder (D13).
// Layout under <dest>:
//   noite-backups/snapshots/noite-YYYYMMDDTHHMMSSzzzZ.db   consistent VACUUM INTO copies
//   noite-backups/attachments/<id>                         shared, copied once

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

// Keeps the `keep` newest snapshots, removes leftover temp files, then deletes backup
// attachments no remaining snapshot references.
PruneReport prune(const QString& dest, int keep);

// Throws BackupInvalid unless the snapshot passes integrity_check and every attachment it
// references exists in the backup's attachments folder.
void verify(const QString& snapshotDb);

} // namespace backup
} // namespace noite
