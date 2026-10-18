#include "helpers.h"
#include "registry.h"

#include "db.h"
#include "migrate.h"

#include <QDir>
#include <QTemporaryDir>

using namespace noite;
using namespace noite::detail;

namespace {

QStringList snapshots(const QString& dir, const QString& pattern = "*.db")
{
    return QDir(dir).entryList({pattern}, QDir::Files);
}

int tableCount(Db& db, const QString& name)
{
    Stmt s = db.prepare("SELECT count(*) FROM sqlite_master WHERE name = ?");
    s.bind(1, name);
    s.step();
    return int(s.int64(0));
}

} // namespace

class MigrateTest : public QObject {
    Q_OBJECT

private slots:
    void freshDbMigratesWithoutSnapshot()
    {
        QTemporaryDir tmp;
        Db db = Db::open(tmp.filePath("noite.db"));
        migrate(db, kMigrations, tmp.filePath("snap"), 1000);
        QCOMPARE(db.userVersion(), kLatest);
        QVERIFY(snapshots(tmp.filePath("snap")).isEmpty());
    }

    void v2AddsAttachmentsTable()
    {
        QTemporaryDir tmp;
        Db db = Db::open(tmp.filePath("noite.db"));
        migrate(db, kMigrations, tmp.filePath("snap"), 1000);
        QCOMPARE(kLatest, 2);
        QCOMPARE(db.userVersion(), 2);
        QCOMPARE(tableCount(db, "attachments"), 1);
    }

    void upgradeSnapshotsOldVersionFirst()
    {
        QTemporaryDir tmp;
        const QString snap = tmp.filePath("snap");
        Db db = Db::open(tmp.filePath("noite.db"));
        migrate(db, {kMigrations[0]}, snap, 1000);
        QCOMPARE(db.userVersion(), 1);

        migrate(db, {kMigrations[0], {2, "CREATE TABLE extra(x);"}}, snap, 2000);
        QCOMPARE(db.userVersion(), 2);
        QCOMPARE(tableCount(db, "extra"), 1);
        const QStringList taken = snapshots(snap, "noite-v1-*.db");
        QCOMPARE(taken.size(), 1);
        Db old = Db::open(snap + "/" + taken[0]);
        QCOMPARE(old.userVersion(), 1);
    }

    void failingMigrationRollsBack()
    {
        QTemporaryDir tmp;
        const QString snap = tmp.filePath("snap");
        Db db = Db::open(tmp.filePath("noite.db"));
        migrate(db, {kMigrations[0]}, snap, 1000);

        NOITE_EXPECT_ERROR(ErrorCode::MigrationFailed,
                           migrate(db, {kMigrations[0], {2, "CREATE TABLE ok(x); CREATE TABLE broken("}}, snap, 2000));
        QCOMPARE(db.userVersion(), 1);
        QCOMPARE(tableCount(db, "ok"), 0);
        QCOMPARE(snapshots(snap, "noite-v1-*.db").size(), 1);
    }

    void newerSchemaRefused()
    {
        QTemporaryDir tmp;
        Db db = Db::open(tmp.filePath("noite.db"));
        db.setUserVersion(99);
        NOITE_EXPECT_ERROR(ErrorCode::NewerSchema, migrate(db, kMigrations, tmp.filePath("snap"), 1000));
        QCOMPARE(db.userVersion(), 99);
    }
};

NOITE_TEST(MigrateTest)
#include "tst_migrate.moc"
