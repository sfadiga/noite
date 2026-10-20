#pragma once

// Shared by the performance benches (board-bench, capture-bench).

#include <QString>
#include <QStringList>

#include <algorithm>
#include <vector>

#include <windows.h>
#include <psapi.h>

namespace bench {

inline PROCESS_MEMORY_COUNTERS_EX memoryCounters()
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof counters);
    return counters;
}

inline double privateMb()
{
    return double(memoryCounters().PrivateUsage) / (1024.0 * 1024.0);
}

inline double workingSetMb()
{
    return double(memoryCounters().WorkingSetSize) / (1024.0 * 1024.0);
}

// Nearest-rank percentile, p in [0, 1]; `values` must not be empty.
inline double percentile(std::vector<double> values, double p)
{
    std::sort(values.begin(), values.end());
    const size_t i = std::min(values.size() - 1, size_t(p * double(values.size() - 1) + 0.5));
    return values[i];
}

inline int intArg(const QStringList& args, const QString& name, int fallback)
{
    const qsizetype i = args.indexOf(name);
    return (i >= 0 && i + 1 < args.size()) ? args[i + 1].toInt() : fallback;
}

} // namespace bench
