# M7b: performance — design

**Date:** 2026-10-20 · **Status:** agreed in a brainstorming session · **Spec:** D5 (performance budget), D28 (CI); roadmap row M7b (`docs/superpowers/plans/2026-10-15-noite-roadmap.md`)

## Goal

- **Fix the known issue:** the first capture after a long idle measured 103 ms with keep-warm off and 91 ms with it on (M2, one press each). The budget is ≤ 100 ms and the target ≤ 50 ms (`spec.md` §2).
- **Measure before fixing:** break a summon into phases so the fix is chosen by numbers.
- **Gate CI on the §2 budget**, as `spec.md` §9 already promises: warm summon, summon after idle, board open with 2,000 notes, idle memory.

## Decisions

| # | Decision | Why |
|---|---|---|
| P1 | The after-idle state is **simulated in-process**: the bench calls `EmptyWorkingSet` on its own process (what Windows does to an idle process's pages), then summons. No 15-minute waits, no keystrokes. | The user's choice. Seconds per run, deterministic, runs on every push. Per the no-unattended-input rule, real keystrokes only run while the author is away. |
| P2 | The real 15-minute idle with the real hotkey (`spikes/hotkey-latency/run-trials.ps1`) stays a **manual check**, run once while the author is away, to confirm the simulation matches reality. | Calibrates P1. `EmptyWorkingSet` moves pages to the standby list (cheap to fault back in); a real idle under memory pressure can also discard them. |
| P3 | CI **fails on the budget** and **warns on the target**: fail if warm p95 > 100 ms, trimmed p95 > 100 ms, board p95 > 300 ms, or private memory > 80 MB; a `::warning::` if trimmed p95 > 50 ms. Every trimmed trial is a first summon after a (simulated) idle, so its p95 stands for the idle metric. | The user's choice. Shared runners are noisy; a 50 ms gate would flake. |
| P4 | A **working-set floor** is an acceptable fix as long as idle memory stays under the 80 MB budget. | The user's choice. Private memory doesn't grow; less of it is paged out. |
| P5 | The fix is chosen by the decision rule in Part B and stays switchable by environment variable, as `NOITE_KEEP_WARM` is today. | Lets the author measure with and without it on real hardware. |
| P6 | The benches summon through the code the hotkey uses (`CaptureWindow::summon()`), never through synthetic input, and do not require focus. Focus is reported, not gated. | A CI VM may refuse foreground activation; focus is covered by the manual check (P2) and the existing offscreen tests. |

## Part A — measurement

### Phase marks in `LatencyProbe`

- New marks, in order: `pressed` (hotkey received; exists), `shown` (after `show()`), `activated` (after `activateWindow()` and `SetForegroundWindow`), `painted` (the editor viewport's first paint that ends the trial; exists) and `flushed` (one event-loop turn after `painted`; the existing reading point, reported as `ready_ms`).
- `CaptureWindow::showAndFocus()` reports `shown` and `activated` to the probe when one is set.
- **Focus:** by default a trial ends at the first paint *with focus*, as today, so the real harness's `ready_ms` keeps its meaning. `setRequireFocus(false)` (the bench only) ends it at the first paint, and the probe records whether the editor had focus then.
- The probe keeps each trial's phase times (ms since `pressed`).
- The CSV keeps `trial,ready_ms,text` first and adds `shown_ms,activated_ms,painted_ms,focused` after, so `run-trials.ps1` and `trial-stats.ps1` keep working unchanged.

### `capture-bench`

- `app/bench/capture_bench.cpp`, built like `board-bench` (`qt_add_executable`, links `noite-app` and `psapi`), runs on the real platform (a window flashes on screen).
- Setup: a temporary data folder with a store, `StoreAttachments`, the real `NoteEdit`, `CaptureController`, `CaptureWindow` and, unless `--no-keep-warm`, `KeepWarm` started as `App` does. One summon and dismiss first, as `App` pre-warms at startup. The probe runs with `setRequireFocus(false)` (P6).
- Each trial: `probe.pressed()`, `window.summon()`, wait for `flushed` (5 s timeout: a timeout fails the run), type nothing, `controller.dismiss()`, then 300 ms of idle event loop.
- **Warm:** `--warm N` trials (default 20).
- **Trimmed:** `--trimmed N` trials (default 10). Before each: `EmptyWorkingSet(GetCurrentProcess())`, then 500 ms of idle event loop so the trim settles before the summon.
- Fix candidates are switched on with flags (Part B) so one binary measures all of them.
- Output, one line per figure, as `board-bench` prints:
  - per mode and phase: p50, p95, max (ms);
  - focused trials out of total;
  - private memory and working set (MB) after the runs.
- Exit code: 1 if a P3 budget is missed, 0 otherwise; the 50 ms target miss prints `warning: …` and still exits 0.
- `--csv <path>` also writes every trial (mode, trial, phase times, focused).

## Part B — the fix

Candidates, each behind a bench flag and an environment variable in the app:

| | Candidate | Bench flag | App switch |
|---|---|---|---|
| a | **Working-set floor:** once, after the startup pre-warm, `SetProcessWorkingSetSizeEx(process, min, max, QUOTA_LIMITS_HARDWS_MIN_ENABLE \| QUOTA_LIMITS_HARDWS_MAX_DISABLE)` with `min` = the working set at that moment plus 8 MB, `max` = the current maximum. On failure: log a warning and carry on. | `--floor` | `NOITE_WS_FLOOR=0` turns it off |
| b | **Wider keep-warm:** besides `grab()`, the tick lays out the editor's document and touches the window's native handle (`winId()`, a `GetWindowRect`). | `--warm-wide` | under `NOITE_KEEP_WARM` |
| c | a + b | `--floor --warm-wide` | both |

**Decision rule.** On the trimmed runs on the author's PC (Release build, `capture-bench --trimmed 10` per candidate, three runs each):

1. Ship the smallest candidate whose trimmed `ready_ms` p95 is ≤ 50 ms with private memory ≤ 80 MB (a before b before c).
2. Else ship the best candidate that is ≤ 100 ms, and record the gap to 50 ms in `spec.md` §2 as the remaining known issue.
3. Else stop: the phase numbers go to the author before any further fix.

A candidate not shipped is removed from the code, its numbers kept in the results doc.

## Part C — CI gates

- In the `build` job, after **Test** (the Release build is there): steps **Board bench** (`build/Release/board-bench.exe --notes 2000 --runs 20`) and **Capture bench** (`build/Release/capture-bench.exe --warm 20 --trimmed 10 --csv perf/capture.csv`), with the shipped fix on.
- Each step runs with `QT_PLUGIN_PATH` and `PATH` set to the CI Qt, as `run-tests.ps1` does, with a 5-minute timeout.
- The bench's own exit code fails the job (P3). `warning:` lines are echoed as `::warning::` annotations.
- The bench output and `perf/capture.csv` upload as a `perf-results` artifact, kept even when the step fails.
- `board-bench` already exits 1 over 300 ms or 80 MB; it gains nothing except running in CI.

## Part D — validation and docs

- **Manual real-idle check** (P2), only after the author says they are away: deploy the Release build, start it with `NOITE_LATENCY_CSV` set, idle 15 min, run `run-trials.ps1 -Process noite -Trials 10`; once with the shipped fix, once with it switched off. Record the first-trial and p95 times.
- If the real first summon with the fix is more than 25 ms above the bench's trimmed p95, the simulation under-reads: record it, and make the CI trimmed gate that much stricter so it still protects the budget.
- Results doc `docs/spikes/2026-10-m7b-idle-summon.md`: the phase breakdown before the fix, each candidate's numbers, the decision, and the real-idle check.
- Updates: the `spec.md` §2 known issue (resolved, or the remaining gap); the README's latency line; the roadmap's M7b row; a CHANGELOG entry.

## Errors

| Case | Behaviour |
|---|---|
| `SetProcessWorkingSetSizeEx` fails (quota, policy) | Warning in the log with `GetLastError()`; noite runs without the floor. |
| A bench trial never reaches `flushed` within 5 s | The bench prints which phase was last reached and exits 1. |
| CI cannot foreground the window | Trials still count (P6); `focused` shows how many had focus. |
| `EmptyWorkingSet` fails | The bench exits 1: a trimmed run that didn't trim would pass for the wrong reason. |

## Testing

- `tst_latency_probe`: phase marks in order; the CSV's new columns after the old ones; by default an unfocused paint doesn't end the trial; with `setRequireFocus(false)` it does, recorded with `focused=0`.
- `tst_capture_window`: with a probe set, a summon reports `shown` and `activated` before `painted`.
- A test for the floor helper: it computes `min` = working set + 8 MB and reports failure without throwing (the Win32 call itself is exercised by the bench).
- The benches themselves are the performance tests; the CI steps are their harness.

## Order of work

1. Phase marks in `LatencyProbe` and `CaptureWindow` (tests first).
2. `capture-bench`, warm and trimmed, no fix: record the breakdown.
3. Candidates a and b behind flags; measure per the decision rule.
4. Ship the winner in the app with its switch; remove the losers.
5. CI steps and the `perf-results` artifact.
6. Manual real-idle check (author away), results doc, docs updates.
