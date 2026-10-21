#pragma once

#include <QString>

#include <vector>

namespace noite::detail {

class Db;

struct Migration {
    int version;
    const char* sql;
};

extern const std::vector<Migration> kMigrations;
inline constexpr int kLatest = 3;

// Brings `db` to the highest version in `migrations` (sorted ascending). An existing
// database is snapshotted to snapshotDir/noite-v<old>-<nowMs>.db first; all pending
// migrations run in one transaction. Throws NewerSchema or MigrationFailed (D26).
void migrate(Db& db, const std::vector<Migration>& migrations, const QString& snapshotDir, qint64 nowMs);

} // namespace noite::detail
