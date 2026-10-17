#include "registry.h"

#include "capture/KeepWarm.h"

#include <QTest>
#include <QWidget>

using namespace std::chrono_literals;

class KeepWarmTest : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { qunsetenv("NOITE_KEEP_WARM"); }

    void ticksOnlyWhileHidden()
    {
        QWidget window;
        window.resize(200, 100);
        KeepWarm warm(&window, 20ms);
        warm.start();
        QTRY_VERIFY(warm.ticks() >= 2);

        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const int whileShown = warm.ticks();
        QTest::qWait(120);
        QCOMPARE(warm.ticks(), whileShown);

        window.hide();
        QTRY_VERIFY(warm.ticks() > whileShown);
        warm.stop();
        const int stopped = warm.ticks();
        QTest::qWait(80);
        QCOMPARE(warm.ticks(), stopped);
    }

    void enabledUnlessEnvSaysNo()
    {
        qunsetenv("NOITE_KEEP_WARM");
        QVERIFY(KeepWarm::enabledByEnvironment());
        qputenv("NOITE_KEEP_WARM", "0");
        QVERIFY(!KeepWarm::enabledByEnvironment());
        qputenv("NOITE_KEEP_WARM", "1");
        QVERIFY(KeepWarm::enabledByEnvironment());
    }
};

NOITE_TEST(KeepWarmTest)
#include "tst_keep_warm.moc"
