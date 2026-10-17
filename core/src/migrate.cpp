#include "migrate.h"

#include "db.h"
#include "noite_schema.h"

#include <noite/error.h>

#include <QDir>

#include <algorithm>

namespace noite::detail {

const std::vector<Migration> kMigrations = {
    {1, kSchemaV1},
};

void migrate(Db& db, const std::vector<Migration>& migrations, const QString& snapshotDir, qint64 nowMs)
{
    const int current = db.userVersion();
    int target = 0;
    for (const Migration& m : migrations)
        target = std::max(target, m.version);

    if (current > target)
        throw Error(ErrorCode::NewerSchema,
                    QString("database schema v%1 is newer than supported v%2").arg(current).arg(target));
    if (current == target)
        return;

    if (current > 0) {
        if (!QDir().mkpath(snapshotDir))
            throw Error(ErrorCode::Io, "cannot create " + snapshotDir);
        vacuumInto(db, snapshotDir + QString("/noite-v%1-%2.db").arg(current).arg(nowMs));
    }

    Transaction tx(db);
    for (const Migration& m : migrations) {
        if (m.version <= current)
            continue;
        try {
            db.exec(QString::fromUtf8(m.sql));
            db.setUserVersion(m.version);
        } catch (const Error& e) {
            throw Error(ErrorCode::MigrationFailed,
                        QString("migration to v%1 failed: %2").arg(m.version).arg(e.what()));
        }
    }
    tx.commit();
}

} // namespace noite::detail
