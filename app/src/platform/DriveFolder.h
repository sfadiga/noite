#pragma once

#include <QList>
#include <QString>

#include <functional>
#include <optional>

namespace noite {

struct DriveVolume {
    QString root;  // "G:/"
    QString label; // "Google Drive"
};

// Google Drive for Desktop mounts a volume labelled "Google Drive": its "my drive" folder
// (first of googleDriveFolderNames() that exists); nullopt when there is none (the caller then
// opens the folder picker). `/` separators.
std::optional<QString> suggestedDriveFolder(const QList<DriveVolume>& volumes,
                                            const std::function<bool(const QString&)>& dirExists);

// The ready, valid mounted volumes.
QList<DriveVolume> mountedDriveVolumes();

} // namespace noite
