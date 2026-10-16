# Spike: hotkey → editor-ready latency on Tauri 2

**Date:** 2026-10-15/16 · **Plan:** [M0](../superpowers/plans/2026-10-15-m0-toolchain-and-hotkey-spike.md) · **Code:** `spikes/hotkey-latency/` (throwaway)

**Question:** can Tauri 2 with a hidden, pre-warmed WebView2 window go from a global hotkey to a focused, painted TipTap editor within the D5 budget (≤ 100 ms), without losing keystrokes?

**Answer:** yes, by a wide margin (p95 ≈ 28–34 ms). Idle memory misses D5's 80 MB target by about 2×, because of WebView2 itself. Resume-from-sleep has not been measured yet.

## Setup

| | |
|---|---|
| OS | Windows 11 Pro 10.0.26300, single monitor |
| WebView2 runtime | 155.0.4283.45 |
| Stack | Tauri 2, `tauri-plugin-global-shortcut` 2.4.1, React 19, TipTap 3.32 (StarterKit), Rust 1.99.1 |
| Build measured | release profile, opt-level 3. LTO off and per-key event logging on, so failures can be attributed. `ready_ms` is taken before any typing, so the logging doesn't affect it. |
| Hotkey | `Ctrl+Alt+J`, because `Ctrl+Alt+N` (the D24 default) is taken on this PC; see finding 1 |

**Method:** `run-trials.ps1` activates a dedicated harness window as the "previous app", sends the hotkey, **immediately** types `hello`, waits 400 ms, and sends `Esc`.
- **`ready_ms`** = Rust `Instant` at hotkey press → frontend `focus('end')` plus two `requestAnimationFrame`s (a painted frame).
- **Failed trial:** the editor text ≠ `hello`.
- **Leak:** any keystroke that lands in the harness window. A negative control (`-Hotkey x`) confirms the detector catches leaks.

## Results

| Scenario | Trials | Failed | Leaked | first (ms) | p50 (ms) | p95 (ms) | max (ms) |
|---|---|---|---|---|---|---|---|
| Warm, back-to-back | 50 | 0 | 0 | 22.75 | 20.66 | **28.03** | 29.83 |
| After 15 min idle | 10 | 0 | 0 | **33.98** | 20.58 | **33.98** | 33.98 |
| After sleep/resume | — | — | — | not measured: the PC never slept (no S3 events, empty `powercfg /lastwake`) | | | |

Runs made while the PC was in use are excluded (warm v1: 6/50 failed, diag: 5/50 failed). Their failures were `hellohello`: `Esc` went missing because the capture window lost focus about 40 ms after typing, before `Esc` arrived. The event log shows Alt keyup reaching the page normally, which rules out Alt/menu mode. With the PC left alone, the same build had **0 failures in 60 trials**. Latency in the excluded runs matched the clean ones (p95 27.5 ms).

### Idle memory (5 min idle, capture window hidden)

| Process | Working set (MB) | Private (MB) |
|---|---|---|
| `hotkey-latency.exe` | 27.9 | 5.6 |
| 6 × `msedgewebview2.exe` | 367.5 | 170.8 |
| **Total** | **395.5** | **176.4** |

### Manual checks (reported by the user)

| Check | Result |
|---|---|
| Elevated Task Manager in the foreground → hotkey opens a focused editor that takes typing | pass |
| Second monitor: window centres on the active window's monitor, crisp | **not tested** (no second monitor available) |
| Hotkey pressed while the window is visible → stays focused, no flicker or hide | pass ("it seems so") |

## Gates

| Gate | Condition | Result |
|---|---|---|
| G1 warm latency | p95 ≤ 100 ms | **PASS**: 28.0 ms |
| G2 idle + resume | p95 ≤ 100 ms in both | **idle PASS** (34.0 ms) · **resume PENDING** |
| G3 keystrokes | 0 lost or misdirected | **PASS** with the PC idle (0/60); see finding 3 |
| G4 manual checks | all pass | **PARTIAL**: 2 pass, second monitor untested |
| G5 idle memory | total private ≤ 80 MB | **FAIL**: 176 MB; WebView2 alone is ~171 MB |

## Findings for the product

1. **`Ctrl+Alt+N` is already taken on the author's own PC**, and so is `Ctrl+Alt+Space`. The D24 conflict check is essential, and the default should be a combination that's usually free. Free on this machine: `Ctrl+Alt+J`, `Ctrl+Alt+K`, `Ctrl+Shift+Space`, `Win+Shift+N`.
2. **The memory floor is set by WebView2, not by noite.** The app process is 5.6 MB private, and the WebView2 process tree adds about 170 MB. No frontend change gets this under 80 MB. A target that holds is about **≤ 200 MB private with the board window closed**. That assumes M3 creates the board window on demand and destroys it when it closes, rather than keeping it warm.
3. **Focus can be lost mid-capture when the user clicks elsewhere.** The spike ignores blur. M2 must implement D6's "clicking away saves and hides it", which turns this from a lost `Esc` into intended behaviour.
4. **`center()` centres on the window's own monitor, not the active window's** (D6). This is untested here (one monitor). M2 must explicitly pick the monitor of the foreground window or the cursor before showing.
5. **Focus works across integrity levels:** showing the window from a `RegisterHotKey` handler takes the foreground even over an elevated app.
