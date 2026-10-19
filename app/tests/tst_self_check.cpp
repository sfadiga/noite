#include "registry.h"

#include "SelfCheck.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

class SelfCheckTest : public QObject {
    Q_OBJECT

private slots:
    void passesWithNothingRequired()
    {
        QVERIFY(!runSelfCheck({}, {}).has_value());
    }

    void reportsMissingPlugin()
    {
        const auto result = runSelfCheck({"QJpegPlugin", "QGifPlugin"}, {"QGifPlugin"});
        QVERIFY(result.has_value());
        QCOMPARE(*result, QString("missing plugin QJpegPlugin"));
    }

    void requiredListMatchesBuild()
    {
        QVERIFY(requiredStaticPlugins().isEmpty()); // this test build is dynamic
    }

    void selfCheckMainWritesFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString exe = dir.filePath("noite.exe");
        QVERIFY(QFile::copy(QStringLiteral(NOITE_EXE_PATH), exe));

        QProcess process;
        process.setWorkingDirectory(dir.path());
        process.start(exe, {"--self-check"});
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);

        QFile out(dir.filePath("noite-self-check.txt"));
        QVERIFY(out.open(QIODevice::ReadOnly));
        QVERIFY2(QString::fromUtf8(out.readAll()).startsWith("self-check ok "), "unexpected content");
        QVERIFY(!QDir(dir.filePath("data")).exists());
    }
};

NOITE_TEST(SelfCheckTest)
#include "tst_self_check.moc"
