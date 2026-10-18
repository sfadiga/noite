#include <noite/settings.h>

#include "db.h"

#include <noite/error.h>
#include <noite/store.h>

#include <QJsonDocument>
#include <QJsonObject>

namespace noite {

namespace {

QJsonObject toJson(const Settings& s)
{
    QJsonObject o;
    o["captureHotkey"] = s.captureHotkey;
    o["boardHotkey"] = s.boardHotkey;
    o["trashRetentionDays"] = s.trashRetentionDays;
    o["dayStart"] = s.dayStart.toString("HH:mm");
    if (s.backupDir)
        o["backupDir"] = *s.backupDir;
    o["backupFrequency"] = s.backupFrequency == BackupFrequency::Weekly ? "weekly" : "daily";
    o["backupKeep"] = s.backupKeep;
    o["startWithWindows"] = s.startWithWindows;
    o["setupCompleted"] = s.setupCompleted;
    o["staleDays"] = s.staleDays;
    return o;
}

Settings fromJson(const QJsonObject& o)
{
    Settings s; // defaults for anything missing
    if (o.contains("captureHotkey"))
        s.captureHotkey = o["captureHotkey"].toString();
    if (o.contains("boardHotkey"))
        s.boardHotkey = o["boardHotkey"].toString();
    if (o.contains("trashRetentionDays"))
        s.trashRetentionDays = o["trashRetentionDays"].toInt();
    if (const QTime t = QTime::fromString(o["dayStart"].toString(), "HH:mm"); t.isValid())
        s.dayStart = t;
    if (o.contains("backupDir") && o["backupDir"].isString())
        s.backupDir = o["backupDir"].toString();
    if (o.contains("backupFrequency"))
        s.backupFrequency = o["backupFrequency"].toString() == "weekly" ? BackupFrequency::Weekly : BackupFrequency::Daily;
    if (o.contains("backupKeep"))
        s.backupKeep = o["backupKeep"].toInt();
    if (o.contains("startWithWindows"))
        s.startWithWindows = o["startWithWindows"].toBool();
    if (o.contains("staleDays"))
        s.staleDays = o["staleDays"].toInt();
    if (o.contains("setupCompleted"))
        s.setupCompleted = o["setupCompleted"].toBool();
    return s;
}

} // namespace

void validate(const Settings& s)
{
    if (s.trashRetentionDays < 1 || s.trashRetentionDays > 365)
        throw Error(ErrorCode::InvalidSetting, "trash retention must be 1-365 days");
    if (s.backupKeep < 1)
        throw Error(ErrorCode::InvalidSetting, "backups to keep must be at least 1");
    if (s.staleDays < 1 || s.staleDays > 90)
        throw Error(ErrorCode::InvalidSetting, "stale after must be 1-90 days");
}

Settings Store::settings() const
{
    detail::Stmt s = db().prepare("SELECT value FROM settings WHERE key = 'settings'");
    if (!s.step())
        return {};
    return fromJson(QJsonDocument::fromJson(s.text(0).toUtf8()).object());
}

void Store::saveSettings(const Settings& settings)
{
    validate(settings);
    detail::Stmt s = db().prepare("INSERT OR REPLACE INTO settings(key, value) VALUES ('settings', ?)");
    s.bind(1, QString::fromUtf8(QJsonDocument(toJson(settings)).toJson(QJsonDocument::Compact)));
    s.step();
}

} // namespace noite
