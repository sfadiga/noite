#pragma once

#include <noite/clock.h>
#include <noite/data_dir.h>
#include <noite/model.h>

#include <memory>

namespace noite {

namespace detail {
class Db;
}

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
