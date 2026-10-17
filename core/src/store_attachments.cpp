#include <noite/store.h>

#include "db.h"
#include "store_internal.h"

#include <noite/content.h>
#include <noite/error.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace noite {

using namespace detail;

QString Store::putAttachment(const QByteArray& bytes, const QString& ext)
{
    if (bytes.size() > kMaxAttachmentBytes)
        throw Error(ErrorCode::AttachmentTooLarge,
                    QString("attachment is %1 bytes; limit is %2").arg(bytes.size()).arg(kMaxAttachmentBytes));

    static const QRegularExpression validExt("^[a-z0-9]{1,8}$");
    QString suffix = ext.toLower();
    if (!validExt.match(suffix).hasMatch())
        suffix = "bin";
    const QString id = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) + "." + suffix;

    const QString path = attachmentPath(id);
    if (QFileInfo::exists(path))
        return id;
    QSaveFile file(path); // writes a temp file, renamed into place on commit()
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw Error(ErrorCode::Io, "cannot write attachment " + path + ": " + file.errorString());
    return id;
}

QString Store::attachmentPath(const QString& id) const
{
    if (!content::isValidAttachmentId(id))
        throw Error(ErrorCode::InvalidAttachmentId, "invalid attachment id: " + id);
    return dataDir().attachmentsDir() + "/" + id;
}

PurgeReport Store::purgeTrash(int retentionDays)
{
    if (retentionDays < 1 || retentionDays > 365)
        throw Error(ErrorCode::InvalidSetting, "trash retention must be 1-365 days");
    const qint64 now = nowMs();
    PurgeReport report;

    {
        Transaction tx(db());
        Stmt expired = db().prepare("SELECT id FROM scratches WHERE status = 'trashed' AND trashed_at <= ?");
        expired.bind(1, now - retentionDays * kDayMs);
        QStringList ids;
        while (expired.step())
            ids << expired.text(0);
        for (const QString& id : ids) {
            for (const char* sql : {"DELETE FROM scratch_fts WHERE id = ?", "DELETE FROM scratches WHERE id = ?"}) {
                Stmt del = db().prepare(sql);
                del.bind(1, id);
                del.step();
            }
        }
        report.scratchesDeleted = int(ids.size());
        deleteEmptyBundles(db());
        tx.commit();
    }

    // Mark: every attachment any remaining scratch references (any status).
    QSet<QString> referenced;
    Stmt all = db().prepare("SELECT content FROM scratches");
    while (all.step())
        for (const QString& id : content::attachmentRefs(all.text(0)))
            referenced.insert(id);

    // Sweep: unreferenced attachments and leftover temp files, if older than the grace period.
    const QDir dir(dataDir().attachmentsDir());
    for (const QFileInfo& file : dir.entryInfoList(QDir::Files)) {
        if (file.lastModified().toMSecsSinceEpoch() > now - kAttachmentGraceMs)
            continue;
        const bool isAttachment = content::isValidAttachmentId(file.fileName());
        if (isAttachment && referenced.contains(file.fileName()))
            continue;
        if (QFile::remove(file.absoluteFilePath()) && isAttachment)
            ++report.attachmentsDeleted;
    }
    return report;
}

} // namespace noite
