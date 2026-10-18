#include <noite/backup.h>

#include "db.h"

#include <noite/content.h>
#include <noite/error.h>
#include <noite/store.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimeZone>

namespace noite::backup {

namespace {

constexpr qint64 kStaleTempMs = 3'600'000;

QString installDir(const Store& store, const QString& dest) { return dest + "/noite-backups/" + store.installId(); }
QString snapshotsDir(const Store& store, const QString& dest) { return installDir(store, dest) + "/snapshots"; }

} // namespace

QString snapshot(Store& store, const QString& dest)
{
    if (!QFileInfo(dest).isDir())
        throw Error(ErrorCode::BackupDirMissing, "backup folder not found: " + dest);
    const QString snaps = snapshotsDir(store, dest);
    if (!QDir().mkpath(snaps))
        throw Error(ErrorCode::Io, "cannot create backup folders in " + dest);

    const QString stamp = QDateTime::fromMSecsSinceEpoch(store.nowMs(), QTimeZone::UTC).toString("yyyyMMdd'T'HHmmsszzz'Z'");
    const QString final = snaps + "/noite-" + stamp + ".db";
    const QString tmp = final + ".tmp";
    QFile::remove(tmp);
    detail::vacuumInto(store.db(), tmp);
    if (!QFile::rename(tmp, final))
        throw Error(ErrorCode::Io, "cannot finish snapshot " + final);
    return final;
}

PruneReport prune(const Store& store, const QString& dest, int keep)
{
    if (keep < 1)
        throw Error(ErrorCode::InvalidSetting, "backups to keep must be at least 1");
    PruneReport report;
    QDir snaps(snapshotsDir(store, dest));
    if (!snaps.exists())
        return report;

    for (const QFileInfo& tmp : snaps.entryInfoList({"*.tmp"}, QDir::Files))
        if (tmp.lastModified().toMSecsSinceEpoch() <= store.nowMs() - kStaleTempMs)
            snaps.remove(tmp.fileName());

    QStringList dbs = snaps.entryList({"*.db"}, QDir::Files, QDir::Name); // names sort by time
    while (dbs.size() > keep) {
        if (snaps.remove(dbs.takeFirst()))
            ++report.snapshotsDeleted;
    }
    return report;
}

void verify(const QString& snapshotDb)
{
    try {
        detail::Db db = detail::Db::open(snapshotDb, true);
        detail::Stmt check = db.prepare("PRAGMA integrity_check");
        check.step();
        if (const QString result = check.text(0); result != "ok")
            throw Error(ErrorCode::BackupInvalid, "integrity check failed: " + result);
        detail::Stmt notes = db.prepare("SELECT content FROM scratches");
        detail::Stmt stored = db.prepare("SELECT 1 FROM attachments WHERE id = ?");
        while (notes.step()) {
            for (const QString& id : content::attachmentRefs(notes.text(0))) {
                stored.reset();
                stored.bind(1, id);
                if (!stored.step())
                    throw Error(ErrorCode::BackupInvalid, "snapshot references missing attachment " + id);
            }
        }
    } catch (const Error& e) {
        if (e.code() == ErrorCode::BackupInvalid)
            throw;
        throw Error(ErrorCode::BackupInvalid, QString("cannot read snapshot: ") + e.what());
    }
}

} // namespace noite::backup
