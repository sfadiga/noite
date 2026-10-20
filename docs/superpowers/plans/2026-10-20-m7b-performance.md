# M7b Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Break a capture summon into timed phases, fix the first summon after idle (≤ 100 ms, target ≤ 50 ms), and gate CI on the D5 budget.

**Architecture:** `LatencyProbe` gains phase marks that `CaptureWindow` reports. A new `capture-bench` exe drives the real capture window through `summon()` in warm and trimmed (`EmptyWorkingSet`) trials. Fix candidates (a working-set floor, a wider keep-warm) are measured with bench flags; the winner ships behind an env switch. CI runs `board-bench` and `capture-bench` after the tests.

**Tech Stack:** C++20, Qt 6.10.3 Widgets, Qt Test, Win32 (`psapi`: `EmptyWorkingSet`, `GetProcessMemoryInfo`; `SetProcessWorkingSetSizeEx`), GitHub Actions (`windows-latest`), PowerShell 5.1.

**Spec:** `docs/superpowers/specs/2026-10-20-m7b-performance-design.md`

## Global Constraints

- Budget (fail): warm `ready_ms` p95 ≤ 100 ms; trimmed `ready_ms` p95 ≤ 100 ms; board open p95 ≤ 300 ms with 2,000 notes; private memory ≤ 80 MB.
- Target (warn only): trimmed `ready_ms` p95 ≤ 50 ms; printed as a line starting `warning: `, exit code stays 0.
- Benches summon through `CaptureWindow::summon()`, never synthetic input; focus is reported, not gated.
- Real keystrokes (`run-trials.ps1`) run only after the author says they are away. Running a bench locally takes the foreground for ~30 s: ask before each local run.
- `CaptureWindow`'s probe readings keep their meaning for the harness: by default a trial ends at the first editor paint **with focus**.
- CSV: `trial,ready_ms,text` stay the first three columns.
- Env switches: `NOITE_KEEP_WARM=0` (exists), `NOITE_WS_FLOOR=0` (new; floor on otherwise, if shipped).
- Floor: hard minimum = working set after the pre-warm + 8 MB (`8 * 1024 * 1024`), flags `QUOTA_LIMITS_HARDWS_MIN_ENABLE | QUOTA_LIMITS_HARDWS_MAX_DISABLE`; a failure is logged, never fatal.
- `.github/workflows/ci.yml` stays ASCII-only (its first step checks this).
- Edit sources with the Edit tool, not PowerShell text round-trips.
- **Deviations from the spec, on purpose:** `App` has no startup summon, only `winId()`; so the "pre-warm" both `App` and the bench run is `prewarmCapture()` (native window + one offscreen render + the floor), and the floor is taken right after it. `KeepWarm` ticks every 4 min, never inside a bench run, so trimmed trials call `tick()` and then trim, i.e. "trimmed since the last tick".
- Local commands: build with `powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 -Config Release`, tests with `tools\run-tests.ps1 -Config Release` (results printed per class).

## Review Focus

1. **The floor is refused** (quota, group policy, a huge margin): noite must start and run normally, with one warning in the log. → Task 3, `floorRefusedReportsWithoutThrowing`.
2. **A harness CSV from 1.0.x already exists** with the old three-column header: new rows must still parse for `run-trials.ps1` (first three fields). → Task 1, `appendsToAnOldHeaderFile`.
3. **The hotkey is pressed again while the capture is open**: `summon()` re-activates without `show()`; a mark arriving outside a pending trial must not alter the finished reading. → Task 1, `marksOutsideATrialAreIgnored`.
4. **A summon that never paints** (window already visible, nothing dirty): the app's probe stays pending until dismissed and writes an empty `ready_ms`, as today; the bench must not hang. → Task 2, the 5 s trial timeout prints the last phase reached and exits 1 (`--selftest-timeout` exercises it).
5. **CI cannot foreground the window**: trials must still finish (first paint without focus) and the gate still applies. → Task 2, the bench runs the probe with `setRequireFocus(false)`; Task 1, `unfocusedPaintEndsTrialWhenAllowed`.

---

### Task 1: Phase marks in `LatencyProbe` and `CaptureWindow`

**Files:**
- Modify: `app/src/capture/LatencyProbe.h`, `app/src/capture/LatencyProbe.cpp`
- Modify: `app/src/capture/CaptureWindow.cpp` (`showAndFocus()`, `eventFilter()`)
- Test: `app/tests/tst_latency_probe.cpp`, `app/tests/tst_capture_window.cpp`

**Interfaces:**
- Produces (in `LatencyProbe`):
  - `struct Phases { double shownMs = -1; double activatedMs = -1; double paintedMs = -1; double readyMs = -1; bool focused = false; };` (ms since `pressed()`; -1 = not reached)
  - `void shown(); void activated(); void painted(bool focused);` (replaces `painted()`)
  - `void setRequireFocus(bool require);` (default `true`)
  - `const Phases& lastPhases() const;` (`lastReadyMs()` stays, = `lastPhases().readyMs`)
  - An empty `csvPath` writes nothing on `dismissed()`.
  - CSV header: `trial,ready_ms,text,shown_ms,activated_ms,painted_ms,focused`; `focused` is `1`/`0`; unreached phases are empty fields.
- `CaptureWindow::showAndFocus()` calls `probe_->shown()` right after `show()` (only when it was hidden) and `probe_->activated()` after `SetForegroundWindow`. `eventFilter` calls `probe_->painted(editor_->hasFocus())` on every editor viewport `Paint`.

- [ ] **Step 1: Write the failing probe tests** in `tst_latency_probe.cpp` (update the existing two to call `painted(true)`):

```cpp
void phasesInOrder()            // pressed, shown, activated, painted(true) -> after QTRY !pending:
                                // 0 <= shownMs <= activatedMs <= paintedMs <= readyMs; focused == true
void unfocusedPaintIgnoredByDefault()   // pressed, painted(false), qWait(20) -> pending() still true
void unfocusedPaintEndsTrialWhenAllowed() // setRequireFocus(false); pressed, painted(false)
                                        // -> QTRY !pending; lastPhases().focused == false
void marksOutsideATrialAreIgnored()     // finish a trial; then shown(), activated(), painted(true)
                                        // -> lastPhases() unchanged (compare all five fields)
void csvHasPhaseColumns()       // one trial, dismissed("hi") -> header ==
                                // "trial,ready_ms,text,shown_ms,activated_ms,painted_ms,focused";
                                // row fields 3..5 parse as doubles >= 0, field 6 == "1"
void appendsToAnOldHeaderFile() // file pre-written "trial,ready_ms,text\n1,5.00,\"x\"\n";
                                // one trial -> line 3 starts "2," and its field 1 parses as a double
void emptyPathWritesNothing()   // LatencyProbe probe{QString()}; a trial + dismissed -> no file created in cwd
                                // (check QDir::current() entry count unchanged)
```

- [ ] **Step 2: Write the failing window test** in `tst_capture_window.cpp`:

```cpp
void summonReportsPhasesInOrder() // LatencyProbe probe{QString()}; w.window.setLatencyProbe(&probe);
                                  // probe.pressed(); w.window.summon(); QTRY_VERIFY(!probe.pending());
                                  // shownMs >= 0 && shownMs <= activatedMs && activatedMs <= paintedMs
```

- [ ] **Step 3: Run to see them fail**

Run: `tools\build.ps1 -Config Release`, then `tools\run-tests.ps1 -Config Release`
Expected: build fails on `painted(true)` / `shown()` (no such members) — that is the red state.

- [ ] **Step 4: Implement** the interface above. A phase mark records only while `pending_` and only the first time per trial. `painted(focused)` is ignored when `requireFocus_ && !focused`; otherwise it records `paintedMs` and `focused` and schedules the existing one-turn `readyMs` reading. Write the header only for a new file; old files keep theirs.

- [ ] **Step 5: Run the suite** — Expected: `LatencyProbeTest` and `CaptureWindowTest` pass; `TOTAL failures: 0`.

- [ ] **Step 6: Commit** — `feat(capture): phase marks in the latency probe (shown, activated, painted, ready)`

---

### Task 2: `capture-bench` (warm and trimmed, no fix)

**Files:**
- Create: `app/bench/BenchUtil.h` (header-only: `percentile`, `intArg`, `privateMb`, `workingSetMb`), `app/bench/capture_bench.cpp`
- Create: `app/src/capture/Prewarm.h`, `app/src/capture/Prewarm.cpp`
- Modify: `app/bench/board_bench.cpp` (use `BenchUtil.h`; drop its local copies), `app/CMakeLists.txt` (add `capture-bench`; add `Prewarm` to `noite-app`), `app/src/App.cpp` (call `prewarmCapture` in place of `window_->winId()`), `app/src/capture/KeepWarm.h` (make `tick()` public)

**Interfaces:**
- Consumes: Task 1's `LatencyProbe` (`setRequireFocus`, `lastPhases`, `pending`).
- Produces:
  - `struct PrewarmOptions { bool floor = false; };`
  - `std::optional<QString> prewarmCapture(QWidget* window, PrewarmOptions options);` — creates the native window (`winId()`), renders it once offscreen (`grab()`), applies the floor when asked (Task 3; until then `floor` is ignored); returns an error message, never throws.
  - `capture-bench [--warm N=20] [--trimmed N=10] [--csv PATH] [--no-keep-warm] [--floor] [--warm-wide] [--selftest-timeout]`
  - Output lines: `mode=<warm|trimmed> phase=<shown|activated|painted|ready> p50=… p95=… max=…`, then `focused=<k>/<n>`, `private_mb=…`, `working_set_mb=…`, `warning: …` lines, then `PASS` or `FAIL`. Exit 1 on a budget miss, a trial timeout, or a failed `EmptyWorkingSet`.
  - `--csv` columns: `mode,trial,shown_ms,activated_ms,painted_ms,ready_ms,focused`.

- [ ] **Step 1: Move the shared helpers** from `board_bench.cpp` into `BenchUtil.h` (`inline`), add `workingSetMb()` (`PROCESS_MEMORY_COUNTERS_EX::WorkingSetSize`). Build; run `build\Release\board-bench.exe --notes 200 --runs 3` (ask first: it opens windows) — Expected: prints `runs=3 …` and `PASS`.

- [ ] **Step 2: Add `prewarmCapture`** and use it in `App::start` at the `winId()` line (log a returned error with `qWarning() << "prewarm:" << *error`). Run the suite — Expected: `TOTAL failures: 0`.

- [ ] **Step 3: Write `capture_bench.cpp`.** Setup mirrors `App`: temp `DataDir`/`Store`, `StoreAttachments`, `NoteEdit`, `CaptureController`, `CaptureWindow`, `prewarmCapture(&window, {floor})`, `KeepWarm` unless `--no-keep-warm` (constructed as `App` does; not started — the bench calls `tick()` itself). `LatencyProbe probe{QString()}; probe.setRequireFocus(false); window.setLatencyProbe(&probe);`
  - Trial: `probe.pressed(); window.summon();` pump events (`processEvents(WaitForMoreEvents, 5)`) until `!probe.pending()` or 5 s; on timeout print `timeout: trial <n> last phase <name>` and exit 1. Then `controller.dismiss();` and 300 ms of event loop.
  - Trimmed trial: before it, `keepWarm.tick()` (if on), then `EmptyWorkingSet(GetCurrentProcess())` (failure → print `EmptyWorkingSet failed: <GetLastError()>`, exit 1), then 500 ms of event loop.
  - `--selftest-timeout`: runs one trial with the probe's `pressed()` but without `summon()`, so the timeout path is exercised; must print `timeout:` and exit 1.
  - Gates and warning per Global Constraints.

- [ ] **Step 4: Add the CMake target** next to `board-bench`: `qt_add_executable(capture-bench bench/capture_bench.cpp bench/BenchUtil.h)`, `target_include_directories(capture-bench PRIVATE bench)`, `target_link_libraries(capture-bench PRIVATE noite-app psapi)`. Build — Expected: `build\Release\capture-bench.exe` exists.

- [ ] **Step 5: Run the timeout self-test** (ask first): `build\Release\capture-bench.exe --selftest-timeout` — Expected: a `timeout:` line, exit code 1.

- [ ] **Step 6: Baseline run** (ask first): `build\Release\capture-bench.exe --warm 20 --trimmed 10 --csv <scratchpad>\baseline.csv`, three times, plus once with `--no-keep-warm`. Expected: all four complete; record every output in `docs/spikes/2026-10-m7b-idle-summon.md` under "Baseline" (warm and trimmed per phase, focused, memory). If trimmed `ready_ms` p95 is already ≤ 50 ms, say so in the doc and ask the author before Task 3.

- [ ] **Step 7: Commit** — `feat(bench): capture-bench times warm and trimmed summons by phase` (include the results doc).

---

### Task 3: Fix candidates behind flags

**Files:**
- Create: `app/src/platform/WorkingSetFloor.h`, `app/src/platform/WorkingSetFloor.cpp`; add to `noite-app` in `app/CMakeLists.txt`
- Modify: `app/src/capture/Prewarm.cpp` (apply the floor when `options.floor`), `app/src/capture/KeepWarm.h/.cpp` (wide mode)
- Test: `app/tests/tst_working_set_floor.cpp` (add to `app-tests`), `app/tests/tst_keep_warm.cpp`

**Interfaces:**
- Produces:
  - `constexpr qint64 kWorkingSetFloorMargin = 8 * 1024 * 1024;`
  - `struct WorkingSetLimits { qint64 minBytes; qint64 maxBytes; };`
  - `WorkingSetLimits workingSetFloor(qint64 workingSet, qint64 currentMax, qint64 margin = kWorkingSetFloorMargin);` — `min = workingSet + margin`, `max = std::max(currentMax, min)`.
  - `std::optional<QString> applyWorkingSetFloor(qint64 margin = kWorkingSetFloorMargin);` — reads the working set and current max (`GetProcessMemoryInfo`, `GetProcessWorkingSetSizeEx`), calls `SetProcessWorkingSetSizeEx` with the Global Constraints flags; on failure returns `"SetProcessWorkingSetSizeEx failed: <GetLastError()>"`.
  - `bool workingSetFloorEnabledByEnvironment();` — `NOITE_WS_FLOOR` != `"0"`.
  - `KeepWarm(QWidget* window, std::chrono::milliseconds interval = std::chrono::minutes(4), QObject* parent = nullptr, bool wide = false);` — in wide mode `tick()` also computes `document()->documentLayout()->documentSize()` for each `QTextEdit` child of the window and calls `GetWindowRect` on its native handle.

- [ ] **Step 1: Write the failing tests**

```cpp
// tst_working_set_floor.cpp
void floorIsWorkingSetPlusMargin()  // workingSetFloor(40 MB, 1 MB) == {48 MB, 48 MB};
                                    // workingSetFloor(40 MB, 100 MB) == {48 MB, 100 MB}
void floorRefusedReportsWithoutThrowing() // applyWorkingSetFloor(qint64(1) << 42 /* 4 TB */)
                                          // returns a message starting "SetProcessWorkingSetSizeEx failed"
void floorSwitchFollowsEnvironment()      // unset -> true; "0" -> false; "1" -> true
// tst_keep_warm.cpp
void wideTickTouchesTheEditorLayout() // QWidget with a QTextEdit child holding text; KeepWarm(&w, 20ms, nullptr, true);
                                      // tick() while hidden -> ticks() == 1 and no crash; (wide adds work, not state)
```

- [ ] **Step 2: Run to see them fail** (missing symbols). **Step 3: Implement.** **Step 4: Run the suite** — Expected: `TOTAL failures: 0`.

- [ ] **Step 5: Check the floor holds under the bench's trim** (ask first): `capture-bench --trimmed 3 --floor` with a temporary `working_set_mb` print after each trim. Expected: working set after trim ≥ the floor's `min`. **If it falls below** (`EmptyWorkingSet` ignores the hard minimum), stop: report to the author that the simulation cannot judge candidate a, with the numbers, before Task 4.

- [ ] **Step 6: Measure per the decision rule** (ask first): `capture-bench --warm 20 --trimmed 10` three runs each with `--floor`, `--warm-wide`, `--floor --warm-wide`. Record all outputs in the results doc under "Candidates", with the decision (spec Part B: the smallest candidate with trimmed p95 ≤ 50 ms and private ≤ 80 MB, in order a, b, c; else the best ≤ 100 ms with the gap recorded; else stop and report).

- [ ] **Step 7: Commit** — `perf(capture): working-set floor and wide keep-warm candidates, measured`

---

### Task 4: Ship the winner, remove the rest

**Files:**
- Modify: `app/src/App.cpp` (startup), `app/src/capture/Prewarm.cpp`, `app/src/capture/KeepWarm.*`, `app/src/platform/WorkingSetFloor.*` (only what loses goes), `app/bench/capture_bench.cpp` (default to the shipped fix; keep a flag to turn it off: `--no-floor` and/or `--no-keep-warm`)
- Test: the tests from Task 3 for whatever stays; `app/tests/tst_app.cpp`

**Interfaces:**
- If the floor ships: `App::start` passes `PrewarmOptions{workingSetFloorEnabledByEnvironment()}`; an error is logged as `qWarning() << "working-set floor:" << *error` and startup continues.
- If wide keep-warm ships: it becomes `KeepWarm`'s only behaviour (drop the `wide` parameter); `NOITE_KEEP_WARM=0` still turns it off.
- A losing candidate is deleted with its tests and flag; its numbers stay in the results doc.

- [ ] **Step 1: Write the failing app test** (if the floor ships): `startupAppliesTheFloorUnlessSwitchedOff` in `tst_app.cpp` — with `NOITE_WS_FLOOR` unset, after `app.start()` the process's hard minimum (`GetProcessWorkingSetSizeEx` flags include `QUOTA_LIMITS_HARDWS_MIN_ENABLE`); with `"0"` it doesn't. Every `App` in the test process would otherwise pin it: `AppTest::init()` sets `NOITE_WS_FLOOR=0` for all tests, this test unsets it, and `cleanup()` restores the default limits with `SetProcessWorkingSetSizeEx(h, -1, -1, QUOTA_LIMITS_HARDWS_MIN_DISABLE)`.
- [ ] **Step 2: Run to see it fail. Step 3: Wire and remove. Step 4: Run the suite** — Expected: `TOTAL failures: 0`.
- [ ] **Step 5: Bench with the defaults** (ask first): `capture-bench --warm 20 --trimmed 10` — Expected: `PASS`, numbers matching the winning candidate's.
- [ ] **Step 6: Commit** — `perf(capture): ship <winner> for the first summon after idle`

---

### Task 5: CI gates

**Files:**
- Modify: `.github/workflows/ci.yml` (`build` job, after **Test**, before **Upload test results**)

**Interfaces:**
- Consumes: `build/Release/board-bench.exe`, `build/Release/capture-bench.exe` (Task 2/4 options and exit codes).

- [ ] **Step 1: Add the steps.** Two `shell: powershell` steps, `timeout-minutes: 5` each, both with `$env:PATH = "$env:QT_ROOT_DIR\bin;$env:PATH"` and `$env:QT_PLUGIN_PATH = "$env:QT_ROOT_DIR\plugins"`:
  - **Board bench**: `build/Release/board-bench.exe --notes 2000 --runs 20`, output teed to `perf/board.txt`.
  - **Capture bench**: `build/Release/capture-bench.exe --warm 20 --trimmed 10 --csv perf/capture.csv`, output teed to `perf/capture.txt`; each line starting `warning: ` is also echoed as `::warning::<rest>`.
  - Each step ends with `exit $LASTEXITCODE` so the bench's exit code fails it.
  - An **Upload perf results** step (`actions/upload-artifact`, name `perf-results`, path `perf/`, `if: always()`).
- [ ] **Step 2: Check the workflow is ASCII-only** — Run: `grep -nP '[^\x00-\x7F]' .github/workflows/ci.yml` — Expected: no output.
- [ ] **Step 3: Commit and push the branch; open a draft PR** — `ci: gate the board and capture benches on the D5 budget`. Expected: the `build` job is green with both bench steps passing, and the `perf-results` artifact holds `board.txt`, `capture.txt`, `capture.csv`. If a bench fails only on CI, read its numbers from the artifact and report to the author before changing any threshold.

---

### Task 6: Real-idle check and docs

**Files:**
- Modify: `docs/spikes/2026-10-m7b-idle-summon.md`, `docs/spec.md` (§2 idle row), `README.md` (the latency line under "Instant capture"), `docs/superpowers/plans/2026-10-15-noite-roadmap.md` (M7b row: plan link, done), `CHANGELOG.md` (`[Unreleased]`)

- [ ] **Step 1: Ask the author to say they are away.** Only then: `tools\deploy.ps1 -Config Release`; start `dist\noite\noite.exe --data <temp>` with `NOITE_LATENCY_CSV=<temp>\trials.csv`; wait 15 min (background wait, no polling); `spikes\hotkey-latency\run-trials.ps1 -Process noite -Trials 10 -Csv <temp>\trials.csv -Label m7b-idle-fix`. Repeat with the shipped fix switched off (`NOITE_WS_FLOOR=0` and/or `NOITE_KEEP_WARM=0`), label `m7b-idle-nofix`. Expected: each run prints `first=… p50=… p95=…` with `failed=0 missing_rows=0 leaked_chars=0`.
- [ ] **Step 2: Record and calibrate.** Add both runs to the results doc. If the real first summon with the fix is more than 25 ms above the bench's trimmed p95, record the gap and lower the bench's trimmed fail threshold by it (`capture_bench.cpp` constant), noting why in a comment.
- [ ] **Step 3: Update the docs:** `spec.md` §2 idle row (the measured result; "Known issue" removed, or the remaining gap stated); README latency line with the new numbers; roadmap M7b row → `[m7b](2026-10-20-m7b-performance.md) (done)`; CHANGELOG `### Changed`/`### Fixed` line for the faster first capture after idle, and that CI now checks the performance budget.
- [ ] **Step 4: Run the suite** — Expected: `TOTAL failures: 0`. **Step 5: Commit** — `docs(m7b): idle-summon results, spec and roadmap updated`; mark the PR ready for review.
