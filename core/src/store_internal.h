#pragma once

// Helpers shared by the store_*.cpp files. Internal to noite-core.

#include <noite/model.h>

#include <QHash>

#include <memory>
#include <optional>

class QTextDocument;

namespace noite {
class Store;
}

namespace noite::detail {

class Db;
class Stmt;

// The columns summaryFromRow reads, in this order.
inline constexpr const char* kSummaryColumns =
    "id, title, plain_text, first_image, color, due_date, bundle_id, status, created_at, done_at, trashed_at";
// Every scratch's tags (inline and manual), keyed by scratch id.
QHash<QString, QStringList> allTags(Db& db);
// A board summary from a row selected with kSummaryColumns first.
ScratchSummary summaryFromRow(const Stmt& s, const QHash<QString, QStringList>& tags);

// Rewrites everything derived from `html` in one place: content, plain text, title,
// first image, inline tag rows and the full-text index row.
void writeDerived(Db& db, const ScratchId& id, const QString& html);

// The scratch's status, or throws NotFound.
Status requireStatus(Db& db, const ScratchId& id);

// Throws InvalidTransition if the scratch is trashed (trashed scratches are read-only).
void requireEditable(Db& db, const ScratchId& id);

void touch(Db& db, const ScratchId& id, qint64 nowMs);

// The note as exported (M8a): unreadable images replaced by "[missing image]", the rest
// added as resources, light default font. With `textWidth` > 0, images wider than it get
// that width (aspect kept).
std::unique_ptr<QTextDocument> exportDocument(const Store& store, const QString& html, qreal textWidth);

// Deletes bundles that no scratch (of any status) belongs to. Returns how many.
int deleteEmptyBundles(Db& db);

// Search-box text -> FTS5 query: each whitespace-separated term becomes a quoted prefix
// ("term"*), so no input can be FTS5 syntax. Blank input -> nullopt (no text filter).
std::optional<QString> ftsQuery(const QString& input);

} // namespace noite::detail
