#include "helpers.h"
#include "registry.h"

#include <noite/store.h>

#include <sqlite3.h>

using namespace noite;
using namespace noite::test;

namespace {

// Raw SQLite access, independent of noite's own wrapper.
QString rawQuery(const QString& path, const char* sql)
{
    sqlite3* db = nullptr;
    sqlite3_open(path.toUtf8().constData(), &db);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    QString result;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        result = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}

} // namespace

class StoreOpenTest : public QObject {
    Q_OBJECT

private slots:
    void openCreatesLatestSchema()
    {
        auto f = fixture();
        QCOMPARE(f->store->schemaVersion(), 1);
        QCOMPARE(f->store->dataDir().root(), f->root());
    }

    void dbUsesWal()
    {
        auto f = fixture();
        f->store.reset();
        QCOMPARE(rawQuery(f->root() + "/noite.db", "PRAGMA journal_mode"), QString("wal"));
    }

    void newerDbRefusedViaStore()
    {
        auto f = fixture();
        f->store.reset();
        rawQuery(f->root() + "/noite.db", "PRAGMA user_version = 99");
        NOITE_EXPECT_ERROR(ErrorCode::NewerSchema, reopen(*f));
    }
};

NOITE_TEST(StoreOpenTest)
#include "tst_store_open.moc"
