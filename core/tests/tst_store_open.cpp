#include "helpers.h"
#include "registry.h"

#include "db.h"
#include "migrate.h"

#include <noite/store.h>

#include <QDir>

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
        QCOMPARE(f->store->schemaVersion(), 2);
        QCOMPARE(f->store->dataDir().root(), f->root());
    }

    void autoVacuumIsIncremental()
    {
        auto f = fixture();
        f->store.reset();
        QCOMPARE(rawQuery(f->root() + "/noite.db", "PRAGMA auto_vacuum"), QString("2"));
        f->store = Store::open(DataDir::acquire(f->root()), f->clock);
        f->store.reset();
        QCOMPARE(rawQuery(f->root() + "/noite.db", "PRAGMA auto_vacuum"), QString("2"));
    }

    void v1DatabaseOpensAsV2()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        QVERIFY(QDir().mkpath(root));
        {
            detail::Db db = detail::Db::open(root + "/noite.db");
            detail::migrate(db, {detail::kMigrations[0]}, root + "/migration-backups", 1000);
            db.exec("INSERT INTO scratches(id, content, plain_text, title, created_at, modified_at, status) "
                    "VALUES ('s1', '<p>old</p>', 'old', 'old', 1, 1, 'active')");
        }
        auto store = Store::open(DataDir::acquire(root), std::make_shared<FixedClock>(t0()));
        QCOMPARE(store->schemaVersion(), 2);
        QCOMPARE(store->list(Tab::All, today()).size(), 1);
        QCOMPARE(QDir(root + "/migration-backups").entryList({"noite-v1-*.db"}, QDir::Files).size(), 1);
        store.reset();
        QCOMPARE(rawQuery(root + "/noite.db", "PRAGMA auto_vacuum"), QString("2"));
    }

    void emptyAttachmentsFolderRemoved()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        QVERIFY(QDir().mkpath(root + "/attachments"));
        auto store = Store::open(DataDir::acquire(root), std::make_shared<FixedClock>(t0()));
        QVERIFY(!QDir(root + "/attachments").exists());
    }

    void nonEmptyAttachmentsFolderKept()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        QVERIFY(QDir().mkpath(root + "/attachments"));
        QFile file(root + "/attachments/x.png");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
        file.close();
        auto store = Store::open(DataDir::acquire(root), std::make_shared<FixedClock>(t0()));
        QVERIFY(QFile::exists(root + "/attachments/x.png"));
    }

    void dbUsesWal()
    {
        auto f = fixture();
        f->store.reset();
        QCOMPARE(rawQuery(f->root() + "/noite.db", "PRAGMA journal_mode"), QString("wal"));
    }

    // Review I3: each installation has its own id, so backups from several installs can share a folder.
    void installIdIsStableAndUnique()
    {
        auto a = fixture();
        auto b = fixture();
        const QString id = a->store->installId();
        QVERIFY(!id.isEmpty());
        QVERIFY(id != b->store->installId());
        reopen(*a);
        QCOMPARE(a->store->installId(), id);
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
