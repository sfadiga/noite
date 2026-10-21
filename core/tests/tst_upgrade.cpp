#include "helpers.h"
#include "registry.h"

#include <noite/backup.h>

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "migrate.h"

using namespace noite;
using namespace noite::test;

namespace {

// Every subfolder of core/tests/golden is one frozen data folder of an older (or the
// current) schema; each must open with this build and show what its manifest says.
QString goldenRoot() { return QDir::fromNativeSeparators(QStringLiteral(NOITE_GOLDEN_DIR)); }

QStringList goldenVersions() { return QDir(goldenRoot()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name); }

void addVersions()
{
    QTest::addColumn<QString>("version");
    for (const QString& v : goldenVersions())
        QTest::newRow(qPrintable(v)) << v;
}

QJsonObject manifestOf(const QString& version)
{
    QFile f(goldenRoot() + "/" + version + "/manifest.json");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

QDate day(const QJsonValue& v) { return QDate::fromString(v.toString(), Qt::ISODate); }

// A golden folder copied into a temp data dir and opened with the fixed clock at the
// moment the fixture was made (2026-10-10 09:00 UTC).
struct Opened {
    QTemporaryDir tmp;
    std::shared_ptr<FixedClock> clock;
    std::unique_ptr<Store> store;
    QString root() const { return tmp.filePath("data"); }
};

void openGolden(const QString& version, Opened& o)
{
    QVERIFY(o.tmp.isValid());
    o.clock = std::make_shared<FixedClock>(
        QDateTime(QDate(2026, 10, 10), QTime(9, 0), QTimeZone::UTC).toMSecsSinceEpoch());
    DataDir dir = DataDir::acquire(o.root());
    QVERIFY(QFile::copy(goldenRoot() + "/" + version + "/noite.db", dir.dbPath()));
    o.store = Store::open(std::move(dir), o.clock);
}

QDate goldenToday() { return QDate(2026, 10, 10); }

} // namespace

class UpgradeTest : public QObject {
    Q_OBJECT

private slots:
    void goldenFoldersExist()
    {
        // Every older schema needs its golden folder, and so does the current one.
        const QStringList have = goldenVersions();
        for (int v = 1; v <= detail::kLatest; ++v)
            QVERIFY2(have.contains(QString("v%1").arg(v)), qPrintable(QString("missing golden/v%1 in %2").arg(v).arg(goldenRoot())));
    }

    void fixtureUnchanged_data() { addVersions(); }
    void fixtureUnchanged()
    {
        QFETCH(QString, version);
        const QJsonObject m = manifestOf(version);
        QVERIFY(!m.isEmpty());
        QFile f(goldenRoot() + "/" + version + "/noite.db");
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString sha = QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());
        QCOMPARE(sha, m["dbSha256"].toString());
    }

    void opensAtLatestSchema_data() { addVersions(); }
    void opensAtLatestSchema()
    {
        QFETCH(QString, version);
        const int schema = manifestOf(version)["schema"].toInt();
        QVERIFY(schema >= 1);
        Opened o;
        openGolden(version, o);
        QVERIFY(o.store);
        QCOMPARE(o.store->schemaVersion(), detail::kLatest);

        const QStringList backups =
            QDir(o.store->dataDir().migrationBackupsDir()).entryList({QString("noite-v%1-*.db").arg(schema)}, QDir::Files);
        QCOMPARE(backups.size(), schema < detail::kLatest ? 1 : 0);
    }

    void contentMatchesManifest_data() { addVersions(); }
    void contentMatchesManifest()
    {
        QFETCH(QString, version);
        const QJsonObject m = manifestOf(version);
        Opened o;
        openGolden(version, o);
        QVERIFY(o.store);
        Store& store = *o.store;

        QHash<QString, ScratchSummary> byTitle;
        for (Tab tab : {Tab::Today, Tab::All, Tab::Completed, Tab::Deferred, Tab::Trash})
            for (const ScratchSummary& s : store.list(tab, goldenToday()))
                byTitle.insert(s.title, s);

        QHash<BundleId, QString> bundleNames;
        for (const Bundle& b : store.listBundles())
            bundleNames.insert(b.id, b.name);

        const QJsonArray notes = m["notes"].toArray();
        QCOMPARE(byTitle.size(), notes.size());
        for (const QJsonValue& v : notes) {
            const QJsonObject n = v.toObject();
            const QString title = n["title"].toString();
            QVERIFY2(byTitle.contains(title), qPrintable(title));
            const Scratch s = store.get(byTitle[title].id);
            QCOMPARE(statusName(s.status), n["status"].toString());
            if (n.contains("trashedAtMs"))
                QCOMPARE(s.trashedAt.value_or(-1), qint64(n["trashedAtMs"].toDouble()));
            else
                QVERIFY(!s.trashedAt);
            QCOMPARE(s.color ? colorName(*s.color) : QString(), n["color"].toString());
            QCOMPARE(s.due ? s.due->toString(Qt::ISODate) : QString(), n["dueDate"].toString());
            QStringList tags;
            for (const QJsonValue& t : n["tags"].toArray())
                tags << t.toString();
            QCOMPARE(s.tags, tags);
            QCOMPARE(s.bundleId ? bundleNames.value(*s.bundleId) : QString(), n["bundle"].toString());
        }

        for (const QJsonValue& v : m["bundles"].toArray()) {
            const QJsonObject b = v.toObject();
            QStringList expected;
            for (const QJsonValue& t : b["members"].toArray())
                expected << t.toString();
            QStringList actual;
            for (const ScratchSummary& s : std::as_const(byTitle))
                if (s.bundleId && bundleNames.value(*s.bundleId) == b["name"].toString())
                    actual << s.title;
            expected.sort();
            actual.sort();
            QCOMPARE(actual, expected);
        }
        QCOMPARE(store.listBundles().size(), m["bundles"].toArray().size());

        const QJsonObject want = m["settings"].toObject();
        const Settings got = store.settings();
        QCOMPARE(got.captureHotkey, want["captureHotkey"].toString());
        QCOMPARE(got.boardHotkey, want["boardHotkey"].toString());
        QCOMPARE(got.trashRetentionDays, want["trashRetentionDays"].toInt());
        QCOMPARE(got.dayStart.toString("HH:mm"), want["dayStart"].toString());
        QCOMPARE(got.backupDir.value_or(QString()), want["backupDir"].toString());
        QCOMPARE(got.backupFrequency == BackupFrequency::Weekly ? QString("weekly") : QString("daily"),
                 want["backupFrequency"].toString());
        QCOMPARE(got.backupKeep, want["backupKeep"].toInt());
        QCOMPARE(got.startWithWindows, want["startWithWindows"].toBool());
        QCOMPARE(got.setupCompleted, want["setupCompleted"].toBool());
        QCOMPARE(got.staleDays, want["staleDays"].toInt());

        QCOMPARE(store.lastReviewDay().value_or(QDate()), day(m["lastReviewDay"]));

        if (m.contains("jobState")) {
            const JobState job = store.jobState();
            QCOMPARE(job.lastPurgeDay.value_or(QDate()), day(m["jobState"].toObject()["lastPurgeDay"]));
            QVERIFY(!job.lastBackupDay);
        }

        if (m.contains("attachment")) {
            const QJsonObject a = m["attachment"].toObject();
            const QByteArray bytes = store.attachmentBytes(a["id"].toString());
            QCOMPARE(qint64(bytes.size()), qint64(a["size"].toDouble()));
            QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()),
                     a["sha256"].toString());
            const Scratch note = store.get(byTitle[a["noteTitle"].toString()].id);
            QVERIFY(note.html.contains("noite-att:" + a["id"].toString()));
        }
    }

    void boardAndSearch_data() { addVersions(); }
    void boardAndSearch()
    {
        QFETCH(QString, version);
        const QJsonObject m = manifestOf(version);
        Opened o;
        openGolden(version, o);
        QVERIFY(o.store);

        QCOMPARE(o.store->list(Tab::All, goldenToday()).size(), m["activeCount"].toInt());
        QCOMPARE(o.store->tabCounts(goldenToday()).of(Tab::All), m["activeCount"].toInt());

        const auto hits = o.store->list(Tab::All, goldenToday(), m["searchWord"].toString());
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits[0].title, m["searchHit"].toString());
    }

    void backupOfUpgradedFolderVerifies_data() { addVersions(); }
    void backupOfUpgradedFolderVerifies()
    {
        QFETCH(QString, version);
        Opened o;
        openGolden(version, o);
        QVERIFY(o.store);
        QTemporaryDir dest;
        backup::Job job;
        job.dbPath = o.store->dataDir().dbPath();
        job.installId = o.store->installId();
        job.dest = dest.path();
        job.keep = 3;
        job.nowMs = o.clock->nowMs();
        const backup::Result r = backup::run(job); // verifies the snapshot; throws if invalid
        QVERIFY(QFile::exists(r.path));
    }
};

NOITE_TEST(UpgradeTest)

#include "tst_upgrade.moc"
