# Spike: Qt 6 Widgets capture window (vs Tauri)

**Date:** 2026-10-16 · **Plan:** [M0b](../superpowers/plans/2026-10-16-m0b-qt-spike.md) · **Code:** `spikes/qt-capture/` (throwaway) · **Baseline:** [Tauri spike](2026-10-hotkey-latency.md)

**Question:** can a native Qt 6 Widgets + C++ capture window meet D5 (hotkey → typing ≤ 100 ms, small idle memory)? Can `QTextEdit` deliver the D7/D21 editor without a large build effort?

**Answer:** yes to both.
- **Speed:** about 4× faster than Tauri when warm (p95 7.6 ms vs 28 ms).
- **Memory:** about 9× smaller (18.9 MB vs 176 MB).
- **Editor:** every capability tested works. The markdown typing shortcuts had to be written by hand (~250 lines).
- **Weak spot:** the first summon after 15 minutes idle took 97.5 ms, only just inside the budget.

## Setup

| | |
|---|---|
| OS | Windows 11 Pro 10.0.26300, single monitor |
| Qt | 6.10.3, `win64_msvc2022_64` (6.11/6.12 not installable with aqtinstall 3.3.0), Widgets, default `windows11` style |
| Toolchain | VS Build Tools 2026 (MSVC 18.11), CMake + Ninja, C++20 |
| Build measured | Release, deployed with `windeployqt` |
| Hotkey | `Ctrl+Alt+J` via `RegisterHotKey(MOD_CONTROL\|MOD_ALT\|MOD_NOREPEAT)` and a native event filter |

**Method:** the same harness as the Tauri spike (`spikes/hotkey-latency/run-trials.ps1 -Process qt-capture`).
- **`ready_ms`:** a `QElapsedTimer` started on `WM_HOTKEY`, stopped at the first editor paint while the editor has focus.
- **Positioning:** before showing, the window centres on the monitor of the foreground window (`MonitorFromWindow` → `QWindowsScreen::handle()`).
- **Runs:** made only with the PC idle; the leak detector was checked with a negative control.

## Results

| Scenario | Trials | Failed | Leaked | first (ms) | p50 (ms) | p95 (ms) | max (ms) |
|---|---|---|---|---|---|---|---|
| Warm, back-to-back | 50 | 0 | 0 | 6.99 | 7.05 | **7.58** | 8.29 |
| After 15 min idle | 10 | 0 | 0 | **97.48** | 6.95 | **97.48** | 97.48 |
| After S3 sleep/resume* | 10 | 0 | 0 | 8.26 | 6.83 | 8.26 | 8.26 |
| Harness dry run | 5 | 0 | 0 | 9.75 | 7.32 | 11.07 | 11.07 |

\* The sleep is confirmed in the System log (events 42 → 107). The window was used by hand after waking and before the trials, so this is *not* a first-summon-after-resume measurement.

| Other measurements | Value |
|---|---|
| Idle memory (5 min idle, after 55 summons) | **18.9 MB private**, 40.9 MB working set, **1 process** |
| Cold start (launch → hotkey registered) | **35 ms** median of 5 (98 ms on the first, uncached launch) |
| Deployed folder (`windeployqt`) | 62.9 MB. Includes `vc_redist.x64.exe` (17.9 MB) and `dxcompiler.dll` (13.7 MB), which a portable raster Widgets app doesn't need. About **33 MB trimmed** (estimate, not verified). |

## Side by side

| | Tauri 2 + WebView2 + TipTap | **Qt 6 Widgets + QTextEdit** |
|---|---|---|
| Warm p50 / p95 | 20.7 / 28.0 ms | **7.1 / 7.6 ms** |
| First summon after 15 min idle | **34.0 ms** | 97.5 ms |
| Idle memory (private) | 176 MB (7 processes) | **18.9 MB** (1 process) |
| Cold start | not measured | 35 ms |
| Runtime dependency | WebView2 runtime | none (Qt DLLs ship in the folder) |
| Editor | TipTap: everything ready-made | QTextEdit: rich model ready-made; markdown shortcuts hand-written |
| Language | TypeScript + Rust | C++ (the user's language) |
| Stored format | ProseMirror JSON | Qt HTML subset |

## Editor capability checklist

Automated tests run in `tests/tst_editor.cpp`, 23/23 passing on Release (offscreen). "User" means checked by hand in the real app.

| Capability | Result | How |
|---|---|---|
| `#`/`##`/`###` headings, `-`/`*` bullets, `1.` numbered | pass (tests + user) | hand-written `MarkdownShortcuts` |
| Checkboxes: `- [ ]`, `- []`, `[]` at line start, `[x]`; click toggles | pass (tests); real-app retest of `[]` pending | marker on `QTextBlockFormat`; hit-test in `mousePressEvent` |
| `**bold**`, `*italic*`/`_italic_`, `` `code` `` | pass (tests + user) | hand-written |
| `[label](url)`, bare URL + space, pasted bare URL → link; Ctrl+click opens | pass (tests + user) | hand-written; `QDesktopServices` |
| Rich paste from browser and Word/Docs (formatting, links, images) | pass (user) | Qt's HTML import |
| Pasted markdown text is rendered; ordinary text keeps its line breaks | pass (tests + user) | `QTextDocumentFragment::fromMarkdown` behind a heuristic |
| Pasted image stored as a content-addressed resource (`noite-att://<sha256>.png`) | pass (tests) | `insertFromMimeData` override |
| Tables | pass (tests) | built in |
| Move a selection (text + images) into its own document (split, req. 22) | pass (tests) | `QTextDocumentFragment` + copying resources |
| Save → reload keeps heading, bold, link, checkbox, table, image | **HTML: pass** · **Markdown: loses bold and tables** | `toHtml`/`setHtml` |
| 5,000-paragraph paste | **242 ms** Release (3.6 s Debug) | built in |
| Checkbox rendering under Fusion and Windows 11 styles | pass (rendered images inspected) | built in |

## Gates

| Gate | Condition | Result |
|---|---|---|
| G1 warm latency | p95 ≤ 100 ms | **PASS**: 7.6 ms |
| G2 idle + resume | p95 ≤ 100 ms in both | **PASS, thin margin**: idle 97.5 ms; resume 8.3 ms (with the caveat above) |
| G3 keystrokes | 0 lost or misdirected | **PASS**: 0 of 76 |
| G4 manual checks | all pass | **PARTIAL**: elevated app, hotkey while open, crisp at your scaling all pass; second monitor untested (code picks the foreground window's monitor) |
| G5 idle memory | ≤ 80 MB private | **PASS**: 18.9 MB |

## Findings for the product

1. **The first summon after a long idle is the latency risk** (97.5 ms vs 7 ms warm). It's most likely Windows trimming the hidden window's working set. In M2, measure mitigations (a periodic invisible repaint or touch, keeping the backing store, `SetProcessWorkingSetSize`) and add the idle scenario to the CI performance check.
2. **Notes must be stored as Qt HTML, not markdown.** `toMarkdown()` loses bold and tables. Markdown remains an export (D7 already says export-only).
3. **The markdown shortcuts are the hand-built part**, plus autolink and markdown paste: about 250 lines plus tests here. Real users' habits matter: the author types `- [] task` (no space), as in Notion. The rules should accept the common variants.
4. **Images:** pasted images become content-addressed resources (`noite-att://<hash>`). That maps directly onto D13's attachments folder, with the editor loading resources from storage.
5. **Portable size:** trim `windeployqt` output (no `vc_redist` installer, no `dxcompiler`; ship the VC runtime DLLs next to the exe). Target about 35 MB.
6. **Tooling quirks:** QTest output doesn't come through a pipe here (results go via `-o file`). Offscreen tests need `QT_PLUGIN_PATH` pointing at the Qt install. PowerShell 5.1 loses `ExitCode` unless `$p.Handle` is read straight after starting the process.
7. Carried over from the Tauri spike: **`Ctrl+Alt+N` is taken on the author's PC**, so the default should be `Ctrl+Alt+J`.
