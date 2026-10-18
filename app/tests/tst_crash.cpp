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
        // Written newest first, so only the names give the order.
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
        QTest::newRow("av") << "av";
        QTest::newRow("throw") << "throw";
        QTest::newRow("fatal") << "fatal";
        // The hooks without a context of their own.
        QTest::newRow("terminate") << "terminate";
        QTest::newRow("abort") << "abort";
        QTest::newRow("purecall") << "purecall";
        QTest::newRow("invalid") << "invalid";
    }

    void crashLeavesTraceAndDump()
    {
        QFETCH(QString, kind);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QProcess helper;
        helper.start(QCoreApplication::applicationDirPath() + "/noite_crash_helper.exe", {dir.path(), kind});
        QVERIFY2(helper.waitForStarted(), qPrintable(helper.errorString()));
        QVERIFY(helper.waitForFinished(30'000));
        QVERIFY(helper.exitCode() != 0);

        const QStringList lines = readLines(QDir(dir.path()).filePath("noite.log"));
        const QRegularExpression frame(R"(^  \S+\+0x[0-9a-f]+$)");
        int crashLines = 0, frames = 0, dumpLines = 0;
        for (const QString& line : lines) {
            crashLines += line.contains("CRASH 0x");
            frames += frame.match(line).hasMatch();
            dumpLines += line.contains("dump: crash-");
        }
        QVERIFY2(crashLines == 1, qPrintable(lines.join('\n')));
        QVERIFY2(frames >= 3, qPrintable(lines.join('\n')));
        QVERIFY2(dumpLines == 1, qPrintable(lines.join('\n')));

        const QFileInfoList dumps = QDir(dir.path()).entryInfoList({"crash-*.dmp"}, QDir::Files);
        QCOMPARE(dumps.size(), 1);
        QVERIFY(dumps[0].size() > 0);
    }
};

NOITE_TEST(CrashTest)
#include "tst_crash.moc"
