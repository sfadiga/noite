# Spike: Qt 6 Widgets capture window (vs Tauri)

**Date:** 2026-10-16 · **Plan:** [M0b](../superpowers/plans/2026-10-16-m0b-qt-spike.md) · **Code:** `spikes/qt-capture/` (throwaway) · **Baseline:** [Tauri spike](2026-10-hotkey-latency.md)

**Question:** can a native Qt 6 Widgets + C++ capture window meet D5 (hotkey → typing ≤ 100 ms, small idle memory)? Can `QTextEdit` deliver the D7/D21 editor without a large build effort?

**Answer:** yes to both.
- **Speed:** both stacks are well within budget when warm (Qt p95 7.6 ms, Tauri 28 ms). The two timers stop at different points, so this isn't a like-for-like ratio. Qt stops at the editor's paint, before the backing-store flush and the compositor. Tauri stops after two animation frames plus an IPC call, roughly one frame later.
- **Memory:** about 9× smaller (18.9 MB vs 176 MB).
- **Editor:** every capability tested works after the review fixes below. The markdown typing shortcuts had to be written by hand (~300 lines). Rich-paste cleanup (colours, fonts, remote images) is still open for M2.
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
| **First summon after S3 resume**† | 10 | 0 | 0 | **18.46** | 7.08 | 18.46 | 18.46 |
| Harness dry run | 5 | 0 | 0 | 9.75 | 7.32 | 11.07 | 11.07 |

\* The sleep is confirmed in the System log (events 42 → 107). The window was used by hand after waking and before the trials, so this is *not* a first-summon-after-resume measurement.

† Re-run with a fresh app process (started 17:25:05), the window never shown, and the PC slept at 17:38:00 and resumed at 17:38:03 (events 42 → 107). `trials.csv` confirms no summon between resume and the first trial. Caveats: the sleep lasted only 3 s, n=1, and this was also the window's first show since launch, after ~13 min hidden.

| Other measurements | Value |
|---|---|
| Idle memory (5 min idle, after 55 summons) | **18.9 MB private**, 40.9 MB working set, **1 process** |
| Cold start (`main()` entry → hotkey registered; excludes process creation and DLL loading) | **35 ms** median of 5 (98 ms on the first, uncached launch) |
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
| `[label](url)`, bare URL + space, pasted bare URL → link | pass (tests); pasted URL re-tested by the user after the fix | hand-written |
| Ctrl+click opens a link | pass (user, on a pasted URL); **not covered by a test**; only http/https/mailto allowed (tests) | `QDesktopServices` + scheme allowlist |
| Rich paste from browser and Word/Docs | **formatting kept** (user). **Images and styling not verified:** pasted colours, fonts and sizes are kept as-is (text pasted in black would vanish in dark mode), and images stay as remote or Word-temp references, not attachments. Both are open for M2. | Qt's HTML import, no cleanup yet |
| Pasted markdown text is rendered (needs ≥ 2 markdown constructs, keeps each line); ordinary text, and text with one `- ` line, stay plain; `Ctrl+Shift+V` pastes plain | pass (tests); markdown paste re-tested by the user before the ≥ 2 rule | `fromMarkdown` behind a heuristic |
| Pasted image stored as a content-addressed resource (`noite-att:<sha256>.png`); two images stay distinct | pass (tests; the first version used `noite-att://`, an invalid QUrl, so all images collided, found in review) | `insertFromMimeData` override |
| HTML with content beats the clipboard's bitmap (spreadsheet → table) | pass (tests) | precedence check |
| `_`/`*` inside words, numbers, URLs and code are left alone (`my_var_name`, `2*3*4`, `https://x.com/a_b_c`) | pass (tests; found in review) | word-boundary + URL guards |
| Tables | pass (tests) | built in |
| Move a selection (text + images) into its own document (split, req. 22) | pass (tests) | `QTextDocumentFragment` + copying resources |
| Save → reload keeps heading, bold, link, checkbox, table, and the image *key* | **HTML: pass.** Image bytes aren't in the HTML; they load from attachments by key. Code formatting (fixed pitch) is **lost**; italic and numbered/nested lists are untested. **Markdown: loses bold and tables**, so markdown export needs its own writer. | `toHtml`/`setHtml` |
| 5,000-paragraph paste | **242 ms** Release (3.6 s Debug) | built in |
| Checkbox rendering under Fusion and Windows 11 styles | pass (rendered images inspected) | built in |

## Gates

| Gate | Condition | Result |
|---|---|---|
| G1 warm latency | p95 ≤ 100 ms | **PASS**: 7.6 ms |
| G2 idle + resume | p95 ≤ 100 ms in both | **idle: at the limit**, 97.5 ms from n=1 first-summon. With Tauri's later stop point it would land around or above 100 ms. **Resume: PASS**, 18.5 ms first summon after resume (n=1, 3 s sleep; see †). |
| G3 keystrokes | 0 lost or misdirected | **PASS**: 0 of 85 (5 + 50 + 10 + 10 + 10) |
| G4 manual checks | all pass | **PARTIAL**: elevated app, hotkey while open, crisp at your scaling all pass; second monitor untested (code picks the foreground window's monitor) |
| G5 idle memory | ≤ 80 MB private | **PASS**: 18.9 MB |

## Findings for the product

1. **The first summon after a long idle is the latency risk** (97.5 ms vs 7 ms warm). It's most likely Windows trimming the hidden window's working set. In M2, measure mitigations (a periodic invisible repaint or touch, keeping the backing store, `SetProcessWorkingSetSize`) and add the idle scenario to the CI performance check.
2. **Notes must be stored as Qt HTML, not markdown.** `toMarkdown()` loses bold and tables. Markdown remains an export (D7 already says export-only).
3. **The markdown shortcuts are the hand-built part**, plus autolink and markdown paste: about 250 lines plus tests here. Real users' habits matter: the author types `- [] task` (no space), as in Notion. The rules should accept the common variants.
4. **Images:** pasted images become content-addressed resources (`noite-att:<hash>.png`, with no `//`). That maps directly onto D13's attachments folder, with the editor loading resources from storage.
5. **Portable size:** trim `windeployqt` output (no `vc_redist` installer, no `dxcompiler`; ship the VC runtime DLLs next to the exe). Target about 35 MB.
6. **Tooling quirks:** QTest output doesn't come through a pipe here (results go via `-o file`). Offscreen tests need `QT_PLUGIN_PATH` pointing at the Qt install. PowerShell 5.1 loses `ExitCode` unless `$p.Handle` is read straight after starting the process.
7. Carried over from the Tauri spike: **`Ctrl+Alt+N` is taken on the author's PC**, so the default should be `Ctrl+Alt+J`.
8. **Measurement method for M2/M6:**
   - stop the Qt timer after the flush (a zero-timer posted from paint, or `DwmFlush`), so it's comparable to the Tauri number,
   - measure first-summon-after-idle over several idle cycles, not n=1,
   - treat a missing `ready_ms` as a failure (the harness now does).

## Review fixes (whole-branch review, 2026-10-16)

An independent review found one critical and ten important issues. Fixed in the spike, each with a regression test that failed first (suite 36/36; harness scoring 4/4 with Pester):
- **Attachment key:** `noite-att://<hash>` is an invalid QUrl, so every image collided. It's now `noite-att:<hash>` (spec D7 updated).
- **Emphasis:** no longer fires inside words, numbers, URLs or code.
- **Markdown paste:** needs ≥ 2 constructs and keeps line breaks; `Ctrl+Shift+V` pastes plain text.
- **Clipboard precedence:** HTML with content beats the bitmap, so spreadsheets paste as tables.
- **Links:** Ctrl+click allows only http/https/mailto, and a pasted URL's link format no longer extends to the text typed after it.
- **Harness:** a missing `ready_ms` is a failure, the text check is case-sensitive, and try/finally always closes the target window.

**Deferred to M2 hardening** (they need real input or a design):
- **Rich-paste cleanup:** strip colours, fonts and sizes, and capture `<img>` sources as attachments.
- **Hotkey:** register it to a message-only HWND, because modal loops drop thread messages.
- **Text input paths:** IME/dictation input skips the shortcuts.
- **Single instance:** D27 must run before hotkey registration.
- **Images:** keep the original encoding (JPEG/GIF) and hash off the GUI thread.
- **Theme:** heading size and link colour shouldn't be stored as absolute values.
- **Autolink:** trailing punctuation.
- **Diagnostics:** the keylog and note dump must not ship.
