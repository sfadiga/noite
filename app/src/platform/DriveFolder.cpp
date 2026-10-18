#include "platform/DriveFolder.h"

#include <noite/data_dir.h>

#include <QStorageInfo>

namespace noite {

std::optional<QString> suggestedDriveFolder(const QList<DriveVolume>& volumes,
                                            const std::function<bool(const QString&)>& dirExists)
{
    for (const DriveVolume& volume : volumes) {
        if (volume.label.compare("Google Drive", Qt::CaseInsensitive) != 0)
            continue;
        QString root = volume.root;
        if (!root.endsWith('/'))
            root += '/';
        for (const QString& name : googleDriveFolderNames()) {
            const QString candidate = root + name;
            if (dirExists(candidate))
                return candidate;
        }
    }
    return std::nullopt;
}

QList<DriveVolume> mountedDriveVolumes()
{
    QList<DriveVolume> volumes;
    for (const QStorageInfo& info : QStorageInfo::mountedVolumes()) {
        if (info.isValid() && info.isReady())
            volumes.append({info.rootPath(), info.name()});
    }
    return volumes;
}

} // namespace noite
