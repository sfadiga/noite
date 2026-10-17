#include <noite/data_dir.h>

#include <noite/error.h>

#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>

namespace noite {

namespace {

QStringList components(const QString& path)
{
    static const QRegularExpression separators(R"([\\/]+)");
    QStringList parts = path.toLower().split(separators, Qt::SkipEmptyParts);
    return parts;
}

bool startsWithComponents(const QStringList& path, const QStringList& prefix)
{
    if (prefix.isEmpty() || prefix.size() > path.size())
        return false;
    for (qsizetype i = 0; i < prefix.size(); ++i)
        if (path[i] != prefix[i])
            return false;
    return true;
}

} // namespace

std::optional<SyncProvider> syncProviderFor(const QString& path, const QStringList& oneDriveRoots)
{
    const QStringList parts = components(path);
    for (const QString& root : oneDriveRoots)
        if (startsWithComponents(parts, components(root)))
            return SyncProvider::OneDrive;
    if (parts.contains("my drive") || parts.contains("google drive"))
        return SyncProvider::GoogleDrive;
    if (parts.contains("dropbox"))
        return SyncProvider::Dropbox;
    return std::nullopt;
}

DataDir DataDir::acquire(const QString& root)
{
    const QString clean = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    for (const QString& sub : {QString(), QString("/attachments"), QString("/logs"), QString("/migration-backups")})
        if (!QDir().mkpath(clean + sub))
            throw Error(ErrorCode::Io, "cannot create " + clean + sub);

    auto lock = std::make_unique<QLockFile>(clean + "/noite.lock");
    lock->setStaleLockTime(0); // no time-based staleness; a dead owner's lock is still reclaimed
    if (!lock->tryLock(0))
        throw Error(ErrorCode::AlreadyRunning, "another noite instance is using " + clean);
    return DataDir(clean, std::move(lock));
}

DataDir::DataDir(QString root, std::unique_ptr<QLockFile> lock)
    : root_(std::move(root))
    , lock_(std::move(lock))
{
}

DataDir::DataDir(DataDir&&) noexcept = default;
DataDir& DataDir::operator=(DataDir&&) noexcept = default;
DataDir::~DataDir() = default;

std::optional<SyncProvider> DataDir::syncWarning() const
{
    QStringList roots;
    for (const char* var : {"OneDrive", "OneDriveConsumer", "OneDriveCommercial"})
        if (const QString value = qEnvironmentVariable(var); !value.isEmpty())
            roots << value;
    return syncProviderFor(root_, roots);
}

} // namespace noite
