#pragma once

#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

class QLockFile;

namespace noite {

enum class SyncProvider { GoogleDrive, OneDrive, Dropbox };

// Which file-sync service, if any, owns `path` (the live database must never be synced, D13).
std::optional<SyncProvider> syncProviderFor(const QString& path, const QStringList& oneDriveRoots,
                                            const QString& volumeLabel = {});

// The portable data folder (D4), locked for the lifetime of this object so only one
// noite instance writes to it (D27). Move-only.
class DataDir {
public:
    // Creates the folder layout and takes the lock; throws ErrorCode::AlreadyRunning if
    // another live process (or another DataDir in this one) holds it.
    static DataDir acquire(const QString& root);

    DataDir(DataDir&&) noexcept;
    DataDir& operator=(DataDir&&) noexcept;
    DataDir(const DataDir&) = delete;
    DataDir& operator=(const DataDir&) = delete;
    ~DataDir();

    QString root() const { return root_; }
    QString dbPath() const { return root_ + "/noite.db"; }
    QString attachmentsDir() const { return root_ + "/attachments"; }
    QString logsDir() const { return root_ + "/logs"; }
    QString migrationBackupsDir() const { return root_ + "/migration-backups"; }

    // Checks the root against Google Drive / Dropbox folder names and the OneDrive roots
    // from the OneDrive, OneDriveConsumer and OneDriveCommercial environment variables.
    std::optional<SyncProvider> syncWarning() const;

private:
    DataDir(QString root, std::unique_ptr<QLockFile> lock);

    QString root_;
    std::unique_ptr<QLockFile> lock_;
};

} // namespace noite
