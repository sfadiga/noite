#include "registry.h"

#include "log/Log.h"

#include <QDateTime>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using namespace applog;

namespace {

// Stands in for QTest's handler: swallows messages (pristine output) and records them.
QStringList g_seen;

void captureHandler(QtMsgType, const QMessageLogContext&, const QString& message)
{
    g_seen << message;
}

// Installs the capturing handler for the life of a test and restores the original one.
struct Capture {
    QtMessageHandler original;
    Capture()
    {
        g_seen.clear();
        original = qInstallMessageHandler(captureHandler);
    }
    ~Capture() { qInstallMessageHandler(original); }
};

QStringList readLines(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QStringList lines = QString::fromUtf8(f.readAll()).split("\r\n");
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    return lines;
}

QString g_hookPath;
QStringList g_hookFileLines;
QString g_hookPathSeen;
QStringList readLines(const QString& path);
void fatalHook(const char*)
{
    g_hookFileLines = readLines(g_hookPath);
    g_hookPathSeen = applog::currentLogPath(); // takes the log mutex
    qInfo("from hook");                          // goes through the handler again
}

QString numbered(int i)
{
    return QString::number(i).rightJustified(30, '0');
}

class Spammer : public QThread {
public:
    void run() override
    {
        for (int i = 0; i < 500; ++i)
            qInfo().noquote() << QString(80, QChar('x'));
    }
};

} // namespace

class LogTest : public QObject {
    Q_OBJECT

private slots:
    void formatsLines()
    {
        const QDateTime when(QDate(2026, 10, 18), QTime(6, 0, 1, 123));
        QCOMPARE(formatLine(QtWarningMsg, when, "backup", "x"), QString("2026-10-18 06:00:01.123 W [backup] x"));
        QVERIFY(formatLine(QtDebugMsg, when, "main", "x").contains(" D [main] x"));
        QVERIFY(formatLine(QtInfoMsg, when, "main", "x").contains(" I [main] x"));
        QVERIFY(formatLine(QtCriticalMsg, when, "main", "x").contains(" C [main] x"));
        QVERIFY(formatLine(QtFatalMsg, when, "main", "x").contains(" F [main] x"));
    }

    void rotatesAtLimitKeepsSeven()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            LogFile log(dir.path(), 100, 7);
            QVERIFY(log.isOpen());
            for (int i = 0; i < 60; ++i)
                log.write(numbered(i));
        }
        const QDir d(dir.path());
        QCOMPARE(d.entryList(QDir::Files).size(), 7);
        QVERIFY(QFile::exists(d.filePath("noite.log")));
        for (int i = 1; i <= 6; ++i) {
            const QString name = QString("noite.%1.log").arg(i);
            QVERIFY(QFile::exists(d.filePath(name)));
            QVERIFY(QFileInfo(d.filePath(name)).size() <= 100 + 32);
        }
        QCOMPARE(readLines(d.filePath("noite.log")).last(), numbered(59));
        // 3 lines per file: noite.6.log holds the oldest survivor, lines 39..41.
        QCOMPARE(readLines(d.filePath("noite.6.log")).first(), numbered(39));
    }

    void rotationFailureKeepsLogging()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkdir("noite.1.log")); // blocks the rename of noite.log
        {
            LogFile log(dir.path(), 100);
            QVERIFY(log.isOpen());
            for (int i = 0; i < 10; ++i)
                log.write(numbered(i));
        }
        QCOMPARE(readLines(QDir(dir.path()).filePath("noite.log")).last(), numbered(9));
    }

    void bufferedLinesComeFirst()
    {
        Capture capture;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        startBuffering();
        qWarning("early");
        install(dir.path());
        qWarning("late");
        const QString path = currentLogPath();
        uninstall();

        QVERIFY(!path.isEmpty());
        const QStringList lines = readLines(path);
        QCOMPARE(lines.size(), 2);
        QVERIFY(lines[0].endsWith(" W [main] early"));
        QVERIFY(lines[1].endsWith(" W [main] late"));
        QCOMPARE(g_seen, (QStringList{"early", "late"})); // still chained
    }

    void bufferKeeps256()
    {
        Capture capture;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        startBuffering();
        for (int i = 0; i < 300; ++i)
            qInfo("early %d", i);
        install(dir.path());
        for (int i = 0; i < 5; ++i)
            qInfo("late %d", i);
        const QString path = currentLogPath();
        uninstall();

        const QStringList lines = readLines(path);
        QCOMPARE(lines.size(), 256 + 5);
        QVERIFY(lines.first().endsWith(" early 0"));
        QVERIFY(lines[255].endsWith(" early 255"));
        QVERIFY(lines[256].endsWith(" late 0"));
        QCOMPARE(g_seen.size(), 305); // nothing lost on the chain
    }

    void threadsDoNotTearLines()
    {
        Capture capture;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        install(dir.path());
        Spammer a, b;
        a.start();
        b.start();
        a.wait();
        b.wait();
        const QString path = currentLogPath();
        uninstall();

        const QStringList lines = readLines(path);
        QCOMPARE(lines.size(), 1000);
        const QRegularExpression format(R"(^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} I \[[^\]]+\] x{80}$)");
        for (const QString& line : lines)
            QVERIFY2(format.match(line).hasMatch(), qPrintable(line));
    }

    void debugDroppedInRelease()
    {
        QVERIFY(!shouldWrite(QtDebugMsg, false));
        QVERIFY(shouldWrite(QtDebugMsg, true));
        QVERIFY(shouldWrite(QtWarningMsg, false));
    }

    void uninstallRestoresPrevious()
    {
        Capture capture;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        startBuffering();
        install(dir.path());
        const QString path = currentLogPath();
        uninstall();
        QVERIFY(currentLogPath().isEmpty());

        qWarning("after");
        QCOMPARE(g_seen, (QStringList{"after"}));
        QVERIFY(readLines(path).isEmpty()); // the file did not see it
    }

    void unopenableDirKeepsRunning()
    {
        Capture capture;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile blocker(dir.filePath("file"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();

        startBuffering();
        install(dir.filePath("file/logs")); // a folder cannot be created under a file
        qWarning("nowhere to go");
        QVERIFY(currentLogPath().isEmpty());
        uninstall();
        // The chained handler (Qt's default one in production, which feeds the debugger) still got it.
        QCOMPARE(g_seen, (QStringList{"nowhere to go"}));
    }

    void previousHandlerIsNeverNull()
    {
        // Qt hands back its default handler when none was installed, so there is always something to chain to.
        Capture capture;
        qInstallMessageHandler(nullptr);
        const QtMessageHandler previous = qInstallMessageHandler(captureHandler);
        QVERIFY(previous != nullptr);
    }

    void fatalHookRunsAfterWriteWithMutexFree()
    {
        Capture capture;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        install(dir.path());
        g_hookPath = currentLogPath();
        g_hookFileLines.clear();
        g_hookPathSeen.clear();
        setFatalHook(fatalHook);

        const QtMessageHandler ours = qInstallMessageHandler(nullptr); // read our handler back
        qInstallMessageHandler(ours);
        static const QMessageLogContext context;
        ours(QtFatalMsg, context, "boom");
        setFatalHook(nullptr);
        const QString path = currentLogPath();
        uninstall();

        QCOMPARE(g_hookFileLines.size(), 1); // written and flushed before the hook ran
        QVERIFY(g_hookFileLines[0].endsWith(" F [main] boom"));
        QCOMPARE(g_hookPathSeen, g_hookPath); // no deadlock on the mutex
        const QStringList lines = readLines(path);
        QCOMPARE(lines.size(), 2);
        QVERIFY(lines[1].endsWith(" I [main] from hook"));
    }
};

NOITE_TEST(LogTest)
#include "tst_log.moc"
