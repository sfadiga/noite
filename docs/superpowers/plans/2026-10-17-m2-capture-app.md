# M2: Capture App Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The first usable noite: a tray app where `Ctrl+Alt+J` opens a blank note, everything typed or pasted is saved through `noite-core`, and `Esc`, clicking away or `Ctrl+Enter` closes it. Fast, offline and portable.

**Architecture:**
- **`app/`** builds the `noite` WIN32 executable on top of a static `noite-app` library, so the app and its tests share one codebase. It links `noite-core` and Qt Widgets.
- **`NativeHost`** owns a Win32 *message-only window*. Global hotkeys are registered against that window, not as thread messages, so modal loops can't swallow them (review finding). A second launch finds this window and asks the running instance to open the capture window.
- **The editor** (`NoteEdit` + `MarkdownShortcuts`) comes from the Qt spike, hardened with the review's deferred list.
- **`CaptureController`** turns editor changes into `Store` calls: create on first edit, debounced autosave, discard-if-empty, done, colour and due date.
- **`CaptureWindow`** is the frameless always-on-top window, created at startup and kept hidden.

**Tech Stack:** C++20, Qt 6.10.3 (Core, Gui, Widgets, Test), `noite-core`, Win32 API (`RegisterHotKey`, `HWND_MESSAGE`), CMake + Ninja + MSVC, PowerShell harness from the spikes.

**Spec:** [`docs/spec.md`](../../spec.md): §2 (D5 budget, D27 single instance), §5 capture window, keys and paste (D6, D7, D21, D30), §4 due dates (D9). Inputs: the [Qt spike findings](../../spikes/2026-10-qt-capture.md), including its **deferred-to-M2 list**, and the [roadmap](2026-10-15-noite-roadmap.md) M2 row.

## Global Constraints

- One native process; no WebView, no QML, **no Qt Network** (D13: no network code).
- Data folder: `<exe dir>\data` by default; `--data <dir>` overrides it (tests, development). Everything noite writes goes there (D4).
- Capture hotkey comes from `Settings::captureHotkey` (default `Ctrl+Alt+J`). If it can't be registered, the app keeps running with a tray warning and never exits (D24 handles the fix later).
- Notes are stored as Qt HTML. Images and files are `noite-att:<sha256>.<ext>` attachments via `Store::putAttachment` (D7, D21). The editor stores **no absolute colours and no absolute font sizes**: headings use the relative `FontSizeAdjustment`, and links are underlined anchors without a foreground colour (D30 theming).
- Paste:
  - Pasted HTML keeps bold, italic, underline, strike, links, lists, tables, headings and code. It loses colours, background colours, font families and absolute sizes.
  - Images in pasted HTML become attachments (`data:` and local `file:` sources). Remote images (`http(s)`) become links, because noite never fetches (D13).
  - Copied files become file-chip links. The 25 MiB limit is `kMaxAttachmentBytes` (D21).
- Keys in the capture window:
  - `Esc`: save and hide.
  - `Ctrl+Enter`: done and close.
  - `Ctrl+Shift+C`: cycle colour.
  - `Ctrl+Shift+V`: paste plain text.

  Clicking away (window deactivation) also saves and hides (D6, D30).
- Ctrl+click opens only `http`, `https`, `mailto` and `noite-att` links. A `noite-att` link opens the local attachment file.
- Autosave debounce **400 ms**. A note closed while empty is discarded via `Store::discardIfEmpty` (D6).
- Performance (D5): hotkey → focused, painted editor **≤ 100 ms p95**; first summon after 15 min idle **≤ 100 ms**, target ≤ 50 ms; idle private memory **≤ 80 MB**.
- **Keystroke harness runs only after the user says they are away.** Never log other windows' titles. Edit files only with the Edit/Write tools; never round-trip them through Windows PowerShell `Get-Content`.
- The spike's diagnostic key log and note dump must **not** be ported.

## Review Focus

1. **Closing a note mid-autosave:** `Esc`, click-away or `Ctrl+Enter` within the 400 ms debounce must save the latest text, never lose it. Test in Task 7.
2. **Pasted text with hard-coded colours** (e.g. black on white from Word): it must stay readable in a dark theme, so colours are stripped. Test in Task 5.
3. **Capture hotkey already taken at startup:** the app keeps running with a warning, and the tray still opens a note. Test in Task 8.
4. **Second launch while noite is running:** the running instance opens a capture, and the second exits 0 without touching the data. Test in Task 3 (messaging) and Task 9 (real exe).
5. **Non-ASCII data folder path** (e.g. `C:\Users\João\noite\data`): it must open and save normally. Test in Task 8.

---

## File structure

```
app/CMakeLists.txt                      noite-app (static), noite (WIN32 exe), app-tests (exe, add_test)
app/src/main.cpp                        builds App, returns App::run()
app/src/App.h/.cpp                      startup: data dir, lock / second instance, Store, settings, host, tray, capture
app/src/AppOptions.h/.cpp               parse args; resolveDataDir()
app/src/platform/Hotkey.h/.cpp          Hotkey, parseHotkey()
app/src/platform/NativeHost.h/.cpp      message-only HWND, hotkeys, instance activation, instanceKeyFor()
app/src/editor/MarkdownShortcuts.h/.cpp ported from spikes/qt-capture + hardening
app/src/editor/NoteEdit.h/.cpp          ported from spikes/qt-capture + attachments + date tokens
app/src/editor/AttachmentStore.h        interface over Store attachments (+ StoreAttachments adapter)
app/src/editor/PasteCleaner.h/.cpp      HTML clean-up and image capture for pastes
app/src/editor/DateTokens.h/.cpp        parseDateToken()
app/src/capture/CaptureController.h/.cpp
app/src/capture/CaptureWindow.h/.cpp
app/src/capture/LatencyProbe.h/.cpp     optional (NOITE_LATENCY_CSV), harness-compatible rows
app/src/capture/KeepWarm.h/.cpp         idle first-summon mitigation (Task 10)
app/tests/main.cpp + registry.h         same registry pattern as core/tests
app/tests/tst_*.cpp
tools/build.ps1, tools/run-tests.ps1    build/run both test exes
tools/deploy.ps1                        portable dist/noite with trimmed windeployqt output
docs/testing/manual-capture-checks.md   reusable hands-on checklist
```

---

### Task 1: App skeleton and the two-suite test runner

**Files:**
- Create: `app/CMakeLists.txt`, `app/src/main.cpp`, `app/src/App.h/.cpp` (empty `App::run()` returning 0), `app/tests/main.cpp`, `app/tests/registry.h`, `app/tests/tst_smoke.cpp`
- Modify: root `CMakeLists.txt` (`find_package` adds `Widgets`; `add_subdirectory(app)`), `tools/run-tests.ps1` (runs `core-tests` and `app-tests`, sums failures, prints `TOTAL failures: N`, and fails if any expected result file is missing — review minor 14), `.github/workflows/ci.yml` (no change needed if it calls `run-tests.ps1`)

**Interfaces:**
- Produces: targets `noite-app` (STATIC: everything in `app/src` except `main.cpp`; links `noite-core Qt6::Widgets user32`), `noite` (WIN32), `app-tests` (links `noite-app Qt6::Test`). Test registry macro `NOITE_APP_TEST(Class)`.

- [ ] **Step 1: Failing test** `tst_smoke.cpp`: `void appLibraryLinks()` constructs `App` from `AppOptions{}` and asserts nothing else. It fails to compile until `App`/`AppOptions` exist.
- [ ] **Step 2: RED.** `tools/build.ps1 -Config Debug -Test` → compile error.
- [ ] **Step 3: Implement the skeleton**, then **Step 4: GREEN.** Expected: `TOTAL failures: 0`, with both `core-tests` and `app-tests` results printed.
- [ ] **Step 5: Commit** with `feat(app): application skeleton and two-suite test runner`.

### Task 2: Hotkey parsing

**Files:** Create `app/src/platform/Hotkey.h/.cpp`, `app/tests/tst_hotkey.cpp`

**Interfaces:**
- Produces: `struct Hotkey { unsigned modifiers{}; unsigned vk{}; bool operator==(const Hotkey&) const = default; };` (`MOD_*` flags without `MOD_NOREPEAT`, Win32 VK code) and `std::optional<Hotkey> parseHotkey(const QString& text)`.
- Rules:
  - Tokens are split on `+`, case-insensitive, trimmed.
  - Modifiers are `Ctrl`/`Control`, `Alt`, `Shift` and `Win`/`Meta`, each at most once.
  - Exactly one key: `A`–`Z`, `0`–`9`, `F1`–`F24`, `Space`.
  - At least one of Ctrl, Alt or Win is required (Shift alone isn't enough, since it would collide with typing).

- [ ] **Step 1: Failing tests:**
  - `parsesCommonCombos`: `Ctrl+Alt+J` → {CONTROL|ALT, 'J'}; `ctrl + shift + space` → {CONTROL|SHIFT, VK_SPACE}; `Win+Shift+N`; `Ctrl+Alt+F12` → VK_F12
  - `rejectsInvalid`: `""`, `"J"`, `"Shift+J"`, `"Ctrl+Alt"`, `"Ctrl+Ctrl+J"`, `"Ctrl+Alt+J+K"`, `"Ctrl+Alt+Ä"`, `"Ctrl+Alt+F25"`
- [ ] **Steps 2–4:** RED, implement, GREEN. **Step 5: Commit** with `feat(app): parse hotkey strings into Win32 modifiers and keys`.

### Task 3: NativeHost: message-only window, hotkeys, second-instance activation

**Files:** Create `app/src/platform/NativeHost.h/.cpp`, `app/tests/tst_native_host.cpp`

**Interfaces:**
- Produces:
  - `QString instanceKeyFor(const QString& dataRoot)`: the first 16 hex characters of the SHA-1 of the lowercased, cleaned absolute path.
  - `class NativeHost : public QObject` with:
    - `explicit NativeHost(const QString& instanceKey, QObject* parent = nullptr)`, `~NativeHost()`, which unregisters everything
    - `bool registerHotkey(int id, Hotkey)`, which adds `MOD_NOREPEAT`; `void unregisterHotkey(int id)`
    - `static bool activateRunningInstance(const QString& instanceKey)`
    - signals `void hotkeyPressed(int id)` and `void activationRequested()`
- Rules:
  - The window is created with `CreateWindowExW(0, L"NoiteHostWindow", L"noite-host-<key>", 0, 0,0,0,0, HWND_MESSAGE, …)`.
  - Its WndProc forwards `WM_HOTKEY` and the `RegisterWindowMessageW(L"noite.activate")` message to the object (stored in `GWLP_USERDATA`).
  - `activateRunningInstance` uses `FindWindowExW(HWND_MESSAGE, nullptr, L"NoiteHostWindow", L"noite-host-<key>")` + `PostMessageW`, and returns whether a window was found.
  - Messages go to a window procedure, so any modal loop still dispatches them.

- [ ] **Step 1: Failing tests** (no OS input; messages are posted directly to our own window):
  - `hotkeyMessageEmitsSignal`: `PostMessageW(hostHwnd, WM_HOTKEY, 7, 0)`, then `QTRY_COMPARE(spy.count(), 1)` with arg 7
  - `activationReachesRunningInstance`: host A(key K) exists → `activateRunningInstance(K)` returns true and A emits `activationRequested`; `activateRunningInstance("unknown")` returns false
  - `registerConflictIsReported`: A registers `Ctrl+Alt+Shift+F12` (id 1) → true; B (other key) registering the same → false; after A unregisters, B succeeds. This holds a rarely used global combination for a moment; it sends no input.
  - `instanceKeyIsStableAndPathInsensitive`: the same key for `C:/X/data`, `c:\x\data\` and `C:\x\.\data`; a different key for `C:\y\data`
- [ ] **Steps 2–4:** RED, implement, GREEN. **Step 5: Commit** with `feat(app): message-only native host for hotkeys and single-instance activation`.

### Task 4: Port and harden the editor

**Files:** Create `app/src/editor/{MarkdownShortcuts,NoteEdit}.{h,cpp}` from `spikes/qt-capture/src/`, plus `app/tests/tst_editor.cpp` ported from `spikes/qt-capture/tests/tst_editor.cpp`.

**Interfaces:**
- Produces: `NoteEdit` and `MarkdownShortcuts` with the spike's public API **minus** `takeKeyLog()`/`setTrials()` and **plus**:
  - `void NoteEdit::setToday(std::function<QDate()>)` (used in Task 6)
  - the signal `void NoteEdit::escapePressed()`, kept as-is
- Port: every behavioural spike test except `checkboxRenders` (an investigation, not a check). Image-paste tests move to Task 5, where images go through `AttachmentStore`.
- Hardening (each with a new failing test first):
  - `imeCommitTriggersShortcuts`: a `QInputMethodEvent` with commit string `" "` after typing `#` gives a heading. Shortcuts run from `inputMethodEvent` too.
  - `headingSizeIsRelative`: after `# T` the block char format has `FontSizeAdjustment` == 2 (H1), 1 (H2), 0 (H3) plus bold, and **no** `FontPointSize` property.
  - `linksStoreNoColour`: `[a](https://x.y)`, a typed autolink and a pasted URL all produce anchors with **no** `ForegroundBrush` property.
  - `autolinkExcludesTrailingPunctuation`: `(see https://x.com). ` → href `https://x.com`; `https://x.com, next` → href `https://x.com`.

- [ ] **Step 1: Port the spike tests and write the four hardening tests.** **Step 2: RED** (missing files, then the four hardening failures). **Step 3: Port the code and implement the hardening.** **Step 4: GREEN** (all ported and new tests).
- [ ] **Step 5: Commit** with `feat(app): editor ported from the Qt spike with IME, relative headings and colourless links`.

### Task 5: Attachments and paste clean-up

**Files:** Create `app/src/editor/AttachmentStore.h`, `app/src/editor/PasteCleaner.h/.cpp`, `app/tests/tst_paste.cpp`; modify `NoteEdit`.

**Interfaces:**
- Produces:
  - `struct AttachmentStore { virtual ~AttachmentStore() = default; virtual QString put(const QByteArray& bytes, const QString& ext) = 0; virtual QString pathFor(const QString& id) const = 0; };`
  - `class StoreAttachments final : public AttachmentStore`, wrapping `noite::Store&`
  - `void NoteEdit::setAttachmentStore(AttachmentStore*)`
  - `QString PasteCleaner::clean(const QString& html, AttachmentStore& store)`, which returns cleaned HTML
- Rules:
  - **`loadResource`:** `NoteEdit::loadResource` resolves `noite-att:<id>` to `QImage(pathFor(id))` for `ImageResource`.
  - **Image paste:** keeps the source encoding when the clipboard provides PNG/JPEG/GIF bytes (`image/png` / `image/jpeg` / `image/gif` MIME data). Otherwise it encodes a PNG. Either way the result is `put` and inserted as `noite-att:<id>`.
  - **Copied files** (`hasUrls` with local files):
    - each file ≤ `kMaxAttachmentBytes` is `put` and inserted as a link: text = file name, href = `noite-att:<id>`;
    - a larger file is inserted as plain text `"<name> (too large to attach)"`.
  - **`PasteCleaner`** removes:
    - `style` properties `color`, `background`, `background-color`, `font-family`, `font-size`, `font`;
    - `<font>` attributes;
    - `bgcolor`.

    It keeps structural tags. `<img src>` handling:
    - `data:` → decode and `put`;
    - `file:` → read and `put` (if ≤ limit);
    - `http(s):` → replace with `<a href=URL>alt or "image"</a>`;
    - anything else → drop.
  - `isSafeLink` additionally allows `noite-att`. Ctrl+click opens `QUrl::fromLocalFile(pathFor(id))`.

- [ ] **Step 1: Failing tests** (a `FakeAttachments` test double stores into a `QTemporaryDir`):
  - `pastedImageBecomesStoredAttachment`
  - `pastedPngBytesKeptAsIs`: the stored bytes equal the clipboard's `image/png` data
  - `reloadedNoteShowsImage`: `setHtml` with `noite-att:<id>` → the resource loads as a non-null `QImage`
  - `htmlPasteStripsColoursFontsSizes`: the fragments have no `ForegroundBrush`, `BackgroundBrush`, `FontFamilies` or `FontPointSize`; bold, link, list and table survive
  - `htmlPasteCapturesDataUriImage`
  - `htmlPasteCapturesLocalFileImage`
  - `htmlPasteRemoteImageBecomesLink`
  - `copiedFileBecomesChip`
  - `oversizedFileNotAttached`
  - `noiteAttLinksAreSafe`
- [ ] **Steps 2–4:** RED, implement, GREEN. **Step 5: Commit** with `feat(app): attachments in the editor and paste clean-up`.

### Task 6: Due-date tokens

**Files:** Create `app/src/editor/DateTokens.h/.cpp`, `app/tests/tst_dates.cpp`; modify `MarkdownShortcuts`/`NoteEdit`.

**Interfaces:**
- Produces:
  - `std::optional<QDate> parseDateToken(const QString& token, QDate today)`, where `token` excludes the `@`
  - the signal `void NoteEdit::dueDateTyped(QDate)`
- Rules:
  - **Matching:** case-insensitive, accents ignored.
  - **Words:** `today`/`hoje`; `tomorrow`/`amanha`/`amanhã`.
  - **Weekdays:** `mon tue wed thu fri sat sun` / `seg ter qua qui sex sab sáb dom` give the next occurrence **including today** (D9 example `@fri`; pt-BR aliases because that's the author's locale).
  - **Numeric dates:** `YYYY-MM-DD`; `DD/MM` (this year, or next year if already past); `DD/MM/YYYY`. Anything else → `nullopt`.
  - **Editor:** typing a space after `@token`, where the `@` is at the start of the text or after whitespace and the token parses, removes `@token` (keeping the space) and emits `dueDateTyped`. An unparseable `@word` stays as text.

- [ ] **Step 1: Failing tests:**
  - `wordsAndWeekdays`: today = Thu 2026-10-15; `@fri` → 10-16; `@thu` → 10-15; `@sex` → 10-16; `@amanhã` → 10-16
  - `numericForms`: `2026-12-01`, `20/10` → 2026-10-20, `01/02` → 2027-02-01, `31/02` → nullopt
  - `typingTokenSetsDueAndRemovesText`: keyClicks `"call bob @tomorrow x"` → plain text `"call bob x"`, the signal fired once with 10-16
  - `unknownTokenStaysText`: `"@someone "` stays
  - `emailIsNotAToken`: `"a@fri.com "` stays
- [ ] **Steps 2–4:** RED, implement, GREEN. **Step 5: Commit** with `feat(app): @date tokens set the due date`.

### Task 7: CaptureController

**Files:** Create `app/src/capture/CaptureController.h/.cpp`, `app/tests/tst_capture_controller.cpp`

**Interfaces:**
- Consumes: `noite::Store` (M1), `NoteEdit` (Tasks 4–6).
- Produces `class CaptureController : public QObject`:
  - `CaptureController(noite::Store&, NoteEdit&, std::function<QDate()> today, QObject* parent = nullptr)`
  - `void begin()`: clears the editor; no scratch exists yet
  - `void flush()`: saves now if an edit is pending
  - `void dismiss()`: `flush()`, then `discardIfEmpty`; emits `finished`
  - `void markDoneAndClose()`, `void cycleColor()`, `void setDue(std::optional<QDate>)`
  - `std::optional<noite::ScratchId> currentId() const`
  - `static constexpr int kAutosaveMs = 400`
  - signals `finished()`, `dueChanged(std::optional<QDate>)`, `colorChanged(std::optional<noite::Color>)`, `errorOccurred(QString)`
- Rules:
  - **Scratch creation:** a scratch is created on the first save whose HTML isn't `content::isEmpty`; later saves call `updateContent`.
  - **Autosave:** each `contentsChanged` restarts a single-shot timer.
  - **Due and colour before creation:** a due date or colour set before the scratch exists is kept pending and applied after `createScratch`.
  - **Colour cycle:** none → red → orange → yellow → green → blue → purple → none.
  - **`markDoneAndClose`:** flush; if a scratch exists, `markDone`; then `finished`.
  - **Errors:** a `noite::Error` from the store is reported through `qWarning` and the `errorOccurred(QString)` signal. The editor text is never cleared on a failed save.

- [ ] **Step 1: Failing tests** (real `Store` on a temp dir; `NoteEdit` offscreen):
  - `noScratchUntilFirstEdit`
  - `typingSavesAfterDebounce`: `QTRY_VERIFY` within 1 s
  - `dismissFlushesPendingEdit`: type, then dismiss immediately → stored text equals the editor's (Review Focus 1)
  - `dismissDiscardsEmptied`: type, select all, delete, dismiss → `list(All)` empty
  - `markDoneAndClose`: status Done, `finished` emitted
  - `dueTokenBeforeFirstSaveIsApplied`
  - `colourCycles`
  - `imageOnlyNoteIsKept`
- [ ] **Steps 2–4:** RED, implement, GREEN. **Step 5: Commit** with `feat(app): capture controller with debounced autosave and discard-if-empty`.

### Task 8: CaptureWindow, App wiring, tray, data folder

**Files:** Create `app/src/capture/CaptureWindow.h/.cpp`, `app/src/AppOptions.h/.cpp`; fill in `App`; create `app/tests/tst_capture_window.cpp` and `app/tests/tst_app.cpp`.

**Interfaces:**
- Produces:
  - `struct AppOptions { QString dataDir; }`, `AppOptions parseArgs(const QStringList&)` (`--data <dir>`), and `QString resolveDataDir(const AppOptions&, const QString& exeDir)`, which defaults to `exeDir + "/data"`
  - `class CaptureWindow : public QWidget`:
    - `CaptureWindow(CaptureController&, NoteEdit*)`
    - `void summon()`: the spike's foreground-monitor centring; if already visible, refocus only; otherwise `controller.begin()` and show
    - footer label showing the due date and colour
  - `class App : public QObject`:
    - `explicit App(AppOptions)`
    - `std::optional<int> start()`: does all startup and returns an exit code only if the process should end right away (second instance → 0)
    - `int run()` = `start()`, then `QApplication::exec()`. Tests call `start()`, so they never block in an event loop.
    - signal `void hotkeyUnavailable(QString combo)`, which the tray shows as a balloon
- Rules:
  - **Startup order:**
    1. Resolve the data dir.
    2. `DataDir::acquire`. On `AlreadyRunning`, call `NativeHost::activateRunningInstance(instanceKeyFor(dir))` and return 0.
    3. `Store::open`.
    4. `NativeHost(instanceKeyFor(root))`.
    5. Register `parseHotkey(settings.captureHotkey)`; on failure emit `hotkeyUnavailable` and continue.
    6. Tray icon with menu "New note" / "Quit".
    7. `hotkeyPressed` and `activationRequested` → `capture.summon()`.
  - **Window keys:** `Esc` → `dismiss`; `Ctrl+Enter` → `markDoneAndClose`; `Ctrl+Shift+C` → `cycleColor`; `QEvent::WindowDeactivate` while visible → `dismiss` (D6). `finished` → `hide()`.
  - **Quit:** flushes any open capture, then exits.

- [ ] **Step 1: Failing tests:**
  - `tst_capture_window`:
    - `escDismissesAndHides`
    - `ctrlEnterMarksDone`
    - `deactivationDismisses`: send `QEvent::WindowDeactivate`
    - `colourKeyCycles`
    - `summonWhileVisibleKeepsText`
  - `tst_app`:
    - `parseArgsReadsData`
    - `defaultDataDirIsNextToExe`
    - `nonAsciiDataDirWorks`: open `Store` and create/get a note under `QTemporaryDir/João/noite/data` (Review Focus 5)
    - `secondAppOnSameDataDirActivatesFirst`: App A `start()`ed on dir X; App B on X → `start()` returns 0 and A emits `activationRequested` (`QTRY_COMPARE`) (Review Focus 4)
    - `unavailableHotkeyKeepsRunning`: pre-register the settings' combination with another `NativeHost`; App startup emits `hotkeyUnavailable` and stays usable (Review Focus 3)

  The tray icon is created only when `QSystemTrayIcon::isSystemTrayAvailable()`. It isn't on offscreen, so tests don't touch the real tray.
- [ ] **Steps 2–4:** RED, implement, GREEN. **Step 5: Commit** with `feat(app): capture window, tray and single-instance app wiring`.

### Task 9: Latency probe, portable deploy, harness run

**Files:** Create `app/src/capture/LatencyProbe.h/.cpp`, `tools/deploy.ps1`, `app/tests/tst_latency_probe.cpp`; modify `spikes/hotkey-latency/run-trials.ps1` only if its `-Process` default needs `noite`.

**Interfaces:**
- Produces `class LatencyProbe`:
  - `static std::unique_ptr<LatencyProbe> fromEnvironment()`: returns null unless `NOITE_LATENCY_CSV` is set
  - `void pressed()`, `void painted()`, `void dismissed(const QString& text)`
- Rules:
  - **Ready time:** `painted()` posts a zero-timer and records the time **after the backing-store flush** (review recommendation: comparable to the Tauri method).
  - **CSV:** rows use the harness format `trial,ready_ms,text`. The probe is off by default; with no env var, nothing is written.
- **`tools/deploy.ps1 -Config Release`:**
  - copies `noite.exe` to `dist/noite/`, runs `windeployqt --release --no-translations --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw --no-compiler-runtime`;
  - copies `msvcp140*.dll`, `vcruntime140*.dll` and `concrt140.dll` from `$env:VCToolsRedistDir\x64\Microsoft.VC*.CRT\`;
  - deletes any `vc_redist*.exe`;
  - prints the file count and size, and fails if the size exceeds **45 MB**.

- [ ] **Step 1: Failing tests:** `probeDisabledWithoutEnv`; `probeWritesHarnessRows` (env set to a temp file → pressed/painted/dismissed → one row with a non-empty `ready_ms` and the text).
- [ ] **Steps 2–4:** RED, implement, GREEN; wire the probe into `CaptureWindow`/`NoteEdit::paintEvent`.
- [ ] **Step 5: Deploy.** Run `tools/deploy.ps1 -Config Release`. Expected: size ≤ 45 MB (record the actual number; the spike estimated ~33 MB).
- [ ] **Step 6 (user away, ask first): harness.** Start `dist/noite/noite.exe --data <temp>` with `NOITE_LATENCY_CSV=<temp>/trials.csv`. Then `run-trials.ps1 -Process noite -Csv <temp>/trials.csv`: 5-trial dry run, leak control (`-Hotkey x`), 50 warm trials, memory after 5 min idle.
  - Expected: leak control detects `xhello`; warm p95 ≤ 100 ms; 0 failed or leaked; private memory ≤ 80 MB.
  - Also: launching a second `noite.exe --data <same>` brings up the capture window, and the second process exits 0.
- [ ] **Step 7: Commit** with `feat(app): latency probe and trimmed portable deploy`, recording the numbers in the commit body.

### Task 10: Idle first-summon mitigation

**Files:** Create `app/src/capture/KeepWarm.h/.cpp`, `app/tests/tst_keep_warm.cpp`; modify `App`.

**Interfaces:**
- Produces `class KeepWarm : public QObject`:
  - `KeepWarm(QWidget* window, std::chrono::milliseconds interval = 4min)`
  - `void start()`, `void stop()`, `int ticks() const`
  - while the window is hidden, each tick calls `window->grab()` into a scratch `QPixmap` and drops it, which keeps fonts, glyph caches and the widget paint path resident
  - it's controlled by env `NOITE_KEEP_WARM=0/1` (default 1) so it can be compared

- [ ] **Step 1: Failing tests:** `ticksOnlyWhileHidden` (short interval, a hidden widget → ticks increase; shown → no new ticks); `disabledByEnv`.
- [ ] **Steps 2–4:** RED, implement, GREEN.
- [ ] **Step 5 (user away, ~35 min, ask first): measure.**
  - Run two 15-minute idle runs with 10 trials each: `NOITE_KEEP_WARM=0`, then `=1`.
  - Record the first-trial and p95 times for both.
  - **Gate:** with keep-warm, first summon ≤ 50 ms.
  - If keep-warm doesn't help, record that. Then try a working-set floor (`SetProcessWorkingSetSizeEx` with `QUOTA_LIMITS_HARDWS_MIN_ENABLE`, min = current + 8 MB) as the next candidate, measured the same way. If neither helps, leave both off and record the decision.
- [ ] **Step 6: Commit** with `perf(app): keep the capture path warm while hidden`, including the measurements.

### Task 11: CI and the manual checklist

**Files:** Create `docs/testing/manual-capture-checks.md`; modify `.github/workflows/ci.yml` (the job name now covers core + app).

- [ ] **Step 1: Write the checklist** (reusable at every release), one line each with pass/fail:
  - hotkey from an elevated app
  - hotkey while open
  - typed markdown: headings, lists, `[]`, bold/italic/code, links
  - `@tomorrow` / `@sex` / `20/10`
  - paste from a browser (with images) and from Word (with images)
  - paste a copied file → chip opens with Ctrl+click
  - pasted text readable in dark mode
  - click-away saves
  - `Ctrl+Enter` marks done
  - `Ctrl+Shift+C` colour
  - tray "New note"
  - second launch focuses the first
  - second monitor (when available)
  - crisp at display scale
- [ ] **Step 2: Push and watch CI** (as in M1). Expected: green; the artifact contains both suites' results.
- [ ] **Step 3 (user): run the checklist** on the deployed build and record the results in the ledger. Each failure becomes a RED test + fix before the branch is finished.
- [ ] **Step 4: Commit** with `docs: manual capture checklist; ci: core and app tests`.
