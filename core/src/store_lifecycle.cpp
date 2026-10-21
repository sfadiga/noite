#include <noite/store.h>

#include "db.h"
#include "store_internal.h"

#include <noite/error.h>

namespace noite {

namespace {

// Runs a guarded UPDATE (its WHERE clause names the allowed source statuses). No row
// changed means the scratch is missing (NotFound) or in the wrong status (InvalidTransition).
void transition(detail::Db& db, const ScratchId& id, qint64 now, const char* sql, const char* what)
{
    detail::Transaction tx(db);
    detail::Stmt s = db.prepare(sql);
    s.bind(1, now).bind(2, id);
    s.step();
    if (db.changes() == 0) {
        detail::requireStatus(db, id);
        throw Error(ErrorCode::InvalidTransition, QString("cannot %1 scratch %2 in its current status").arg(what, id));
    }
    tx.commit();
}

} // namespace

void Store::markDone(const ScratchId& id)
{
    transition(db(), id, nowMs(),
               "UPDATE scratches SET status = 'done', done_at = ?1, modified_at = ?1 "
               "WHERE id = ?2 AND status IN ('active', 'deferred')",
               "mark done");
}

void Store::reopen(const ScratchId& id)
{
    transition(db(), id, nowMs(),
               "UPDATE scratches SET status = 'active', done_at = NULL, modified_at = ?1 "
               "WHERE id = ?2 AND status = 'done'",
               "reopen");
}

void Store::trash(const ScratchId& id)
{
    transition(db(), id, nowMs(),
               "UPDATE scratches SET trashed_from = status, status = 'trashed', trashed_at = ?1, modified_at = ?1 "
               "WHERE id = ?2 AND status IN ('active', 'done', 'deferred')",
               "trash");
}

void Store::restore(const ScratchId& id)
{
    transition(db(), id, nowMs(),
               "UPDATE scratches SET status = trashed_from, trashed_from = NULL, trashed_at = NULL, modified_at = ?1 "
               "WHERE id = ?2 AND status = 'trashed'",
               "restore");
}

void Store::defer(const ScratchId& id)
{
    transition(db(), id, nowMs(),
               "UPDATE scratches SET status = 'deferred', modified_at = ?1 WHERE id = ?2 AND status = 'active'",
               "defer");
}

void Store::bringBack(const ScratchId& id)
{
    transition(db(), id, nowMs(),
               "UPDATE scratches SET status = 'active', modified_at = ?1 WHERE id = ?2 AND status = 'deferred'",
               "bring back");
}

} // namespace noite
