# M7b: first summon after idle — measurements

**Plan:** `docs/superpowers/plans/2026-10-20-m7b-performance.md` · **Spec:** `docs/superpowers/specs/2026-10-20-m7b-performance-design.md`

All runs on the author's PC, Release build, `capture-bench` (summons through `CaptureWindow::summon()`, no input sent). Times are ms from "hotkey pressed" to each phase; `ready` = one event-loop turn after the first editor paint (on screen).

## Baseline (2026-10-20, no fix)

`capture-bench --warm 20 --trimmed 10`; a trimmed trial is a keep-warm tick, `EmptyWorkingSet` on the process, 500 ms idle, then the summon.

| Run | Mode | shown p95 | activated p95 | painted p95 | ready p50 | ready p95 | ready max |
|---|---|---|---|---|---|---|---|
| keep-warm on, 1 | warm | 2.5 | 5.8 | 7.9 | 7.8 | 9.1 | 17.0 |
| | trimmed | 3.1 | 6.5 | 9.1 | 9.2 | 9.9 | 9.9 |
| keep-warm on, 2 | warm | 2.4 | 6.4 | 8.7 | 7.9 | 9.2 | 15.6 |
| | trimmed | 3.1 | 6.5 | 9.1 | 9.3 | 9.9 | 9.9 |
| keep-warm on, 3 | warm | 2.5 | 6.3 | 8.0 | 7.7 | 8.9 | 16.8 |
| | trimmed | 3.2 | 6.4 | 8.9 | 9.4 | 10.0 | 10.0 |
| keep-warm off | warm | 2.6 | 6.4 | 9.4 | 7.8 | 10.1 | 15.8 |
| | trimmed | 3.0 | 6.9 | 9.6 | 9.0 | 10.3 | 10.3 |

Every run: 30/30 trials focused, private memory 7.3–7.5 MB, working set 9.1–9.2 MB, `PASS`.

Where a warm summon's ~8 ms goes: `show()` ~2 ms, activation (`activateWindow` + `SetForegroundWindow`) ~3 ms, first editor paint ~2 ms, flush ~1 ms.

**Finding: the simulated trim does not reproduce the real idle cost.** Emptying the working set adds ~1 ms (9 → 10 ms), against 91–103 ms measured after a real 15-minute idle in M2. `EmptyWorkingSet` moves pages to the standby list, from which they fault back cheaply. So the real cost is either pages that were discarded from standby during the idle (hard faults from disk), or not memory at all (CPU deep idle states, GPU/compositor wake-up). The spike's 18 ms first summon right after an S3 resume, with memory intact, also points away from a pure working-set explanation.

Consequence for the plan: the decision rule (Part B) can't choose a fix from trimmed runs that are already under 50 ms without a fix. Stopped for the author's call before Task 3.
