#include <noite/backup.h>

#include "db.h"

#include <noite/content.h>
#include <noite/error.h>
#include <noite/store.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTimeZone>

namespace noite::backup {

namespace {

QString snapshotsDir(const QString& dest) { return dest + "/noite-backups/snapshots"; }
QString attachmentsDir(const QString& dest) { return dest + "/noite-backups/attachments"; }

QStringList referencedBy(const QString& snapshotDb)
{
    detail::Db db = detail::Db::open(snapshotDb, true);
    detail::Stmt s = db.prepare("SELECT content FROM scratches");
    QStringList refs;
    while (s.step())
        refs << content::attachmentRefs(s.text(0));
    refs.removeDuplicates();
    return refs;
}

void copyAtomically(const QString& from, const QString& to)
{
    const QString tmp = to + ".tmp";
    QFile::remove(tmp);
    if (!QFile::copy(from, tmp) || !QFile::rename(tmp, to))
        throw Error(ErrorCode::Io, "cannot copy " + from + " to " + to);
}

} // namespace

QString snapshot(Store& store, const QString& dest)
{
    if (!QFileInfo(dest).isDir())
        throw Error(ErrorCode::BackupDirMissing, "backup folder not found: " + dest);
    const QString snaps = snapshotsDir(dest);
    const QString atts = attachmentsDir(dest);
    if (!QDir().mkpath(snaps) || !QDir().mkpath(atts))
        throw Error(ErrorCode::Io, "cannot create backup folders in " + dest);

    const QString stamp = QDateTime::fromMSecsSinceEpoch(store.nowMs(), QTimeZone::UTC).toString("yyyyMMdd'T'HHmmsszzz'Z'");
    const QString final = snaps + "/noite-" + stamp + ".db";
    const QString tmp = final + ".tmp";
    QFile::remove(tmp);
    detail::vacuumInto(store.db(), tmp);

    for (const QString& id : referencedBy(tmp)) {
        const QString target = atts + "/" + id;
        const QString source = store.attachmentPath(id);
        if (!QFileInfo::exists(target) && QFileInfo::exists(source))
            copyAtomically(source, target);
    }
    if (!QFile::rename(tmp, final))
        throw Error(ErrorCode::Io, "cannot finish snapshot " + final);
    return final;
}

PruneReport prune(const QString& dest, int keep)
{
    if (keep < 1)
        throw Error(ErrorCode::InvalidSetting, "backups to keep must be at least 1");
    PruneReport report;
    QDir snaps(snapshotsDir(dest));
    QDir atts(attachmentsDir(dest));
    if (!snaps.exists())
        return report;

    for (QDir* dir : {&snaps, &atts})
        for (const QString& name : dir->entryList({"*.tmp"}, QDir::Files))
            dir->remove(name);

    QStringList dbs = snaps.entryList({"*.db"}, QDir::Files, QDir::Name); // names sort by time
    while (dbs.size() > keep) {
        if (snaps.remove(dbs.takeFirst()))
            ++report.snapshotsDeleted;
    }

    QSet<QString> referenced;
    for (const QString& name : dbs)
        for (const QString& id : referencedBy(snaps.filePath(name)))
            referenced.insert(id);
    for (const QString& name : atts.entryList(QDir::Files)) {
        if (!referenced.contains(name) && atts.remove(name))
            ++report.attachmentsDeleted;
    }
    return report;
}

void verify(const QString& snapshotDb)
{
    QStringList refs;
    try {
        detail::Db db = detail::Db::open(snapshotDb, true);
        detail::Stmt check = db.prepare("PRAGMA integrity_check");
        check.step();
        if (const QString result = check.text(0); result != "ok")
            throw Error(ErrorCode::BackupInvalid, "integrity check failed: " + result);
        refs = referencedBy(snapshotDb);
    } catch (const Error& e) {
        if (e.code() == ErrorCode::BackupInvalid)
            throw;
        throw Error(ErrorCode::BackupInvalid, QString("cannot read snapshot: ") + e.what());
    }
    const QString atts = QFileInfo(snapshotDb).absoluteDir().absoluteFilePath("../attachments");
    for (const QString& id : refs)
        if (!QFileInfo::exists(atts + "/" + id))
            throw Error(ErrorCode::BackupInvalid, "snapshot references missing attachment " + id);
}

} // namespace noite::backup
