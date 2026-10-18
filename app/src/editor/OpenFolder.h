#pragma once

#include <QString>

// Where Ctrl+click on a file chip writes the attachment so the shell can open it (A5):
// %TEMP%\noite-open; its per-attachment folders are removed at every startup.
QString defaultOpenFolder();

// Removes the per-attachment folders (12 lowercase hex digits) inside `dir`, best-effort:
// files still open elsewhere stay, and nothing else in `dir` is touched.
void clearOpenFolder(const QString& dir);

// `name` made safe as one Windows file name: characters Windows rejects (<>:"/\|?* and
// control characters) become '_', surrounding spaces and trailing dots go. If nothing
// usable is left (empty, "." or ".."), returns `fallback`.
QString safeFileName(const QString& name, const QString& fallback);
