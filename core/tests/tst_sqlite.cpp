#include "registry.h"

#include <noite/error.h>

#include <QTest>
#include <sqlite3.h>

class SqliteTest : public QObject {
    Q_OBJECT

private slots:
    void fts5IsCompiledIn() { QCOMPARE(sqlite3_compileoption_used("ENABLE_FTS5"), 1); }

    void fts5TableWorks()
    {
        sqlite3* db = nullptr;
        QCOMPARE(sqlite3_open(":memory:", &db), SQLITE_OK);
        const char* sql =
            "CREATE VIRTUAL TABLE t USING fts5(x, tokenize = 'unicode61 remove_diacritics 2');"
            "INSERT INTO t(x) VALUES('Reunião amanhã');";
        QCOMPARE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr), SQLITE_OK);
        sqlite3_stmt* stmt = nullptr;
        QCOMPARE(sqlite3_prepare_v2(db, "SELECT count(*) FROM t WHERE t MATCH '\"reuniao\"*'", -1, &stmt, nullptr), SQLITE_OK);
        QCOMPARE(sqlite3_step(stmt), SQLITE_ROW);
        QCOMPARE(sqlite3_column_int(stmt, 0), 1);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
    }

    void errorCarriesCode()
    {
        const noite::Error e(noite::ErrorCode::NotFound, "x");
        QCOMPARE(e.code(), noite::ErrorCode::NotFound);
        QCOMPARE(QString::fromUtf8(e.what()), QString("x"));
    }
};

NOITE_TEST(SqliteTest)
#include "tst_sqlite.moc"
