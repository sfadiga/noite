#pragma once

#include <noite/data_dir.h>
#include <noite/model.h>
#include <noite/settings.h>

#include <QDate>
#include <QString>

// The wizard's and Settings' fixed copy (M6). Pure functions.

// The first note, shown after setup: a bold title and a bulleted list, with the user's own
// hotkeys and stale days.
QString starterNoteHtml(const noite::Settings& settings);

// "today", "yesterday", "Oct 15", "never"; "failing since Oct 15" wins over the day.
QString lastBackupText(const noite::JobState& job, QDate today);

// Shown when the data folder sits inside a synced folder.
QString syncWarningText(noite::SyncProvider provider);
