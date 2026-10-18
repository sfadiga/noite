#include <noite/store.h>

#include "db.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace noite {

namespace {

void putDay(QJsonObject& o, const char* key, const std::optional<QDate>& day)
{
    if (day)
        o[key] = day->toString(Qt::ISODate);
}

std::optional<QDate> day(const QJsonObject& o, const char* key)
{
    const QDate d = QDate::fromString(o[key].toString(), Qt::ISODate);
    return d.isValid() ? std::optional<QDate>(d) : std::nullopt;
}

} // namespace

JobState Store::jobState() const
{
    detail::Stmt s = db().prepare("SELECT value FROM settings WHERE key = 'job_state'");
    if (!s.step())
        return {};
    const QJsonObject o = QJsonDocument::fromJson(s.text(0).toUtf8()).object();
    JobState state;
    state.lastPurgeDay = day(o, "lastPurgeDay");
    state.lastBackupDay = day(o, "lastBackupDay");
    state.backupAlertDay = day(o, "backupAlertDay");
    if (o["backupFailingSinceMs"].isDouble())
        state.backupFailingSinceMs = qint64(o["backupFailingSinceMs"].toDouble());
    return state;
}

void Store::saveJobState(const JobState& state)
{
    QJsonObject o;
    putDay(o, "lastPurgeDay", state.lastPurgeDay);
    putDay(o, "lastBackupDay", state.lastBackupDay);
    putDay(o, "backupAlertDay", state.backupAlertDay);
    if (state.backupFailingSinceMs)
        o["backupFailingSinceMs"] = double(*state.backupFailingSinceMs);
    detail::Stmt s = db().prepare("INSERT OR REPLACE INTO settings(key, value) VALUES ('job_state', ?)");
    s.bind(1, QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
    s.step();
}

} // namespace noite
