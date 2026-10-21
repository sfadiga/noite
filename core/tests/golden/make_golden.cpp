// Generator for the golden data folders (core/tests/golden/v<N>/{noite.db,manifest.json}).
//
//   noite_golden <dir> <1|2|3>
//
// NEVER re-run this for a schema version whose folder already exists: the committed files are
// frozen evidence of what real data of that version looks like, and tst_upgrade proves the
// current build still opens them. Run it only to add the folder for the *current* schema
// before bumping it (see the note above kMigrations in core/src/migrate.cpp).
//
// Every timestamp comes from a fixed clock (2026-10-10 09:00 UTC). Version 1 is written with
// SQL against the embedded v1 schema (no Store: the current Store only writes the latest
// schema); later versions go through Store, which writes the current schema.

#include "db.h"
#include "noite_schema.h"

#include <noite/clock.h>
#include <noite/content.h>
#include <noite/data_dir.h>
#include <noite/store.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimeZone>
#include <QUuid>

#include <optional>

using namespace noite;

namespace {

const qint64 kNow = QDateTime(QDate(2026, 10, 10), QTime(9, 0), QTimeZone::UTC).toMSecsSinceEpoch();

struct NoteSpec {
    QString text; // first line is the title
    QString status = "active";
    qint64 trashedAtMs = 0;
    std::optional<Color> color;
    QString dueDate;
    QStringList manualTags;
    QString bundle;
    bool attachment = false;
};

const QList<NoteSpec> kNotes = {
    {"Plan the week #work", "active", 0, std::nullopt, {}, {"home"}, {}, true},
    {"Dentist golden crown fitting", "active", 0, std::nullopt, "2026-10-12", {}, {}, false},
    {"Milk and eggs", "active", 0, Color::Yellow, {}, {}, "Groceries", false},
    {"Bread and butter", "active", 0, std::nullopt, {}, {}, "Groceries", false},
    {"Call the bank", "done", 0, std::nullopt, {}, {}, {}, false},
    {"Old receipts to shred", "trashed", kNow - 2 * kDayMs, std::nullopt, {}, {}, {}, false},
};

// The notes of a version's folder: v3 adds a deferred note (M8c) with a manual tag, so the
// tags' trip through the v3 table rebuild stays proven for later versions too.
QList<NoteSpec> notesFor(int version)
{
    QList<NoteSpec> notes = kNotes;
    if (version >= 3)
        notes.append({"Someday learn the cello", "deferred", 0, std::nullopt, {}, {"music"}, {}, false});
    return notes;
}

QJsonObject settingsJson()
{
    // The Settings JSON as stored (keys are additive across versions; the v1 -> v2 migration
    // does not touch it, so v1 and v2 hold the same object).
    QJsonObject o;
    o["captureHotkey"] = "Ctrl+Alt+K";
    o["boardHotkey"] = "Ctrl+Alt+B";
    o["trashRetentionDays"] = 40;
    o["dayStart"] = "07:30";
    o["backupDir"] = "C:/noite-golden-backups";
    o["backupFrequency"] = "weekly";
    o["backupKeep"] = 5;
    o["startWithWindows"] = false;
    o["setupCompleted"] = true;
    o["staleDays"] = 9;
    return o;
}

Settings settingsValue()
{
    Settings s;
    s.captureHotkey = "Ctrl+Alt+K";
    s.boardHotkey = "Ctrl+Alt+B";
    s.trashRetentionDays = 40;
    s.dayStart = QTime(7, 30);
    s.backupDir = "C:/noite-golden-backups";
    s.backupFrequency = BackupFrequency::Weekly;
    s.backupKeep = 5;
    s.startWithWindows = false;
    s.setupCompleted = true;
    s.staleDays = 9;
    return s;
}

QByteArray pngBytes()
{
    QImage image(64, 64, QImage::Format_RGB32);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            image.setPixel(x, y, qRgb(x * 4, y * 4, (x ^ y) * 4));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QString sha256Hex(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString htmlOf(const NoteSpec& n, const QString& attachmentId)
{
    QString html = "<p>" + n.text.toHtmlEscaped();
    if (n.attachment)
        html += "<img src=\"noite-att:" + attachmentId + "\"/>";
    return html + "</p>";
}

QStringList tagsOf(const NoteSpec& n)
{
    QStringList tags = content::inlineTags(n.text);
    for (const QString& t : n.manualTags)
        if (!tags.contains(t))
            tags << t;
    tags.sort();
    return tags;
}

void writeV1(const QString& dbPath)
{
    detail::Db db = detail::Db::open(dbPath);
    db.exec(QString::fromUtf8(kSchemaV1));
    db.setUserVersion(1);

    QHash<QString, QString> bundleIds;
    for (const NoteSpec& n : kNotes) {
        if (!n.bundle.isEmpty() && !bundleIds.contains(n.bundle)) {
            const QString id = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
            bundleIds[n.bundle] = id;
            detail::Stmt s = db.prepare("INSERT INTO bundles(id, name, created_at) VALUES (?, ?, ?)");
            s.bind(1, id).bind(2, n.bundle).bind(3, kNow);
            s.step();
        }
    }
    for (const NoteSpec& n : kNotes) {
        const QString id = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
        const QString html = htmlOf(n, {});
        const QString plain = content::plainText(html);
        const qint64 ts = n.status == "trashed" ? n.trashedAtMs : kNow;
        detail::Stmt s = db.prepare(
            "INSERT INTO scratches(id, content, plain_text, title, first_image, created_at, modified_at, due_date, "
            "color, bundle_id, status, trashed_from, done_at, trashed_at) VALUES (?,?,?,?,NULL,?,?,?,?,?,?,?,?,?)");
        s.bind(1, id).bind(2, html).bind(3, plain).bind(4, content::deriveTitle(plain)).bind(5, ts).bind(6, kNow);
        s.bind(7, n.dueDate.isEmpty() ? std::optional<QString>() : std::optional<QString>(n.dueDate));
        s.bind(8, n.color ? std::optional<QString>(colorName(*n.color)) : std::optional<QString>());
        s.bind(9, n.bundle.isEmpty() ? std::optional<QString>() : std::optional<QString>(bundleIds[n.bundle]));
        s.bind(10, n.status);
        s.bind(11, n.status == "trashed" ? std::optional<QString>("active") : std::optional<QString>());
        s.bind(12, n.status == "done" ? std::optional<qint64>(kNow) : std::optional<qint64>());
        s.bind(13, n.status == "trashed" ? std::optional<qint64>(n.trashedAtMs) : std::optional<qint64>());
        s.step();

        for (const QString& tag : content::inlineTags(plain)) {
            detail::Stmt t = db.prepare("INSERT INTO scratch_tags(scratch_id, tag, source) VALUES (?, ?, 'inline')");
            t.bind(1, id).bind(2, tag);
            t.step();
        }
        for (const QString& tag : n.manualTags) {
            detail::Stmt t = db.prepare("INSERT INTO scratch_tags(scratch_id, tag, source) VALUES (?, ?, 'manual')");
            t.bind(1, id).bind(2, tag);
            t.step();
        }
        detail::Stmt f = db.prepare("INSERT INTO scratch_fts(id, body) VALUES (?, ?)");
        f.bind(1, id).bind(2, plain);
        f.step();
    }

    auto setting = [&](const char* key, const QString& value) {
        detail::Stmt s = db.prepare("INSERT OR REPLACE INTO settings(key, value) VALUES (?, ?)");
        s.bind(1, QString::fromLatin1(key)).bind(2, value);
        s.step();
    };
    setting("settings", QString::fromUtf8(QJsonDocument(settingsJson()).toJson(QJsonDocument::Compact)));
    setting("last_review_day", "2026-10-09");
}

QString writeWithStore(const QString& workDir, int version, QString& attachmentId, qint64& attachmentSize,
                       QString& attachmentSha)
{
    auto clock = std::make_shared<FixedClock>(kNow);
    auto store = Store::open(DataDir::acquire(workDir), clock);

    const QByteArray png = pngBytes();
    attachmentId = store->putAttachment(png, "png");
    attachmentSize = png.size();
    attachmentSha = sha256Hex(png);

    QHash<QString, ScratchId> byBundle;
    for (const NoteSpec& n : notesFor(version)) {
        clock->set(n.status == "trashed" ? n.trashedAtMs : kNow);
        const Scratch s = store->createScratch(htmlOf(n, attachmentId));
        if (!n.dueDate.isEmpty())
            store->setDue(s.id, QDate::fromString(n.dueDate, Qt::ISODate));
        if (n.color)
            store->setColor(s.id, n.color);
        if (!n.manualTags.isEmpty())
            store->setManualTags(s.id, n.manualTags);
        if (!n.bundle.isEmpty()) {
            if (byBundle.contains(n.bundle)) {
                store->bundleTogether(byBundle[n.bundle], s.id);
            } else {
                byBundle[n.bundle] = s.id;
            }
        }
        if (n.status == "done")
            store->markDone(s.id);
        if (n.status == "trashed")
            store->trash(s.id);
        if (n.status == "deferred")
            store->defer(s.id);
        clock->set(kNow);
    }
    for (const Bundle& b : store->listBundles())
        store->renameBundle(b.id, "Groceries");

    store->saveSettings(settingsValue());
    store->setLastReviewDay(QDate(2026, 10, 9));
    JobState job;
    job.lastPurgeDay = QDate(2026, 10, 9);
    store->saveJobState(job);
    return store->dataDir().dbPath();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    if (argc != 3) {
        err << "usage: noite_golden <dir> <1|2|3>\n";
        return 2;
    }
    const QString outDir = QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[1]));
    const int version = QString::fromLocal8Bit(argv[2]).toInt();
    if (version < 1 || version > 3) {
        err << "version must be 1, 2 or 3\n";
        return 2;
    }
    if (QFile::exists(outDir + "/noite.db")) {
        err << "refusing to overwrite an existing golden folder: " << outDir << "\n";
        return 2;
    }

    QTemporaryDir work;
    const QString workDb = work.filePath("data/noite.db");
    QString attId, attSha;
    qint64 attSize = 0;
    try {
        if (version == 1) {
            QDir().mkpath(work.filePath("data"));
            writeV1(workDb); // Db closes at end of scope, folding the WAL into the file
        } else {
            writeWithStore(work.filePath("data"), version, attId, attSize, attSha);
        }
    } catch (const std::exception& e) {
        err << "failed: " << e.what() << "\n";
        return 1;
    }
    if (QFile::exists(workDb + "-wal") && QFileInfo(workDb + "-wal").size() > 0) {
        err << "database still has a WAL; refusing to continue\n";
        return 1;
    }

    QFile dbFile(workDb);
    if (!dbFile.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray dbBytes = dbFile.readAll();
    dbFile.close();

    QJsonArray notes;
    QMap<QString, QStringList> members;
    int active = 0;
    for (const NoteSpec& n : notesFor(version)) {
        const QString title = content::deriveTitle(content::plainText(htmlOf(n, {})));
        QJsonObject o;
        o["title"] = title;
        o["status"] = n.status;
        if (n.status == "trashed")
            o["trashedAtMs"] = double(n.trashedAtMs);
        if (n.color)
            o["color"] = colorName(*n.color);
        if (!n.dueDate.isEmpty())
            o["dueDate"] = n.dueDate;
        o["tags"] = QJsonArray::fromStringList(tagsOf(n));
        if (!n.bundle.isEmpty()) {
            o["bundle"] = n.bundle;
            members[n.bundle] << title;
        }
        if (n.status == "active")
            ++active;
        notes.append(o);
    }
    QJsonArray bundles;
    for (auto it = members.cbegin(); it != members.cend(); ++it)
        bundles.append(QJsonObject{{"name", it.key()}, {"members", QJsonArray::fromStringList(it.value())}});

    QJsonObject manifest;
    manifest["schema"] = version;
    manifest["dbSha256"] = sha256Hex(dbBytes);
    manifest["notes"] = notes;
    manifest["bundles"] = bundles;
    manifest["settings"] = settingsJson();
    manifest["lastReviewDay"] = "2026-10-09";
    if (version >= 2) {
        manifest["jobState"] = QJsonObject{{"lastPurgeDay", "2026-10-09"}};
        manifest["attachment"] = QJsonObject{{"id", attId},
                                             {"size", double(attSize)},
                                             {"sha256", attSha},
                                             {"noteTitle", content::deriveTitle(content::plainText(htmlOf(kNotes[0], {})))}};
    }
    manifest["searchWord"] = "golden";
    manifest["searchHit"] = "Dentist golden crown fitting";
    manifest["activeCount"] = active;

    QDir().mkpath(outDir);
    QFile out(outDir + "/noite.db");
    if (!out.open(QIODevice::WriteOnly) || out.write(dbBytes) != dbBytes.size())
        return 1;
    out.close();
    QFile mf(outDir + "/manifest.json");
    if (!mf.open(QIODevice::WriteOnly))
        return 1;
    mf.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    return 0;
}
