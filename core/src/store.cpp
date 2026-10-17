#include <noite/store.h>

#include "db.h"
#include "migrate.h"

namespace noite {

std::unique_ptr<Store> Store::open(DataDir dir, std::shared_ptr<const Clock> clock)
{
    auto db = std::make_unique<detail::Db>(detail::Db::open(dir.dbPath()));
    detail::migrate(*db, detail::kMigrations, dir.migrationBackupsDir(), clock->nowMs());
    return std::unique_ptr<Store>(new Store(std::move(dir), std::move(clock), std::move(db)));
}

Store::Store(DataDir dir, std::shared_ptr<const Clock> clock, std::unique_ptr<detail::Db> db)
    : dir_(std::move(dir))
    , clock_(std::move(clock))
    , db_(std::move(db))
{
}

Store::~Store() = default;

int Store::schemaVersion() const
{
    return db_->userVersion();
}

} // namespace noite
