#include <noite/store.h>

#include "db.h"
#include "store_internal.h"

#include <noite/error.h>

#include <QUuid>

namespace noite {

namespace detail {

int deleteEmptyBundles(Db& db)
{
    db.exec("DELETE FROM bundles WHERE id NOT IN "
            "(SELECT bundle_id FROM scratches WHERE bundle_id IS NOT NULL)");
    return db.changes();
}

} // namespace detail

using namespace detail;

namespace {

std::optional<BundleId> bundleOf(Db& db, const ScratchId& id)
{
    Stmt s = db.prepare("SELECT bundle_id FROM scratches WHERE id = ?");
    s.bind(1, id);
    s.step();
    return s.optText(0);
}

void setBundle(Db& db, const ScratchId& id, const std::optional<BundleId>& bundle, qint64 now)
{
    Stmt s = db.prepare("UPDATE scratches SET bundle_id = ?, modified_at = ? WHERE id = ?");
    s.bind(1, bundle).bind(2, now).bind(3, id);
    s.step();
}

} // namespace

BundleId Store::bundleTogether(const ScratchId& target, const ScratchId& dragged)
{
    if (target == dragged)
        throw Error(ErrorCode::InvalidTransition, "cannot bundle a scratch with itself");
    Transaction tx(db());
    requireEditable(db(), target);
    requireEditable(db(), dragged);

    BundleId bundle;
    if (const auto existing = bundleOf(db(), target)) {
        bundle = *existing;
    } else {
        bundle = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
        QString name = get(target).title;
        Stmt insert = db().prepare("INSERT INTO bundles(id, name, created_at) VALUES (?, ?, ?)");
        insert.bind(1, bundle).bind(2, name.isEmpty() ? QString("Bundle") : name).bind(3, nowMs());
        insert.step();
        setBundle(db(), target, bundle, nowMs());
    }
    setBundle(db(), dragged, bundle, nowMs());
    deleteEmptyBundles(db());
    tx.commit();
    return bundle;
}

void Store::moveToBundle(const ScratchId& id, std::optional<BundleId> bundle)
{
    Transaction tx(db());
    requireEditable(db(), id);
    if (bundle) {
        Stmt exists = db().prepare("SELECT 1 FROM bundles WHERE id = ?");
        exists.bind(1, *bundle);
        if (!exists.step())
            throw Error(ErrorCode::NotFound, "bundle " + *bundle + " not found");
    }
    setBundle(db(), id, bundle, nowMs());
    deleteEmptyBundles(db());
    tx.commit();
}

void Store::renameBundle(const BundleId& id, const QString& name)
{
    Stmt s = db().prepare("UPDATE bundles SET name = ? WHERE id = ?");
    s.bind(1, name).bind(2, id);
    s.step();
    if (db().changes() == 0)
        throw Error(ErrorCode::NotFound, "bundle " + id + " not found");
}

void Store::recreateBundle(const Bundle& bundle)
{
    Stmt s = db().prepare("INSERT OR IGNORE INTO bundles(id, name, created_at) VALUES (?, ?, ?)");
    s.bind(1, bundle.id).bind(2, bundle.name).bind(3, bundle.createdAt);
    s.step();
}

QStringList Store::dissolveBundle(const BundleId& id)
{
    Transaction tx(db());
    Stmt exists = db().prepare("SELECT 1 FROM bundles WHERE id = ?");
    exists.bind(1, id);
    if (!exists.step())
        throw Error(ErrorCode::NotFound, "bundle " + id + " not found");
    Stmt members = db().prepare("SELECT id FROM scratches WHERE bundle_id = ?");
    members.bind(1, id);
    QStringList released;
    while (members.step())
        released << members.text(0);
    for (const QString& member : released)
        setBundle(db(), member, std::nullopt, nowMs());
    deleteEmptyBundles(db());
    tx.commit();
    return released;
}

QList<Bundle> Store::listBundles() const
{
    Stmt s = db().prepare("SELECT id, name, created_at FROM bundles ORDER BY created_at, id");
    QList<Bundle> bundles;
    while (s.step())
        bundles.append({s.text(0), s.text(1), s.int64(2)});
    return bundles;
}

Scratch Store::cloneScratch(const ScratchId& id)
{
    Transaction tx(db());
    requireEditable(db(), id);
    const Scratch source = get(id);
    const ScratchId copy = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    const qint64 now = nowMs();

    Stmt insert = db().prepare(
        "INSERT INTO scratches(id, content, plain_text, title, created_at, modified_at, color, bundle_id, status) "
        "VALUES (?, '', '', '', ?, ?, ?, ?, 'active')");
    insert.bind(1, copy).bind(2, now).bind(3, now);
    insert.bind(4, source.color ? std::optional<QString>(colorName(*source.color)) : std::nullopt);
    insert.bind(5, source.bundleId);
    insert.step();
    writeDerived(db(), copy, source.html);

    Stmt tag = db().prepare("INSERT INTO scratch_tags(scratch_id, tag, source) VALUES (?, ?, 'manual')");
    for (const QString& manual : source.manualTags) {
        tag.bind(1, copy).bind(2, manual);
        tag.step();
        tag.reset();
    }
    tx.commit();
    return get(copy);
}

} // namespace noite
