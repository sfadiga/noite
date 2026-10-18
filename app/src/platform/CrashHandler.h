#pragma once

#include <QString>
#include <QtGlobal>

// Crash reports (D29, B7): a CRASH line and a module+offset stack trace appended to noite.log,
// plus a minidump crash-YYYYMMDD-HHMMSS.dmp next to it. Then the process ends, without a dialog.
namespace crash {

// Call after applog::install(logsDir). Prunes old dumps, prepares everything the crash path
// needs (it never allocates), and hooks unhandled exceptions, std::terminate, pure calls,
// CRT invalid parameters, SIGABRT and qFatal.
void install(const QString& logsDir);

// Keeps the newest `keep` crash-*.dmp files in logsDir (by name, which is the crash time).
void pruneDumps(const QString& logsDir, int keep = 3);

// "noite.exe+0x1a2b3c": lower-case hex, no padding. The crash path formats frames the same way.
QString formatFrame(const QString& module, quint64 offset);

} // namespace crash
