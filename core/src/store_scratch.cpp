#include <noite/store.h>

#include "db.h"
#include "store_internal.h"

#include <noite/content.h>
#include <noite/error.h>

#include <QUuid>

#include <algorithm>

namespace noite {

namespace detail {

void writeDerived(Db& db, const ScratchId& id, const QString& html)
{
    const QString plain = content::plainText(html);
    Stmt update = db.prepare(
        "UPDATE scratches SET content = ?, plain_text = ?, title = ?, first_image = ? WHERE id = ?");
    update.bind(1, html).bind(2, plain).bind(3, content::deriveTitle(plain)).bind(4, content::firstImageRef(html)).bind(5, id);
    update.step();

    Stmt clear = db.prepare("DELETE FROM scratch_tags WHERE scratch_id = ? AND source = 'inline'");
    clear.bind(1, id);
    clear.step();
    Stmt insertTag = db.prepare("INSERT INTO scratch_tags(scratch_id, tag, source) VALUES (?, ?, 'inline')");
    for (const QString& tag : content::inlineTags(plain)) {
        insertTag.bind(1, id).bind(2, tag);
        insertTag.step();
        insertTag.reset();
    }

    Stmt clearFts = db.prepare("DELETE FROM scratch_fts WHERE id = ?");
    clearFts.bind(1, id);
    clearFts.step();
    Stmt insertFts = db.prepare("INSERT INTO scratch_fts(id, body) VALUES (?, ?)");
    insertFts.bind(1, id).bind(2, plain);
    insertFts.step();
}

Status requireStatus(Db& db, const ScratchId& id)
{
    Stmt s = db.prepare("SELECT status FROM scratches WHERE id = ?");
    s.bind(1, id);
    if (!s.step())
        throw Error(ErrorCode::NotFound, "scratch " + id + " not found");
    return parseStatus(s.text(0)).value();
}

void requireEditable(Db& db, const ScratchId& id)
{
    if (requireStatus(db, id) == Status::Trashed)
        throw Error(ErrorCode::InvalidTransition, "trashed scratches are read-only");
}

void touch(Db& db, const ScratchId& id, qint64 nowMs)
{
    Stmt s = db.prepare("UPDATE scratches SET modified_at = ? WHERE id = ?");
    s.bind(1, nowMs).bind(2, id);
    s.step();
}

} // namespace detail

using namespace detail;

Scratch Store::createScratch(const QString& html)
{
    const ScratchId id = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    const qint64 now = nowMs();
    Transaction tx(db());
    Stmt insert = db().prepare(
        "INSERT INTO scratches(id, content, plain_text, title, created_at, modified_at, status) "
        "VALUES (?, '', '', '', ?, ?, 'active')");
    insert.bind(1, id).bind(2, now).bind(3, now);
    insert.step();
    writeDerived(db(), id, html);
    tx.commit();
    return get(id);
}

Scratch Store::updateContent(const ScratchId& id, const QString& html)
{
    Transaction tx(db());
    requireEditable(db(), id);
    writeDerived(db(), id, html);
    touch(db(), id, nowMs());
    tx.commit();
    return get(id);
}

Scratch Store::get(const ScratchId& id) const
{
    Stmt s = db().prepare(
        "SELECT content, title, created_at, modified_at, due_date, color, bundle_id, status, done_at, trashed_at "
        "FROM scratches WHERE id = ?");
    s.bind(1, id);
    if (!s.step())
        throw Error(ErrorCode::NotFound, "scratch " + id + " not found");

    Scratch scratch;
    scratch.id = id;
    scratch.html = s.text(0);
    scratch.title = s.text(1);
    scratch.createdAt = s.int64(2);
    scratch.modifiedAt = s.int64(3);
    if (const auto due = s.optText(4))
        scratch.due = QDate::fromString(*due, Qt::ISODate);
    if (const auto color = s.optText(5))
        scratch.color = parseColor(*color);
    scratch.bundleId = s.optText(6);
    scratch.status = parseStatus(s.text(7)).value();
    scratch.doneAt = s.optInt64(8);
    scratch.trashedAt = s.optInt64(9);

    Stmt tags = db().prepare("SELECT tag, source FROM scratch_tags WHERE scratch_id = ? ORDER BY tag");
    tags.bind(1, id);
    while (tags.step()) {
        const QString tag = tags.text(0);
        if (!scratch.tags.contains(tag))
            scratch.tags << tag;
        if (tags.text(1) == "manual")
            scratch.manualTags << tag;
    }
    return scratch;
}

bool Store::discardIfEmpty(const ScratchId& id)
{
    Transaction tx(db());
    requireStatus(db(), id);
    Stmt read = db().prepare("SELECT content FROM scratches WHERE id = ?");
    read.bind(1, id);
    read.step();
    if (!content::isEmpty(read.text(0)))
        return false;
    for (const char* sql : {"DELETE FROM scratch_fts WHERE id = ?", "DELETE FROM scratches WHERE id = ?"}) {
        Stmt del = db().prepare(sql);
        del.bind(1, id);
        del.step();
    }
    tx.commit();
    return true;
}

void Store::setDue(const ScratchId& id, std::optional<QDate> due)
{
    Transaction tx(db());
    requireEditable(db(), id);
    Stmt s = db().prepare("UPDATE scratches SET due_date = ?, modified_at = ? WHERE id = ?");
    s.bind(1, due ? std::optional<QString>(due->toString(Qt::ISODate)) : std::nullopt).bind(2, nowMs()).bind(3, id);
    s.step();
    tx.commit();
}

void Store::setColor(const ScratchId& id, std::optional<Color> color)
{
    Transaction tx(db());
    requireEditable(db(), id);
    Stmt s = db().prepare("UPDATE scratches SET color = ?, modified_at = ? WHERE id = ?");
    s.bind(1, color ? std::optional<QString>(colorName(*color)) : std::nullopt).bind(2, nowMs()).bind(3, id);
    s.step();
    tx.commit();
}

void Store::setManualTags(const ScratchId& id, const QStringList& tags)
{
    QStringList normalized;
    for (const QString& raw : tags) {
        const auto tag = content::normalizeTag(raw);
        if (!tag)
            throw Error(ErrorCode::InvalidTag, "invalid tag: " + raw);
        if (!normalized.contains(*tag))
            normalized << *tag;
    }

    Transaction tx(db());
    requireEditable(db(), id);
    Stmt clear = db().prepare("DELETE FROM scratch_tags WHERE scratch_id = ? AND source = 'manual'");
    clear.bind(1, id);
    clear.step();
    Stmt insert = db().prepare("INSERT INTO scratch_tags(scratch_id, tag, source) VALUES (?, ?, 'manual')");
    for (const QString& tag : normalized) {
        insert.bind(1, id).bind(2, tag);
        insert.step();
        insert.reset();
    }
    touch(db(), id, nowMs());
    tx.commit();
}

} // namespace noite
