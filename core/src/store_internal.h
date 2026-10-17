#pragma once

// Helpers shared by the store_*.cpp files. Internal to noite-core.

#include <noite/model.h>

namespace noite::detail {

class Db;

// Rewrites everything derived from `html` in one place: content, plain text, title,
// first image, inline tag rows and the full-text index row.
void writeDerived(Db& db, const ScratchId& id, const QString& html);

// The scratch's status, or throws NotFound.
Status requireStatus(Db& db, const ScratchId& id);

// Throws InvalidTransition if the scratch is trashed (trashed scratches are read-only).
void requireEditable(Db& db, const ScratchId& id);

void touch(Db& db, const ScratchId& id, qint64 nowMs);

// Deletes bundles that no scratch (of any status) belongs to. Returns how many.
int deleteEmptyBundles(Db& db);

} // namespace noite::detail
