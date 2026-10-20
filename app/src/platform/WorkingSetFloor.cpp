#include "platform/WorkingSetFloor.h"

#include <algorithm>

#include <windows.h>
#include <psapi.h>

WorkingSetLimits workingSetFloor(qint64 workingSet, qint64 currentMax, qint64 margin)
{
    const qint64 min = workingSet + margin;
    return {min, std::max(currentMax, min + margin)}; // Windows refuses max == min
}

std::optional<QString> applyWorkingSetFloor(qint64 margin)
{
    const HANDLE process = GetCurrentProcess();
    PROCESS_MEMORY_COUNTERS counters{};
    SIZE_T currentMin = 0;
    SIZE_T currentMax = 0;
    DWORD flags = 0;
    if (!GetProcessMemoryInfo(process, &counters, sizeof counters)
        || !GetProcessWorkingSetSizeEx(process, &currentMin, &currentMax, &flags))
        return QString("cannot read the working set: %1").arg(GetLastError());
    const WorkingSetLimits limits = workingSetFloor(qint64(counters.WorkingSetSize), qint64(currentMax), margin);
    if (!SetProcessWorkingSetSizeEx(process, SIZE_T(limits.minBytes), SIZE_T(limits.maxBytes),
                                    QUOTA_LIMITS_HARDWS_MIN_ENABLE | QUOTA_LIMITS_HARDWS_MAX_DISABLE))
        return QString("SetProcessWorkingSetSizeEx failed: %1").arg(GetLastError());
    return std::nullopt;
}

bool workingSetFloorEnabledByEnvironment()
{
    return qEnvironmentVariable("NOITE_WS_FLOOR") != "0";
}
