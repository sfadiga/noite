#pragma once

#include <QString>

#include <optional>

// Idle first-summon fix (D5, M7b): a hard working-set minimum keeps noite's pages resident
// while it idles in the tray. Without it Windows trims them and later reuses the memory, and
// the first capture after a long idle re-reads ~1,000 pages from disk inside show() and
// activation (~110 ms measured; docs/spikes/2026-10-m7b-idle-summon.md). Private memory
// doesn't grow; less of it is paged out. NOITE_WS_FLOOR=0 turns it off.

constexpr qint64 kWorkingSetFloorMargin = 8 * 1024 * 1024;

struct WorkingSetLimits {
    qint64 minBytes;
    qint64 maxBytes;
};

// The limits for a process now using `workingSet` bytes whose maximum is `currentMax`:
// min = workingSet + margin; max = the larger of currentMax and min + margin (the hard
// minimum is what matters; the maximum stays soft).
WorkingSetLimits workingSetFloor(qint64 workingSet, qint64 currentMax, qint64 margin = kWorkingSetFloorMargin);

// Applies a hard minimum of the current working set + `margin` to this process. An error
// message on failure (quota, policy), never a throw.
std::optional<QString> applyWorkingSetFloor(qint64 margin = kWorkingSetFloorMargin);

bool workingSetFloorEnabledByEnvironment();
