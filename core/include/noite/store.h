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
inline constexpr qint64 kAttachmentGraceMs = kDayMs;      // purge never deletes younger files

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
    // Active -> Done; Done -> Active; Active|Done -> Trashed; Trashed -> previous status.
    // Anything else throws InvalidTransition.
    void markDone(const ScratchId& id);
    void reopen(const ScratchId& id);
    void trash(const ScratchId& id);
    void restore(const ScratchId& id);

    // ---- Bundles and clone (store_bundles.cpp, D12, D20) ----
    // Dropping `dragged` onto `target`: joins target's bundle, or creates one named after
    // target's title ("Bundle" if it has none). A bundle with no members is deleted.
    BundleId bundleTogether(const ScratchId& target, const ScratchId& dragged);
    void moveToBundle(const ScratchId& id, std::optional<BundleId> bundle);
    void renameBundle(const BundleId& id, const QString& name);
    QList<Bundle> listBundles() const;
    // Copies content, manual tags, colour and bundle; clears the due date; Active; new times.
    Scratch cloneScratch(const ScratchId& id);

    // ---- Board (store_board.cpp, D9, D17, D19) ----
    // Today: active, overdue -> due today -> undated (oldest first). All: Today's order, then
    // upcoming by due date. Completed: newest done first. Trash: newest trashed first.
    // `search` is matched as accent-insensitive word prefixes; blank means no filter.
    QList<ScratchSummary> list(Tab tab, QDate today, const QString& search = {}) const;
    ReviewCounts reviewCounts(QDate today) const;

    // ---- Attachments and purge (store_attachments.cpp, D13, D22) ----
    // Stores bytes under <sha256>.<ext> (ext lowercased; "bin" unless 1-8 alphanumerics),
    // deduplicated; returns the attachment id. Throws AttachmentTooLarge.
    QString putAttachment(const QByteArray& bytes, const QString& ext);
    // Throws InvalidAttachmentId unless `id` is a well-formed attachment id.
    QString attachmentPath(const QString& id) const;
    // Deletes trash older than `retentionDays` (1-365), bundles left empty, and attachment
    // files no remaining scratch references, sparing files younger than kAttachmentGraceMs.
    PurgeReport purgeTrash(int retentionDays);

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
