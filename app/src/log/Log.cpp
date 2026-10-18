#include "log/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QThread>

#include <memory>

namespace applog {

LogFile::LogFile(QString dir, qint64 maxBytes, int files)
    : m_dir(std::move(dir)), m_maxBytes(maxBytes), m_files(files)
{
    QDir().mkpath(m_dir);
    openCurrent();
}

QString LogFile::nameOf(int index) const
{
    const QString name = index == 0 ? QString("noite.log") : QString("noite.%1.log").arg(index);
    return QDir(m_dir).filePath(name);
}

QString LogFile::path() const
{
    return nameOf(0);
}

bool LogFile::openCurrent()
{
    m_file.setFileName(nameOf(0));
    return m_file.open(QIODevice::WriteOnly | QIODevice::Append);
}

void LogFile::rotate()
{
    m_file.close();
    // Every failure is ignored: the live file then grows past the limit until the next try.
    QFile::remove(nameOf(m_files - 1));
    for (int i = m_files - 2; i >= 0; --i)
        QFile::rename(nameOf(i), nameOf(i + 1));
    openCurrent();
}

void LogFile::write(const QString& line)
{
    const QByteArray bytes = line.toUtf8() + "\r\n";
    if (m_file.isOpen() && m_file.size() > 0 && m_file.size() + bytes.size() > m_maxBytes)
        rotate();
    if (!m_file.isOpen() && !openCurrent())
        return;
    m_file.write(bytes);
    m_file.flush();
}

QString formatLine(QtMsgType type, const QDateTime& when, const QString& thread, const QString& message)
{
    char letter = 'D';
    switch (type) {
    case QtDebugMsg: letter = 'D'; break;
    case QtInfoMsg: letter = 'I'; break;
    case QtWarningMsg: letter = 'W'; break;
    case QtCriticalMsg: letter = 'C'; break;
    case QtFatalMsg: letter = 'F'; break;
    }
    return QString("%1 %2 [%3] %4").arg(when.toString("yyyy-MM-dd HH:mm:ss.zzz"), QString(QChar(letter)), thread, message);
}

bool shouldWrite(QtMsgType type, bool debugBuild)
{
    return type != QtDebugMsg || debugBuild;
}

namespace {

#if defined(QT_NO_DEBUG) || defined(NDEBUG)
constexpr bool kDebugBuild = false;
#else
constexpr bool kDebugBuild = true;
#endif

constexpr int kBufferLines = 256;

thread_local QString t_threadName;
thread_local bool t_inHandler = false;

struct State {
    QMutex mutex;
    bool hooked = false;
    bool buffering = false;
    QStringList buffer;
    std::unique_ptr<LogFile> file;
    QtMessageHandler previous = nullptr;
    FatalHook fatalHook = nullptr;
};

State& state()
{
    static State s;
    return s;
}

QString threadLabel()
{
    if (!t_threadName.isEmpty())
        return t_threadName;
    if (QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread())
        return QStringLiteral("main");
    return QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    // A message logged from inside the handler (on this thread) is dropped rather than deadlocking.
    if (t_inHandler)
        return;
    t_inHandler = true;
    const QString line = formatLine(type, QDateTime::currentDateTime(), threadLabel(), message);
    QtMessageHandler previous = nullptr;
    FatalHook hook = nullptr;
    {
        State& s = state();
        QMutexLocker lock(&s.mutex);
        if (shouldWrite(type, kDebugBuild)) {
            if (s.file)
                s.file->write(line);
            else if (s.buffering && s.buffer.size() < kBufferLines)
                s.buffer << line;
        }
        previous = s.previous;
        hook = s.fatalHook;
    }
    if (previous)
        previous(type, context, message);
    t_inHandler = false;
    if (type == QtFatalMsg && hook)
        hook(message.toUtf8().constData());
}

// Caller holds the mutex.
void hookHandler(State& s)
{
    if (!s.hooked) {
        s.previous = qInstallMessageHandler(handler);
        s.hooked = true;
    }
}

} // namespace

void setThreadName(const QString& name)
{
    t_threadName = name;
}

void startBuffering()
{
    State& s = state();
    QMutexLocker lock(&s.mutex);
    hookHandler(s);
    s.buffering = true;
    s.buffer.clear();
}

void install(const QString& logsDir)
{
    State& s = state();
    QMutexLocker lock(&s.mutex);
    hookHandler(s);
    auto file = std::make_unique<LogFile>(logsDir);
    if (file->isOpen()) {
        for (const QString& line : s.buffer)
            file->write(line);
        s.file = std::move(file);
    }
    s.buffer.clear();
    s.buffering = false;
}

void uninstall()
{
    State& s = state();
    QMutexLocker lock(&s.mutex);
    if (s.hooked)
        qInstallMessageHandler(s.previous);
    s.hooked = false;
    s.buffering = false;
    s.buffer.clear();
    s.file.reset();
    s.previous = nullptr;
}

QString currentLogPath()
{
    State& s = state();
    QMutexLocker lock(&s.mutex);
    return s.file ? s.file->path() : QString();
}

void setFatalHook(FatalHook hook)
{
    State& s = state();
    QMutexLocker lock(&s.mutex);
    s.fatalHook = hook;
}

} // namespace applog
