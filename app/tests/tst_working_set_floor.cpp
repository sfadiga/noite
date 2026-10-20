#include "registry.h"

#include "platform/WorkingSetFloor.h"

#include <QTest>

namespace {

constexpr qint64 kMb = 1024 * 1024;

} // namespace

class WorkingSetFloorTest : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { qunsetenv("NOITE_WS_FLOOR"); }

    void floorIsWorkingSetPlusMargin()
    {
        const WorkingSetLimits small = workingSetFloor(40 * kMb, 1 * kMb);
        QCOMPARE(small.minBytes, 48 * kMb);
        QCOMPARE(small.maxBytes, 56 * kMb); // above the minimum: Windows refuses max == min (error 87)
        const WorkingSetLimits large = workingSetFloor(40 * kMb, 100 * kMb);
        QCOMPARE(large.minBytes, 48 * kMb);
        QCOMPARE(large.maxBytes, 100 * kMb);
    }

    // Quota or policy can refuse it: noite carries on without the floor.
    void floorRefusedReportsWithoutThrowing()
    {
        const std::optional<QString> error = applyWorkingSetFloor(qint64(1) << 42); // 4 TB
        QVERIFY(error);
        QVERIFY2(error->startsWith("SetProcessWorkingSetSizeEx failed"), qPrintable(*error));
    }

    void floorSwitchFollowsEnvironment()
    {
        qunsetenv("NOITE_WS_FLOOR");
        QVERIFY(workingSetFloorEnabledByEnvironment());
        qputenv("NOITE_WS_FLOOR", "0");
        QVERIFY(!workingSetFloorEnabledByEnvironment());
        qputenv("NOITE_WS_FLOOR", "1");
        QVERIFY(workingSetFloorEnabledByEnvironment());
    }
};

NOITE_TEST(WorkingSetFloorTest)
#include "tst_working_set_floor.moc"
