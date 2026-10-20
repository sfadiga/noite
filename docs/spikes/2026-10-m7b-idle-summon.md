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

Consequence for the plan: the decision rule (Part B) can't choose a fix from trimmed runs that are already under 50 ms without a fix. Stopped for the author's call; the author chose to measure a real idle first, with a page-fault count per trial added to the probe.

## Real idle (2026-10-20, author away)

Deployed Release build, throwaway data folder, `NOITE_LATENCY_CSV` set; 15 min idle, then `run-trials.ps1 -Process noite -Trials 10` (real `Ctrl+Alt+J` into a dedicated harness window; 0 failed, 0 missing, 0 leaked in every run). `faults` = page faults (soft + hard) from the hotkey to `ready`.

| Run | Trial | shown | activated | painted | ready | faults |
|---|---|---|---|---|---|---|
| 1. fresh process, no floor | 1st after idle | 61.8 | 111.7 | 118.9 | **119.8** | 1056 |
| 2. fresh process, floor on | 1st after idle | 48.4 | 81.4 | 88.6 | **89.6** | 1048 |
| 3. run 2's process, used, 15 min idle again | 1st after idle | 56.4 | 71.5 | 79.0 | **86.5** | **20** |
| all runs | trials 2–10 | ~2.1 | ~5.4 | ~7.4 | ~8 | 0–3 |

The trimmed bench, for comparison: ~2,000 faults per trial and 9–10 ms. Its first warm trial (a fresh process, files in cache): ~1,090 faults and ~17 ms, the extra in activation and the first focused paint.

Findings:

1. **After idle, on a process that has been used, the cost is not noite's memory.** Run 3 took 86.5 ms with 20 page faults; 56 ms of it is inside `show()`. Nothing is paged in, so the stall is the system waking (GPU / DWM compositor / CPU power states), not noite's pages. The working-set floor keeps pages resident (faults 1056 → 20 once used) but buys no time here.
2. **The first summon of a fresh process** adds ~1,000 first-touch faults (activation, first focus, first real paint: code never run before). Served from the file cache they cost ~10 ms (bench); after a long idle they come from disk.
3. An off-screen `show()` at startup (tried, not kept) doesn't remove those faults: they sit in activation and the first focused paint, which a hidden prewarm can't run without taking focus.
4. `KeepWarm`'s offscreen `grab()` doesn't touch `show()` or activation, consistent with M2's small gain (103 → 91 ms).

Decision (Part B rule): no candidate reaches the budget for the measured reason; the floor is not shipped. Stopped for the author's call.

## Spike: cloak instead of hide (2026-10-20, author away)

`NOITE_CLOAK=1` (throwaway): the capture window stays shown but DWM-cloaked (`DWMWA_CLOAK`) between captures; a summon uncloaks and activates it instead of calling `ShowWindow`; closing re-cloaks it and hands the foreground back. The author checked by hand that it opens focused, gives focus back on `Esc`, lets clicks and keys through while closed, and stays open on click-away.

| | shown | activated | painted | ready | faults |
|---|---|---|---|---|---|
| bench, warm (20) p50 | 0.4 | 1.0 | 1.3 | **1.5** | 0 |
| bench, trimmed (5) p50 | 0.8 | 1.3 | 1.8 | **2.0** | 660 |
| real, 1st after 15 min idle (used process) | 0.4 | 4.0 | **7.5** | **82.5** | 28 |
| real, warm (trials 2–10) | ~0.35 | ~4.0 | ~5.1 | ~6 | 0 |

**Finding:** with `ShowWindow` gone, the first summon after idle paints at 7.5 ms and then waits **75 ms in the flush** (the first present to the compositor). The wait moves to whichever call first touches the GPU/compositor; it is the system waking from idle, not noite's work. Cloaking cuts the warm summon from ~8 to ~6 ms in the real harness (1.5 ms in the bench) and doesn't change the after-idle time.

## Conclusion

| Case | Measured | Budget ≤ 100 | Target ≤ 50 |
|---|---|---|---|
| warm summon | ~8 ms (harness), ~9 ms p95 (bench) | met | met |
| 1st summon after 15 min idle, process already used | 82.5–86.5 ms | met | not met: system wake-up (~75 ms in the first compositor call) |
| 1st summon of a fresh process after 15 min idle | 89.6–119.8 ms | not always met | not met: first-touch code from disk + wake-up |
