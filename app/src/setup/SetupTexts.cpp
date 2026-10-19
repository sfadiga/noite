#include "setup/SetupTexts.h"

#include <QDateTime>
#include <QLocale>
#include <QTextDocument>

namespace {

QString shortDate(QDate date)
{
    return QLocale(QLocale::English).toString(date, "MMM d");
}

} // namespace

QString starterNoteHtml(const noite::Settings& settings)
{
    const QString capture = settings.captureHotkey.toHtmlEscaped();
    const QString board = settings.boardHotkey.toHtmlEscaped();
    const QString html =
        "<p><b>Welcome to noite</b></p><ul>"
        "<li>" + capture + " opens this window from anywhere. Type, then Esc: it's saved.</li>"
        "<li>" + board + " opens the board with all your notes. Ctrl+Enter marks a note done.</li>"
        "<li>Type @tomorrow or @fri to give a note a date.</li>"
        "<li>Each morning noite shows what's overdue, due today, or untouched for "
        + QString::number(settings.staleDays) + " days: Done, Snooze, Keep or Trash.</li>"
        "<li>Delete this note once you've read it (Del on the board).</li></ul>";
    QTextDocument doc; // canonical Qt rich text, as the editor stores it
    doc.setHtml(html);
    return doc.toHtml();
}

QString lastBackupText(const noite::JobState& job, QDate today)
{
    if (job.backupFailingSinceMs)
        return "failing since " + shortDate(QDateTime::fromMSecsSinceEpoch(*job.backupFailingSinceMs).date());
    if (!job.lastBackupDay)
        return "never";
    if (*job.lastBackupDay == today)
        return "today";
    if (*job.lastBackupDay == today.addDays(-1))
        return "yesterday";
    return shortDate(*job.lastBackupDay);
}

QString syncWarningText(noite::SyncProvider provider)
{
    QString name;
    switch (provider) {
    case noite::SyncProvider::GoogleDrive: name = "Google Drive"; break;
    case noite::SyncProvider::OneDrive: name = "OneDrive"; break;
    case noite::SyncProvider::Dropbox: name = "Dropbox"; break;
    }
    return "This folder is inside " + name + ". Syncing a live database can corrupt it. "
           "Move the noite folder somewhere that isn't synced; backups can still go to Drive.";
}
