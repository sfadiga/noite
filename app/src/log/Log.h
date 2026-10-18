#pragma once

#include <QDateTime>
#include <QFile>
#include <QString>
#include <QtGlobal>

// The app's log file (D29): data/logs/noite.log, rotated at a size limit, fed by a Qt message handler.
namespace applog {

// Appends lines to noite.log, rotating to noite.1.log ... noite.(files-1).log. Not thread-safe by itself.
class LogFile {
public:
    explicit LogFile(QString dir, qint64 maxBytes = 1'048'576, int files = 7);

    bool isOpen() const { return m_file.isOpen(); }
    // Appends line + CRLF and flushes; rotates first when the write would pass maxBytes.
    void write(const QString& line);
    QString path() const;

private:
    bool openCurrent();
    void rotate();
    QString nameOf(int index) const; // 0 is the live file

    QString m_dir;
    qint64 m_maxBytes;
    int m_files;
    QFile m_file;
};

QString formatLine(QtMsgType type, const QDateTime& when, const QString& thread, const QString& message);
// Debug messages are dropped from the file in release builds.
bool shouldWrite(QtMsgType type, bool debugBuild);

// Names the calling thread in log lines; unnamed threads show their numeric id, the GUI thread "main".
void setThreadName(const QString& name);

// Installs the handler early (before the data folder is known): keeps the first 256 lines, chains to the previous handler.
void startBuffering();
// Opens the file in logsDir, writes the buffered lines in order, then every message.
void install(const QString& logsDir);
// Restores the handler that was there before startBuffering/install.
void uninstall();
// Empty when no file is open.
QString currentLogPath();

// Called with the message for QtFatalMsg after its line is written (the crash handler hooks in here).
using FatalHook = void (*)(const char* message);
void setFatalHook(FatalHook hook);

} // namespace applog
