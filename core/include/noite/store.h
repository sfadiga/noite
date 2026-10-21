#pragma once

#include <noite/clock.h>
#include <noite/data_dir.h>
#include <noite/model.h>
#include <noite/settings.h>

#include <memory>

namespace noite {

namespace detail {
class Db;
}

inline constexpr qint64 kMaxAttachmentBytes = 26'214'400; // 25 MiB (D21)
inline constexpr qint64 kAttachmentGraceMs = kDayMs;      // purge never deletes rows touched more recently

// All of noite's persistent state. One Store per data dir; the only writer (D14).
// Non-const methods write; failures throw noite::Error.
class Store {
public:
    // Opens (creating if needed) <dir>/noite.db and migrates it to the latest schema.
    static std::unique_ptr<Store> open(DataDir dir, std::shared_ptr<const Clock> clock);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    int schemaVersion() const;
    const DataDir& dataDir() const { return dir_; }
    // Random id created on first open; separates this installation's backups from others'.
    QString installId() const;

    // ---- Scratches (store_scratch.cpp) ----
    Scratch createScratch(const QString& html);
    Scratch updateContent(const ScratchId& id, const QString& html);
    Scratch get(const ScratchId& id) const;
    // Hard-deletes the scratch if it has no text and no attachments (D6); never trashes.
    bool discardIfEmpty(const ScratchId& id);
    void setDue(const ScratchId& id, std::optional<QDate> due);
    void setColor(const ScratchId& id, std::optional<Color> color);
    // Throws InvalidTag (and changes nothing) if any tag is invalid.
    void setManualTags(const ScratchId& id, const QStringList& tags);

    // ---- Lifecycle (store_lifecycle.cpp, D11) ----
    // Active|Deferred -> Done; Done -> Active; Active|Done|Deferred -> Trashed; Trashed ->
    // previous status; Active -> Deferred -> Active (M8c). Anything else throws InvalidTransition.
    void markDone(const ScratchId& id);
    void reopen(const ScratchId& id);
    void trash(const ScratchId& id);
    void restore(const ScratchId& id);
    void defer(const ScratchId& id);
    void bringBack(const ScratchId& id);

    // ---- Bundles and clone (store_bundles.cpp, D12, D20) ----
    // Dropping `dragged` onto `target`: joins target's bundle, or creates one named after
    // target's title ("Bundle" if it has none). A bundle with no members is deleted.
    BundleId bundleTogether(const ScratchId& target, const ScratchId& dragged);
    void moveToBundle(const ScratchId& id, std::optional<BundleId> bundle);
    void renameBundle(const BundleId& id, const QString& name);
    QList<Bundle> listBundles() const;
    // Undo support: puts back a deleted bundle with its id, name and creation time (no-op if
    // it exists). It stays empty until a scratch moves in, which the same undo step must do.
    void recreateBundle(const Bundle& bundle);
    // Every member (any status) leaves and the bundle is deleted. Returns the members.
    // Throws NotFound for an unknown bundle.
    QStringList dissolveBundle(const BundleId& id);
    // Copies content, manual tags, colour and bundle; clears the due date; Active; new times.
    Scratch cloneScratch(const ScratchId& id);

    // ---- Board (store_board.cpp, D9, D17, D19) ----
    // Today: active, overdue -> due today -> undated (oldest first). All: Today's order, then
    // upcoming by due date. Completed: newest done first. Trash: newest trashed first.
    // `search` is matched as accent-insensitive word prefixes; blank means no filter.
    // `filter` applies the chips (D18): OR within a kind, AND across kinds.
    QList<ScratchSummary> list(Tab tab, QDate today, const QString& search = {}, const BoardFilter& filter = {}) const;
    // The size of list(tab, today, search, filter) for every tab, in one query.
    TabCounts tabCounts(QDate today, const QString& search = {}, const BoardFilter& filter = {}) const;
    // The chips to show for `tab`: each kind counted under the search and the *other* kinds'
    // chips, zero counts dropped, active chips always kept (C6).
    ChipFacets chipFacets(Tab tab, QDate today, const QString& search, const BoardFilter& filter) const;
    // Every tag in use, inline or manual, alphabetical.
    QStringList listTags() const;
    ReviewCounts reviewCounts(QDate today) const;

    // ---- Morning review (store_review.cpp, D31-D34, M4) ----
    // Active notes needing a decision: overdue (most overdue first), due today (oldest
    // first), then stale = undated and untouched for `staleDays` days (longest first).
    QList<ReviewItem> reviewQueue(QDate today, int staleDays) const;
    // "Keep": modified_at = now, which resets the stale clock. NotFound; trashed notes rejected.
    void touch(const ScratchId& id);
    // Undo of touch only: puts modified_at back. NotFound for an unknown id.
    void setModifiedAt(const ScratchId& id, qint64 ms);
    // The day the review was last shown; kept apart from Settings so saving settings never
    // overwrites it.
    std::optional<QDate> lastReviewDay() const;
    void setLastReviewDay(QDate day);

    // ---- Attachments and purge (store_attachments.cpp, D13, D22; in the database, A1) ----
    // Stores bytes under <sha256>.<ext> (ext lowercased; "bin" unless 1-8 alphanumerics),
    // deduplicated; a re-put restarts its grace period. Returns the id. Throws AttachmentTooLarge.
    QString putAttachment(const QByteArray& bytes, const QString& ext);
    // The stored bytes. Throws InvalidAttachmentId for a malformed id, AttachmentMissing if absent.
    QByteArray attachmentBytes(const QString& id) const;
    // Deletes trash older than `retentionDays` (1-365), bundles left empty, and attachment
    // rows no remaining scratch references (sparing ones touched within kAttachmentGraceMs),
    // then returns the freed pages to the filesystem.
    PurgeReport purgeTrash(int retentionDays);

    // ---- Background job state (store_jobs.cpp, M5) ----
    // Unreadable or missing data reads as empty.
    JobState jobState() const;
    void saveJobState(const JobState& state);

    // ---- Settings (settings.cpp, D24, D25) ----
    Settings settings() const;
    // Validates first (InvalidSetting); nothing is stored if invalid.
    void saveSettings(const Settings& settings);
    qint64 nowMs() const { return clock_->nowMs(); }

    // Internal: for noite-core sources (store_*.cpp, backup.cpp) only.
    detail::Db& db() const { return *db_; }

private:
    Store(DataDir dir, std::shared_ptr<const Clock> clock, std::unique_ptr<detail::Db> db);

    DataDir dir_;
    std::shared_ptr<const Clock> clock_;
    std::unique_ptr<detail::Db> db_;
};

} // namespace noite
