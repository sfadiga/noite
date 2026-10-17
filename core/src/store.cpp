#include <noite/store.h>

#include "db.h"
#include "migrate.h"

#include <QDir>
#include <QUuid>

namespace noite {

std::unique_ptr<Store> Store::open(DataDir dir, std::shared_ptr<const Clock> clock)
{
    auto db = std::make_unique<detail::Db>(detail::Db::open(dir.dbPath()));
    detail::migrate(*db, detail::kMigrations, dir.migrationBackupsDir(), clock->nowMs());
    // Incremental auto-vacuum lets the purge return freed blob pages to the filesystem.
    // Switching an existing database over takes one VACUUM.
    qint64 autoVacuum = 0;
    {
        detail::Stmt mode = db->prepare("PRAGMA auto_vacuum");
        mode.step();
        autoVacuum = mode.int64(0);
    }
    if (autoVacuum != 2) // 2 = INCREMENTAL
        db->exec("PRAGMA auto_vacuum = INCREMENTAL; VACUUM;");
    QDir().rmdir(dir.root() + "/attachments"); // left over from before v2; only removed if empty
    detail::Stmt id = db->prepare("INSERT OR IGNORE INTO settings(key, value) VALUES ('install_id', ?)");
    id.bind(1, QUuid::createUuid().toString(QUuid::WithoutBraces));
    id.step();
    return std::unique_ptr<Store>(new Store(std::move(dir), std::move(clock), std::move(db)));
}

Store::Store(DataDir dir, std::shared_ptr<const Clock> clock, std::unique_ptr<detail::Db> db)
    : dir_(std::move(dir))
    , clock_(std::move(clock))
    , db_(std::move(db))
{
}

Store::~Store() = default;

QString Store::installId() const
{
    detail::Stmt s = db_->prepare("SELECT value FROM settings WHERE key = 'install_id'");
    s.step();
    return s.text(0);
}

int Store::schemaVersion() const
{
    return db_->userVersion();
}

} // namespace noite
