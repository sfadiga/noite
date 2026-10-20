#include "registry.h"

#include "capture/LatencyProbe.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {

QStringList csvLines(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed().split('\n');
}

// One trial with every mark, finished and dismissed.
void runTrial(LatencyProbe& probe, const QString& text = "hello")
{
    probe.pressed();
    probe.shown();
    probe.activated();
    probe.painted(true);
    QTRY_VERIFY(!probe.pending()); // recorded after the event loop turn (after the flush)
    probe.dismissed(text);
}

} // namespace

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
        runTrial(*probe);

        const QStringList lines = csvLines(csv);
        QCOMPARE(lines.size(), 2);
        const QStringList row = lines[1].split(',');
        QCOMPARE(row[0], QString("1"));
        bool ok = false;
        QVERIFY(row[1].toDouble(&ok) >= 0 && ok);
        QCOMPARE(row[2], QString("\"hello\""));
    }

    void secondPaintDoesNotChangeTheReading()
    {
        LatencyProbe probe{QString()};
        probe.pressed();
        probe.painted(true);
        QTRY_VERIFY(!probe.pending());
        const double first = probe.lastReadyMs();
        QTest::qWait(30);
        probe.painted(true);
        QTest::qWait(10);
        QCOMPARE(probe.lastReadyMs(), first);
    }

    void phasesInOrder()
    {
        LatencyProbe probe{QString()};
        probe.pressed();
        probe.shown();
        probe.activated();
        probe.painted(true);
        QTRY_VERIFY(!probe.pending());
        const LatencyProbe::Phases& p = probe.lastPhases();
        QVERIFY(p.shownMs >= 0);
        QVERIFY(p.shownMs <= p.activatedMs);
        QVERIFY(p.activatedMs <= p.paintedMs);
        QVERIFY(p.paintedMs <= p.readyMs);
        QCOMPARE(p.readyMs, probe.lastReadyMs());
        QVERIFY(p.focused);
    }

    // The harness's reading means "painted with focus": an unfocused paint doesn't end it.
    void unfocusedPaintIgnoredByDefault()
    {
        LatencyProbe probe{QString()};
        probe.pressed();
        probe.painted(false);
        QTest::qWait(20);
        QVERIFY(probe.pending());
    }

    // The bench: a CI VM may not hand the window focus.
    void unfocusedPaintEndsTrialWhenAllowed()
    {
        LatencyProbe probe{QString()};
        probe.setRequireFocus(false);
        probe.pressed();
        probe.painted(false);
        QTRY_VERIFY(!probe.pending());
        QVERIFY(probe.lastPhases().readyMs >= 0);
        QVERIFY(!probe.lastPhases().focused);
    }

    // A second hotkey press while open re-activates without a new trial.
    void marksOutsideATrialAreIgnored()
    {
        LatencyProbe probe{QString()};
        probe.pressed();
        probe.shown();
        probe.activated();
        probe.painted(true);
        QTRY_VERIFY(!probe.pending());
        const LatencyProbe::Phases before = probe.lastPhases();
        QTest::qWait(10);
        probe.shown();
        probe.activated();
        probe.painted(true);
        QTest::qWait(10);
        const LatencyProbe::Phases& after = probe.lastPhases();
        QCOMPARE(after.shownMs, before.shownMs);
        QCOMPARE(after.activatedMs, before.activatedMs);
        QCOMPARE(after.paintedMs, before.paintedMs);
        QCOMPARE(after.readyMs, before.readyMs);
        QCOMPARE(after.focused, before.focused);
    }

    void csvHasPhaseColumns()
    {
        QTemporaryDir tmp;
        const QString csv = tmp.filePath("trials.csv");
        LatencyProbe probe(csv);
        runTrial(probe, "hi");
        const QStringList lines = csvLines(csv);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0], QString("trial,ready_ms,text,shown_ms,activated_ms,painted_ms,focused,faults"));
        const QStringList row = lines[1].split(',');
        QCOMPARE(row.size(), 8);
        for (int i = 3; i <= 5; ++i) {
            bool ok = false;
            QVERIFY2(row[i].toDouble(&ok) >= 0 && ok, qPrintable(lines[1]));
        }
        QCOMPARE(row[6], QString("1"));
        bool ok = false;
        QVERIFY2(row[7].toInt(&ok) >= 0 && ok, qPrintable(lines[1]));
    }

    // Page faults between pressed and ready: tells a paged-out summon from a slow wake-up.
    void countsPageFaultsDuringTheTrial()
    {
        LatencyProbe probe{QString()};
        QVERIFY(probe.lastPhases().faults == -1);
        probe.pressed();
        probe.painted(true);
        QTRY_VERIFY(!probe.pending());
        QVERIFY(probe.lastPhases().faults >= 0);
    }

    // A CSV left by 1.0.x keeps its header; run-trials.ps1 reads the first three fields.
    void appendsToAnOldHeaderFile()
    {
        QTemporaryDir tmp;
        const QString csv = tmp.filePath("trials.csv");
        QFile old(csv);
        QVERIFY(old.open(QIODevice::WriteOnly | QIODevice::Text));
        old.write("trial,ready_ms,text\n1,5.00,\"x\"\n");
        old.close();
        LatencyProbe probe(csv);
        probe.pressed(); // its own count starts at 1 again
        probe.painted(true);
        QTRY_VERIFY(!probe.pending());
        probe.dismissed("y");
        const QStringList lines = csvLines(csv);
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[0], QString("trial,ready_ms,text"));
        bool ok = false;
        QVERIFY(lines[2].split(',')[1].toDouble(&ok) >= 0 && ok);
    }

    void emptyPathWritesNothing()
    {
        QTemporaryDir tmp;
        const QString cwd = QDir::currentPath();
        QDir::setCurrent(tmp.path());
        LatencyProbe probe{QString()};
        runTrial(probe);
        const qsizetype entries = QDir(tmp.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).size();
        QDir::setCurrent(cwd);
        QCOMPARE(entries, 0);
    }
};

NOITE_TEST(LatencyProbeTest)
#include "tst_latency_probe.moc"
