#include <noite/store.h>

#include "db.h"
#include "store_internal.h"

#include <noite/content.h>
#include <noite/error.h>

#include <QCryptographicHash>
#include <QRegularExpression>
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

    Transaction tx(db());
    Stmt insert = db().prepare("INSERT OR IGNORE INTO attachments(id, data, size, touched_at) VALUES (?, ?, ?, ?)");
    insert.bind(1, id).bindBlob(2, bytes).bind(3, qint64(bytes.size())).bind(4, nowMs());
    insert.step();
    // A re-paste of existing bytes restarts the grace period too: the note that will
    // reference it may not be saved yet.
    Stmt touch = db().prepare("UPDATE attachments SET touched_at = ? WHERE id = ?");
    touch.bind(1, nowMs()).bind(2, id);
    touch.step();
    tx.commit();
    return id;
}

QByteArray Store::attachmentBytes(const QString& id) const
{
    if (!content::isValidAttachmentId(id))
        throw Error(ErrorCode::InvalidAttachmentId, "invalid attachment id: " + id);
    Stmt s = db().prepare("SELECT data FROM attachments WHERE id = ?");
    s.bind(1, id);
    if (!s.step())
        throw Error(ErrorCode::AttachmentMissing, "attachment not found: " + id);
    return s.blob(0);
}

PurgeReport Store::purgeTrash(int retentionDays)
{
    if (retentionDays < 1 || retentionDays > 365)
        throw Error(ErrorCode::InvalidSetting, "trash retention must be 1-365 days");
    const qint64 now = nowMs();
    PurgeReport report;

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

    // Mark: every attachment any remaining scratch references (any status).
    QSet<QString> referenced;
    Stmt all = db().prepare("SELECT content FROM scratches");
    while (all.step())
        for (const QString& id : content::attachmentRefs(all.text(0)))
            referenced.insert(id);

    // Sweep: unreferenced rows past the grace period.
    Stmt candidates = db().prepare("SELECT id FROM attachments WHERE touched_at <= ?");
    candidates.bind(1, now - kAttachmentGraceMs);
    QStringList sweep;
    while (candidates.step())
        if (const QString id = candidates.text(0); !referenced.contains(id))
            sweep << id;
    for (const QString& id : sweep) {
        Stmt del = db().prepare("DELETE FROM attachments WHERE id = ?");
        del.bind(1, id);
        del.step();
    }
    report.attachmentsDeleted = int(sweep.size());
    tx.commit();

    if (!sweep.isEmpty())
        db().exec("PRAGMA incremental_vacuum");
    return report;
}

} // namespace noite
