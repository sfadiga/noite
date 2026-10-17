#include "registry.h"

#include "capture/LatencyProbe.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class LatencyProbeTest : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { qunsetenv("NOITE_LATENCY_CSV"); }

    void probeDisabledWithoutEnv()
    {
        qunsetenv("NOITE_LATENCY_CSV");
        QVERIFY(!LatencyProbe::fromEnvironment());
    }

    void probeWritesHarnessRows()
    {
        QTemporaryDir tmp;
        const QString csv = tmp.filePath("trials.csv");
        qputenv("NOITE_LATENCY_CSV", csv.toUtf8());
        const auto probe = LatencyProbe::fromEnvironment();
        QVERIFY(probe);

        probe->pressed();
        probe->painted();
        QTRY_VERIFY(!probe->pending()); // recorded after the event loop turn (after the flush)
        probe->dismissed("hello");

        QFile file(csv);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList lines = QString::fromUtf8(file.readAll()).trimmed().split('\n');
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0], QString("trial,ready_ms,text"));
        const QStringList row = lines[1].split(',');
        QCOMPARE(row[0], QString("1"));
        bool ok = false;
        QVERIFY(row[1].toDouble(&ok) >= 0 && ok);
        QCOMPARE(row[2], QString("\"hello\""));
    }

    void secondPaintDoesNotChangeTheReading()
    {
        QTemporaryDir tmp;
        qputenv("NOITE_LATENCY_CSV", tmp.filePath("t.csv").toUtf8());
        const auto probe = LatencyProbe::fromEnvironment();
        probe->pressed();
        probe->painted();
        QTRY_VERIFY(!probe->pending());
        const double first = probe->lastReadyMs();
        QTest::qWait(30);
        probe->painted();
        QTest::qWait(10);
        QCOMPARE(probe->lastReadyMs(), first);
    }
};

NOITE_TEST(LatencyProbeTest)
#include "tst_latency_probe.moc"
