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

#include <sqlite3.h>

namespace noite::backup {

namespace {

constexpr qint64 kStaleTempMs = 3'600'000;

QString snapshotsDir(const QString& installId, const QString& dest)
{
    return dest + "/noite-backups/" + installId + "/snapshots";
}

// Checks `dest`, creates the snapshots folder and returns the final name for a snapshot taken
// at `nowMs`.
QString prepareSnapshotPath(const QString& installId, const QString& dest, qint64 nowMs)
{
    if (!QFileInfo(dest).isDir())
        throw Error(ErrorCode::BackupDirMissing, "backup folder not found: " + dest);
    const QString snaps = snapshotsDir(installId, dest);
    if (!QDir().mkpath(snaps))
        throw Error(ErrorCode::Io, "cannot create backup folders in " + dest);
    const QString stamp = QDateTime::fromMSecsSinceEpoch(nowMs, QTimeZone::UTC).toString("yyyyMMdd'T'HHmmsszzz'Z'");
    return snaps + "/noite-" + stamp + ".db";
}

int pruneSnapshots(const QString& installId, const QString& dest, int keep, qint64 nowMs)
{
    if (keep < 1)
        throw Error(ErrorCode::InvalidSetting, "backups to keep must be at least 1");
    int deleted = 0;
    QDir snaps(snapshotsDir(installId, dest));
    if (!snaps.exists())
        return deleted;

    for (const QFileInfo& tmp : snaps.entryInfoList({"*.tmp"}, QDir::Files))
        if (tmp.lastModified().toMSecsSinceEpoch() <= nowMs - kStaleTempMs)
            snaps.remove(tmp.fileName());

    QStringList dbs = snaps.entryList({"*.db"}, QDir::Files, QDir::Name); // names sort by time
    while (dbs.size() > keep) {
        if (snaps.remove(dbs.takeFirst()))
            ++deleted;
    }
    return deleted;
}

int cancelled(void* flag)
{
    return static_cast<const std::atomic<bool>*>(flag)->load() ? 1 : 0;
}

} // namespace

QString snapshot(Store& store, const QString& dest)
{
    const QString final = prepareSnapshotPath(store.installId(), dest, store.nowMs());
    const QString tmp = final + ".tmp";
    QFile::remove(tmp);
    detail::vacuumInto(store.db(), tmp);
    if (!QFile::rename(tmp, final))
        throw Error(ErrorCode::Io, "cannot finish snapshot " + final);
    return final;
}

PruneReport prune(const Store& store, const QString& dest, int keep)
{
    return {pruneSnapshots(store.installId(), dest, keep, store.nowMs())};
}

Result run(const Job& job, const std::atomic<bool>* cancel)
{
    if (job.keep < 1)
        throw Error(ErrorCode::InvalidSetting, "backups to keep must be at least 1");
    const QString final = prepareSnapshotPath(job.installId, job.dest, job.nowMs);
    const QString tmp = final + ".tmp";
    if (cancel && cancel->load())
        throw Error(ErrorCode::Cancelled, "backup cancelled before start");
    QFile::remove(tmp);
    try {
        detail::Db db = detail::Db::open(job.dbPath, true);
        if (cancel)
            sqlite3_progress_handler(db.handle(), 1000, &cancelled, const_cast<std::atomic<bool>*>(cancel));
        try {
            detail::vacuumInto(db, tmp);
        } catch (const Error&) {
            if (sqlite3_errcode(db.handle()) == SQLITE_INTERRUPT)
                throw Error(ErrorCode::Cancelled, "backup cancelled during snapshot");
            throw;
        }
    } catch (...) {
        QFile::remove(tmp);
        throw;
    }
    if (!QFile::rename(tmp, final)) {
        QFile::remove(tmp);
        throw Error(ErrorCode::Io, "cannot finish snapshot " + final);
    }

    if (cancel && cancel->load()) { // a quit should not wait for the integrity check
        QFile::remove(final);
        throw Error(ErrorCode::Cancelled, "backup cancelled before verify");
    }
    try {
        verify(final);
    } catch (const Error& e) {
        if (e.code() == ErrorCode::BackupInvalid)
            QFile::remove(final);
        throw;
    }

    Result result;
    result.path = final;
    result.bytes = QFileInfo(final).size();
    result.pruned = pruneSnapshots(job.installId, job.dest, job.keep, job.nowMs);
    return result;
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
