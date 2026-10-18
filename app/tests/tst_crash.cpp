#include "registry.h"

#include "platform/CrashHandler.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

namespace {

QStringList readLines(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).split("\r\n", Qt::SkipEmptyParts);
}

} // namespace

class CrashTest : public QObject {
    Q_OBJECT

private slots:
    void formatsFrames()
    {
        QCOMPARE(crash::formatFrame("noite.exe", 0x1a2b3c), QString("noite.exe+0x1a2b3c"));
        QCOMPARE(crash::formatFrame("Qt6Core.dll", 0), QString("Qt6Core.dll+0x0"));
    }

    void prunesDumpsToThree()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QDir d(dir.path());
        const QStringList dumps = {"crash-20261014-010000.dmp", "crash-20261015-010000.dmp", "crash-20261016-010000.dmp",
                                   "crash-20261017-010000.dmp", "crash-20261018-010000.dmp"};
        // Written out of order, so only the names give the order.
        for (const QString& name : QStringList{dumps[4], dumps[2], dumps[0], dumps[3], dumps[1], "noite.log"}) {
            QFile f(d.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }

        crash::pruneDumps(dir.path());

        QCOMPARE(d.entryList({"crash-*.dmp"}, QDir::Files, QDir::Name), dumps.mid(2));
        QCOMPARE(QFileInfo(d.filePath("noite.log")).size(), 1);
    }

    void crashLeavesTraceAndDump_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<uint>("code");
        QTest::newRow("av") << "av" << 0xC0000005u;
        QTest::newRow("throw") << "throw" << 0xE06D7363u;
        QTest::newRow("fatal") << "fatal" << 0xE0000001u;
        // The hooks without a context of their own.
        QTest::newRow("terminate") << "terminate" << 0xE0000001u;
        QTest::newRow("abort") << "abort" << 0xE0000001u;
        QTest::newRow("purecall") << "purecall" << 0xE0000002u;
        QTest::newRow("invalid") << "invalid" << 0xE0000002u;
        // The report runs in the guaranteed stack reserve.
        QTest::newRow("overflow") << "overflow" << 0xC00000FDu;
        // A call through a null pointer: the first frame is ?+0x0 and the walk goes on.
        QTest::newRow("nullcall") << "nullcall" << 0xC0000005u;
        // Another thread logs nonstop: the report holds the log mutex, so no line of it is overwritten.
        QTest::newRow("busy") << "busy" << 0xC0000005u;
    }

    void crashLeavesTraceAndDump()
    {
        QFETCH(QString, kind);
        QFETCH(uint, code);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QProcess helper;
        helper.start(QCoreApplication::applicationDirPath() + "/noite_crash_helper.exe", {dir.path(), kind});
        QVERIFY2(helper.waitForStarted(), qPrintable(helper.errorString()));
        QVERIFY(helper.waitForFinished(30'000));
        QCOMPARE(uint(helper.exitCode()), code);

        // The report is one block: the CRASH line, the frames, the dump line, nothing between them.
        const QStringList lines = readLines(QDir(dir.path()).filePath("noite.log"));
        const QString tail = lines.mid(qMax(qsizetype(0), lines.size() - 80)).join('\n');
        qsizetype at = -1;
        for (qsizetype i = 0; i < lines.size(); ++i) {
            if (lines[i].contains("CRASH 0x")) {
                QVERIFY2(at == -1, qPrintable(tail));
                at = i;
            }
        }
        QVERIFY2(at >= 0, qPrintable(tail));
        const QRegularExpression crash(QString(R"( F \[crash\] CRASH 0x%1 at \S+\+0x[0-9a-f]+ \([^)]+\)$)").arg(code, 0, 16));
        QVERIFY2(crash.match(lines[at]).hasMatch(), qPrintable(lines[at]));

        const QRegularExpression frame(R"(^  \S+\+0x[0-9a-f]+$)");
        qsizetype end = at + 1;
        bool helperFrame = false;
        while (end < lines.size() && frame.match(lines[end]).hasMatch())
            helperFrame |= lines[end++].startsWith("  noite_crash_helper.exe+0x");
        QVERIFY2(end - at - 1 >= 3, qPrintable(tail));
        QVERIFY2(helperFrame, qPrintable(tail));
        if (kind == "nullcall")
            QCOMPARE(lines[at + 1], QString("  ?+0x0"));
        QVERIFY2(end == lines.size() - 1, qPrintable(tail));
        QVERIFY2(lines[end].contains(" F [crash] dump: crash-"), qPrintable(tail));

        const QFileInfoList dumps = QDir(dir.path()).entryInfoList({"crash-*.dmp"}, QDir::Files);
        QCOMPARE(dumps.size(), 1);
        QVERIFY(dumps[0].size() > 0);
        QVERIFY(lines[end].endsWith(dumps[0].fileName()));
    }
};

NOITE_TEST(CrashTest)
#include "tst_crash.moc"
