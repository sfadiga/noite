# M6: Setup Wizard and Settings — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A first-run wizard walks through every D24 setting and ends in the real capture window with a starter note. A one-page Settings window changes everything later, live. The tray gains Settings, and the sync-folder warning is shown.

**Architecture:**
- **Hotkeys:** the app's two global hotkeys move into a `HotkeyManager` behind a `HotkeyBackend` interface (NativeHost in the app, a fake in tests).
- **Shared fields:** `HotkeyField` and `BackupFolderField` are used by both a `QWizard` (`SetupWizard`) and a `QDialog` (`SettingsDialog`).
- **Applying:** both windows apply through one `SettingsApplier`.
- **App:** shows the wizard while `setupCompleted` is false and defers the review and the job timers until it closes.

**Tech Stack:** C++20, Qt 6.10 Widgets/Test (`QWizard`, `QDialog`), Win32 `RegisterHotKey` through the existing `NativeHost`.

**Spec:** `docs/superpowers/specs/2026-10-18-m6-setup-settings-design.md` (S1–S7; project spec D24, D25, D29, D33)

## Global Constraints

- Hotkey ids: `kCaptureId = 1`, `kBoardId = 2` (the existing `App::kCaptureHotkeyId`/`kBoardHotkeyId`), probe id `kProbeId = 99`.
- Valid recorded combination: Ctrl and/or Alt (Shift optional) plus one key from A–Z, 0–9, F1–F24, Space; Win is never produced; text in `parseHotkey` form (`"Ctrl+Alt+J"`, modifier order Ctrl, Alt, Shift).
- Hotkey field messages:
  - free: `✓ Ctrl+Alt+J`
  - taken: `Ctrl+Alt+N is used by another program. Try another.`
  - invalid: `Use Ctrl or Alt with a key.`
  - duplicates: `That's already the board hotkey.` / `That's already the new-note hotkey.`
- Wizard: 8 pages in spec order. Buttons Back / Next / Finish, plus `Skip setup` on every page except page 2. Window title `Set up noite`.
- Page texts:
  - page 1: `Your notes live in this folder: <data dir>. Nothing leaves your PC.`
  - page 4: `Keep trash for [30] days` (1–365)
  - page 5: `Day starts at [06:00]` and `Bring back untouched notes after [7] days` (1–90)
  - page 7: `Start noite with Windows`
  - page 8: `Press <capture hotkey> now.`
- Settings: window title `noite settings`.
  - Groups: `Hotkeys` (`New note`, `Board`), `Notes` (`Keep trash for`, `Day starts at`, `Resurface untouched`), `Backup`, `System`.
  - Buttons: `Change…`, `Back up now`, `Open logs folder`, `Run setup again`, `Cancel`, `Save`.
  - Last-backup line `Last backup: <text>`, where `<text>` is one of `today`, `yesterday`, `MMM d` (English locale), `never`, `failing since MMM d`.
- Unset backup folder text: `Not set (Back up now will ask)`.
- Sync warning: `This folder is inside <Google Drive|OneDrive|Dropbox>. Syncing a live database can corrupt it. Move the noite folder somewhere that isn't synced; backups can still go to Drive.`
- Starter note: verbatim from the spec's "Texts" section, with the real capture hotkey, board hotkey and `staleDays`.
- Tray order: `New note` · `Board` · `Morning review` · `Start with Windows` · `Settings` · `Back up now` · `Quit` (separators as today; Settings joins the Start-with-Windows group).
- Test command: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -Config Debug -Test` → `TOTAL failures: N`.
- Edit files only with the Edit/Write tools. Never register real global hotkeys in tests: use the fake backend.

## Review Focus

1. **Swapping the two hotkeys in Settings** (capture ↔ board) → Save works, instead of failing with "already the board hotkey" halfway through. Test: Task 1 `applySwapsBoth` and Task 4 `swapHotkeysSaves`.
2. **Alt+Tab away while a hotkey field is recording** → recording stops and noite's hotkeys are resumed, so the user isn't left without hotkeys. Test: Task 3 `focusOutResumesHotkeys`.
3. **Esc while a hotkey field is recording** → cancels the recording and restores the previous combination; it does not close the dialog or the wizard. Test: Task 3 `escCancelsRecording`.
4. **Day start moved later than now in Settings, before today's review** → the review waits for the new time instead of opening now. Test: Task 4 `dayStartChangeRechecksReview`.
5. **Capture hotkey pressed during the wizard on a page other than 8** → nothing happens: no capture window over the wizard, no finish. Test: Task 5 `captureHotkeyIgnoredBeforeLastPage`.

---

### Task 1: comboFromKey, HotkeyBackend, HotkeyManager

**Files:**
- Create: `app/src/platform/HotkeyManager.h/.cpp`, `app/tests/tst_hotkey_manager.cpp`
- Modify:
  - `app/src/platform/Hotkey.h/.cpp` (`comboFromKey`)
  - `app/src/platform/NativeHost.h` (inherit `HotkeyBackend`)
  - `app/src/App.h/.cpp` (`registerHotkey` replaced by the manager)
  - `app/tests/tst_hotkey.cpp`
  - `app/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `std::optional<QString> comboFromKey(int qtKey, Qt::KeyboardModifiers mods)`
  - `struct HotkeyBackend { virtual bool registerHotkey(int id, Hotkey) = 0; virtual void unregisterHotkey(int id) = 0; virtual ~HotkeyBackend() = default; };` (NativeHost's existing methods become the overrides)
  - `enum class HotkeyStatus { Ok, Taken, Invalid, SameAsOther };`
  - `struct HotkeyOutcome { HotkeyStatus status = HotkeyStatus::Ok; int failedId = 0; };`
  - `class HotkeyManager`, with `explicit HotkeyManager(HotkeyBackend&)` and constants `kCaptureId`, `kBoardId`, `kProbeId`:
    - `HotkeyStatus set(int id, const QString& combo)`: one id, independent of the other (startup and wizard pages 2/3); on failure the previous combination for that id stays registered; `SameAsOther` when equal to the other id's combination
    - `HotkeyOutcome apply(const QString& capture, const QString& board)`: all-or-nothing for Settings; unregisters both, registers both, restores both on any failure
    - `HotkeyStatus probe(const QString& combo)`: register on `kProbeId` and unregister at once; never leaves it registered
    - `void pause()` (unregisters both; idempotent), `void resume()` (re-registers the current combinations), `bool isPaused() const`
    - `QString combo(int id) const`, `bool isRegistered(int id) const`
  - `QString hotkeyMessage(HotkeyStatus status, const QString& combo, int otherId)`: the Global Constraints texts (`otherId` picks "board" / "new-note" for `SameAsOther`)
  - `App` keeps `hotkeyUnavailable(QString)`, emitted when a startup `set` fails; `App::hotkeys()` accessor.

- [ ] **Step 1: Write the failing tests**

`tst_hotkey.cpp` (extended):
```cpp
void comboFromKeyFormats() // (Qt::Key_J, Ctrl|Alt) == "Ctrl+Alt+J"; (Key_F5, Ctrl) == "Ctrl+F5"; (Key_Space, Alt|Shift) == "Alt+Shift+Space";
                           // (Key_J, Shift) and (Key_J, none) and (Key_Escape, Ctrl) and (Key_Control, Ctrl) → nullopt;
                           // (Key_J, Ctrl|Meta) → "Ctrl+J" (Win dropped); every non-null result: parseHotkey(*r) has a value
```
`tst_hotkey_manager.cpp` (a `FakeBackend` recording registrations, with a `taken` set of combos):
```cpp
void setRegistersAndKeepsOldOnFailure() // set(1,"Ctrl+Alt+J") Ok; taken {"Ctrl+Alt+N"}; set(1,"Ctrl+Alt+N") Taken → combo(1) "Ctrl+Alt+J", still registered
void setRejectsInvalidAndSame()        // set(1,"J") Invalid; set(2,"Ctrl+Alt+B") Ok; set(1,"Ctrl+Alt+B") SameAsOther
void applySwapsBoth()                   // 1=J, 2=B → apply("Ctrl+Alt+B","Ctrl+Alt+J") {Ok}; combo(1) B, combo(2) J
void applyIsAllOrNothing()              // taken {"Ctrl+Alt+K"} → apply("Ctrl+Alt+J","Ctrl+Alt+K") {Taken, failedId 2}; both old still registered
void probeLeavesNothing()               // probe("Ctrl+Alt+K") Ok and backend has no id 99; taken → Taken; "J" → Invalid
void pauseResume()                      // pause → backend has neither id; pause again no-op; resume → both back
void messages()                         // hotkeyMessage(Ok,"Ctrl+Alt+J",2) == "✓ Ctrl+Alt+J"; Taken/Invalid/SameAsOther texts verbatim
```

- [ ] **Step 2: Run to verify they fail**

Run: the test command. Expected: compile errors (`comboFromKey`, `HotkeyManager`).

- [ ] **Step 3: Implement**

- **`comboFromKey`:** maps Qt keys to the parser's names: `Key_A`..`Key_Z`, `Key_0`..`Key_9`, `Key_F1`..`Key_F24`, `Key_Space`. Modifiers come out in the order Ctrl, Alt, Shift.
- **`App`:** `start()` creates the manager over `*host_` and calls `set()` for both ids from the settings, emitting `hotkeyUnavailable` on failure (the same behaviour as today). The `hotkeyPressed` handler is unchanged.

- [ ] **Step 4: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0` (existing `tst_app` hotkey tests included).

- [ ] **Step 5: Commit**

```bash
git add app
git commit -m "feat(platform): hotkey manager with probe, pause, all-or-nothing apply; combo from key events"
```

---

### Task 2: SetupTexts and CaptureWindow::summonNote

**Files:**
- Create: `app/src/setup/SetupTexts.h/.cpp`, `app/tests/tst_setup_texts.cpp`
- Modify: `app/src/capture/CaptureWindow.h/.cpp`, `app/tests/tst_capture_window.cpp`, `app/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `QString starterNoteHtml(const noite::Settings&)`: a bold first line `Welcome to noite` then a bulleted list, text verbatim from the spec, with the capture hotkey, board hotkey and staleDays substituted
  - `QString lastBackupText(const noite::JobState&, QDate today)`: `today` / `yesterday` / `MMM d` / `never` / `failing since MMM d` (English locale; failing wins over the day)
  - `QString syncWarningText(noite::SyncProvider)`
  - `void CaptureWindow::summonNote(const noite::ScratchId& id)`: opens an existing note (via `CaptureController::open`) and shows it exactly as `summon()` shows a blank one

- [ ] **Step 1: Write the failing tests**

```cpp
void starterNoteUsesSettings()   // Settings with captureHotkey "Ctrl+Alt+K", boardHotkey "Ctrl+Alt+L", staleDays 9 → plain text of
                                 // starterNoteHtml contains "Welcome to noite", "Ctrl+Alt+K opens this window", "Ctrl+Alt+L opens the board",
                                 // "untouched for 9 days"; Store::createScratch(html) → title "Welcome to noite"
void lastBackupTexts()           // today 2026-10-18: lastBackupDay 10-18 → "today"; 10-17 → "yesterday"; 10-15 → "Oct 15"; none → "never";
                                 // backupFailingSinceMs = 10-15 09:00 local → "failing since Oct 15"
void syncWarningNamesProvider()  // GoogleDrive → contains "inside Google Drive."; OneDrive; Dropbox
```
`tst_capture_window.cpp`:
```cpp
void summonNoteOpensExisting()   // create a scratch "hello"; summonNote(id) → window visible, editor plain text "hello";
                                 // type " world", Esc → stored content contains "hello world"
```

- [ ] **Step 2: Run to verify they fail**

Run: the test command. Expected: compile errors.

- [ ] **Step 3: Implement**

Pure functions. `summonNote` shares the positioning and show code with `summon()`: factor it into one private helper.

- [ ] **Step 4: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`.

- [ ] **Step 5: Commit**

```bash
git add app
git commit -m "feat(setup): starter note, last-backup and sync-warning texts; capture window opens an existing note"
```

---

### Task 3: HotkeyField and BackupFolderField

**Files:**
- Create: `app/src/setup/HotkeyField.h/.cpp`, `app/src/setup/BackupFolderField.h/.cpp`, `app/tests/tst_hotkey_field.cpp`, `app/tests/tst_backup_folder_field.cpp`
- Modify: `app/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1's `HotkeyManager` (`probe`, `pause`, `resume`, `hotkeyMessage`), `comboFromKey`.
- Produces:
  - `class HotkeyField : public QWidget`, constructed as `HotkeyField(HotkeyManager&, int id, QWidget* parent = nullptr)`:
    - `void setCombo(const QString&)` (shown as accepted, no probe)
    - `QString combo() const`, `bool isAccepted() const`
    - `void setOther(const QString& combo)` (the other hotkey's current combination, for `SameAsOther`)
    - `bool isRecording() const`, `QString statusText() const`
    - signal `changed()`
    - Behaviour:
      - Clicking or focusing the key box starts recording, calling `pause()`.
      - A key event goes through `comboFromKey`: invalid gives the invalid message, equal to the other gives SameAsOther, otherwise `probe`.
      - Recording ends on an accepted combination, on Esc (restores the previous combination and status) or on focus-out (keeps the previous one). Ending calls `resume()`.
  - `class BackupFolderField : public QWidget`:
    - `explicit BackupFolderField(std::function<std::optional<QString>()> askFolder, QWidget* parent = nullptr)`
    - `struct Values { std::optional<QString> folder; noite::BackupFrequency frequency; int keep; };` with `setValues(const Values&)`, `Values values() const`
    - `QString folderText() const` (native separators, or the unset text)
    - Controls: `Change…` calls `askFolder` (nullopt keeps the old value); frequency combo `day` / `week`; keep spin 1–999.

- [ ] **Step 1: Write the failing tests** (FakeBackend from Task 1's test, moved to a shared `app/tests/fake_hotkeys.h`)

`tst_hotkey_field.cpp`:
```cpp
void acceptsFreeCombo()          // click field → manager.isPaused(); press Ctrl+Alt+K → isAccepted, combo "Ctrl+Alt+K",
                                 // statusText "✓ Ctrl+Alt+K", !isRecording, !isPaused; changed emitted once
void rejectsTakenAndInvalid()    // taken {"Ctrl+Alt+N"}: Ctrl+Alt+N → statusText "Ctrl+Alt+N is used by another program. Try another.",
                                 // !isAccepted, still recording; Shift+J → "Use Ctrl or Alt with a key."
void rejectsOther()              // id 1, setOther("Ctrl+Alt+B"); press Ctrl+Alt+B → "That's already the board hotkey."
void escCancelsRecording()       // setCombo("Ctrl+Alt+J"); click; Esc → combo "Ctrl+Alt+J", isAccepted, !isRecording, !isPaused;
                                 // the Esc key event is accepted by the field (a parent QDialog stays open: put the field in one and check)
void focusOutResumesHotkeys()    // click (paused); move focus to another widget → !isRecording, !isPaused, combo unchanged
```
`tst_backup_folder_field.cpp`:
```cpp
void valuesRoundTrip()           // setValues({"G:/Meu Drive", Weekly, 9}) → values() equal; folderText "G:\\Meu Drive"
void changeUsesAsker()           // asker returns "D:/bk" → click Change… → values().folder "D:/bk"; asker returns nullopt → unchanged
void unsetText()                 // folder nullopt → folderText "Not set (Back up now will ask)"
```

- [ ] **Step 2: Run to verify they fail**

Run: the test command. Expected: compile errors.

- [ ] **Step 3: Implement**

The key box can be a read-only `QLineEdit` subclass or a focusable `QWidget` that reimplements `keyPressEvent` / `focusInEvent` / `focusOutEvent` (and `event()` for Tab, so Tab is recorded only with Ctrl or Alt held, otherwise it moves focus). The status label sits beside it.

- [ ] **Step 4: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`.

- [ ] **Step 5: Commit**

```bash
git add app
git commit -m "feat(setup): hotkey field (record, probe, Esc/focus-out restore) and backup folder field"
```

---

### Task 4: SettingsApplier

**Files:**
- Create: `app/src/setup/SettingsApplier.h/.cpp`, `app/tests/tst_settings_applier.cpp`
- Modify: `app/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1's `HotkeyManager::apply`, `hotkeyMessage`; `Store::settings` / `saveSettings`; `ReviewScheduler::check` / `msUntilNextCheck`.
- Produces:
  - `struct ApplyError { QString message; int hotkeyId = 0; }` (0 = not a hotkey error)
  - `class SettingsApplier`, constructed as `SettingsApplier(noite::Store&, HotkeyManager&, std::function<void(bool)> applyAutostart, std::function<void()> recheckReview)`
  - `std::optional<ApplyError> apply(const noite::Settings& next)`: steps in order:
    1. If either hotkey differs from the manager's current combination, call `apply(capture, board)`. On failure return `{hotkeyMessage(...), failedId}` and change nothing else.
    2. Call `saveSettings(next)`. On a throw, restore the previous hotkeys with `apply(old capture, old board)` and return `{what, 0}`.
    3. Call `applyAutostart(next.startWithWindows)` only if it changed.
    4. Call `recheckReview()` if `dayStart` or `staleDays` changed.

- [ ] **Step 1: Write the failing tests** (store fixture, FakeBackend, counters for the two callbacks)

```cpp
void savesAndReregisters()        // next.boardHotkey "Ctrl+Alt+L", trashRetentionDays 40 → nullopt; settings() equal to next; combo(2) "Ctrl+Alt+L"
void takenSavesNothing()          // taken {"Ctrl+Alt+L"} → {"Ctrl+Alt+L is used by another program. Try another.", 2}; settings() unchanged; combo(2) old
void swapHotkeysSaves()           // next swaps capture and board → nullopt; both swapped in the manager and the store
void failedSaveRollsBackHotkeys() // next with a new board hotkey and staleDays 0 (invalid → InvalidSetting) → error with hotkeyId 0; combo(2) old
void autostartOnlyWhenChanged()   // same startWithWindows → callback not called; flipped → called once with the new value
void dayStartChangeRechecksReview() // dayStart 06:00 → 10:00 → recheck called once; trash days only → not called.
                                    // With a real ReviewScheduler at now = 08:00 and no review yet today: before, check() would emit
                                    // reviewDue; after apply(dayStart 10:00) + recheck → no reviewDue, msUntilNextCheck() == 7'200'000
```

- [ ] **Step 2: Run to verify they fail**

Run: the test command. Expected: compile errors.

- [ ] **Step 3: Implement**

As in Interfaces. No UI.

- [ ] **Step 4: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`.

- [ ] **Step 5: Commit**

```bash
git add app
git commit -m "feat(setup): settings applier: hotkeys all-or-nothing, save, autostart, review recheck"
```

---

### Task 5: SetupWizard

**Files:**
- Create: `app/src/setup/SetupWizard.h/.cpp`, `app/tests/tst_setup_wizard.cpp`
- Modify: `app/CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 1–3 (`HotkeyManager::set`, `HotkeyField`, `BackupFolderField`, `syncWarningText`).
- Produces:
  - `struct WizardContext { QString dataDir; std::optional<noite::SyncProvider> sync; bool autostartManaged = true; std::function<std::optional<QString>()> askFolder; };`
  - `class SetupWizard : public QWizard`, constructed as `SetupWizard(noite::Settings initial, HotkeyManager&, WizardContext, QWidget* parent = nullptr)`
  - `noite::Settings collected() const`: the values on all pages, with `setupCompleted` untouched
  - `void captureHotkeyPressed()`: acts only on the last page, where it emits `finished(collected(), true)` and closes
  - `void skip()`: what Skip setup does, public for tests; only enabled off page 2
  - signals `finished(noite::Settings settings, bool viaHotkey)` and `abandoned(noite::Settings partial)` (✕ or Esc)
  - Behaviour:
    - Leaving page 2 calls `set(kCaptureId, combo)`; leaving page 3 calls `set(kBoardId, combo)`. A failure keeps the user on the page with the field's message.
    - Page 2's Next is enabled only while the field `isAccepted()`.
    - Skip setup runs Finish with the shown values (`viaHotkey` false). It's offered on page 1 too: the capture hotkey there is the current setting, registered with `set` when skipping; if that fails, the wizard jumps to page 2.

- [ ] **Step 1: Write the failing tests** (FakeBackend; shown offscreen; `QWizard::next()` / `button(QWizard::FinishButton)->click()`)

```cpp
void prefilledFromSettings()      // initial trash 40, dayStart 07:30, staleDays 9, backupDir "G:/x", startWithWindows false →
                                  // each page shows it; collected() == initial
void page2NeedsAcceptedHotkey()   // on page 2 with taken {"Ctrl+Alt+J"} and initial capture "Ctrl+Alt+J": field not accepted → Next disabled;
                                  // record Ctrl+Alt+K → Next enabled; next() → manager combo(1) "Ctrl+Alt+K"
void skipOfferedExceptPage2()     // Skip setup button visible on pages 1, 3–8, hidden on page 2
void skipFinishesWithShownValues()// page 4, set trash 50, skip() → finished(settings with trash 50, false)
void captureHotkeyIgnoredBeforeLastPage() // on page 3: captureHotkeyPressed() → no signal, still on page 3, still visible
void captureHotkeyOnLastPageFinishes()    // go to page 8: label "Press Ctrl+Alt+J now."; captureHotkeyPressed() → finished(…, true), hidden
void finishButtonFinishes()       // page 8, Finish → finished(…, false)
void escAbandons()                // page 4, set trash 50, Esc (reject) → abandoned(settings with trash 50); no finished
void syncWarningOnlyWithProvider()// sync nullopt → no warning label on page 1; GoogleDrive → label text == syncWarningText(GoogleDrive)
void autostartDisabledForDataRun()// autostartManaged false → page 7 checkbox disabled
```

- [ ] **Step 2: Run to verify they fail**

Run: the test command. Expected: compile errors.

- [ ] **Step 3: Implement**

- **Pages:** small `QWizardPage` subclasses in `SetupWizard.cpp`, or plain pages built in the constructor.
- **Skip setup:** a custom button (`QWizard::CustomButton1`), its visibility updated on `currentIdChanged`.
- **Esc and ✕:** route through `reject()`, which emits `abandoned`.
- **Look:** `QWizard::ModernStyle`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`.

- [ ] **Step 5: Commit**

```bash
git add app
git commit -m "feat(setup): first-run wizard: 8 pages, hotkeys registered on leaving pages 2-3, skip, try-it finish"
```

---

### Task 6: SettingsDialog

**Files:**
- Create: `app/src/setup/SettingsDialog.h/.cpp`, `app/tests/tst_settings_dialog.cpp`
- Modify: `app/CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 1–4.
- Produces:
  - `struct SettingsContext { std::optional<noite::SyncProvider> sync; bool autostartManaged = true; QString logsDir; std::function<std::optional<QString>()> askFolder; std::function<QDate()> today; };`
  - `class SettingsDialog : public QDialog`, constructed as `SettingsDialog(noite::Store&, HotkeyManager&, SettingsApplier&, SettingsContext, QWidget* parent = nullptr)`
  - `noite::Settings edited() const`, `QString errorText() const`
  - `void save()`, which Save calls: `applier.apply(edited())`. On an error with `hotkeyId`, that field shows the message; otherwise `errorText` shows it. The dialog stays open. On success it `accept()`s.
  - signals `runSetupRequested()`, `backUpNowRequested()`
  - Buttons: Open logs folder → `QDesktopServices::openUrl(QUrl::fromLocalFile(logsDir))`; Cancel → `reject()` (the manager is resumed if paused).

- [ ] **Step 1: Write the failing tests**

```cpp
void showsCurrentSettings()       // store settings (board "Ctrl+Alt+L", trash 40, dayStart 07:30, staleDays 9, Weekly, keep 5) → fields show them;
                                  // windowTitle "noite settings"
void saveAppliesAndCloses()       // change trash to 50, save() → result Accepted; store trash 50
void failedSaveStaysOpen()        // board field setCombo("Ctrl+Alt+K") (accepted, no probe); then mark "Ctrl+Alt+K" taken in the fake backend;
                                  // save() → dialog still visible, board field statusText "Ctrl+Alt+K is used by another program. Try another.",
                                  // store unchanged
void cancelChangesNothing()       // change trash, Cancel → Rejected; store unchanged; manager not paused
void lastBackupLine()             // jobState lastBackupDay = today → label "Last backup: today"
void runSetupAgainEmits()         // click Run setup again → runSetupRequested once; dialog closed
void backUpNowEmits()             // click Back up now → backUpNowRequested once; dialog stays open
```

- [ ] **Step 2: Run to verify they fail**

Run: the test command. Expected: compile errors.

- [ ] **Step 3: Implement**

A `QFormLayout` inside each `QGroupBox`. The sync warning is a label at the top, shown only with a provider. Start with Windows is disabled when `!autostartManaged`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`.

- [ ] **Step 5: Commit**

```bash
git add app
git commit -m "feat(setup): settings window: grouped page, save via the applier, last backup, logs folder, run setup again"
```

---

### Task 7: App wiring, tray, spec

**Files:**
- Modify:
  - `app/src/App.h/.cpp`, `app/tests/tst_app.cpp`
  - `docs/spec.md` (D24, D25, D29)
  - `docs/testing/manual-capture-checks.md` (a new "Setup" section, S1–S4, verbatim from the design)
  - `docs/superpowers/plans/2026-10-15-noite-roadmap.md` (the M6 row's plan link)

**Interfaces:**
- Consumes: Tasks 1–6.
- Produces:
  - `void App::showWizard()`, `SetupWizard* App::wizard() const`
  - `void App::showSettings()`, `SettingsDialog* App::settingsDialog() const`
  - `QAction* App::settingsAction() const`
  - `bool App::setupPending() const`: true while the review check and the job timers are deferred
  - `QMenu* App::trayMenu() const`: the tray menu is built even when no tray exists (offscreen), and is attached to the tray when there is one
  - Behaviour:
    - **Startup:** while `setupCompleted` is false, `start()` opens the wizard instead of registering hotkeys, and defers the review `check()` and `jobs_->startTimers()` until the wizard finishes or is abandoned (once per process).
    - **Hotkeys while the wizard is open:** a capture press calls `wizard->captureHotkeyPressed()`; a board press is ignored.
    - **`finished`:** apply with `setupCompleted = true`. With `viaHotkey`, create the starter note and `window_->summonNote(id)` (a failure is logged and falls back to `summon()`).
    - **`abandoned`:** apply the partial settings. If the capture hotkey isn't registered, `set` the current setting (emit `hotkeyUnavailable` on failure).
    - **Window rules:** at most one wizard and one dialog. Settings raises the wizard while it's open. `runSetupRequested` closes the dialog, then opens the wizard. `backUpNowRequested` → `backUpNow()`.
    - **Autostart:** the tray action and the applier share `updateAutostart`, and the tray action's checked state follows a saved change.

- [ ] **Step 1: Write the failing tests** (`tst_app.cpp`; App constructed with a fake hotkey backend: add an optional `HotkeyBackend*` override to `App`'s constructor, tests only, defaulting to `host_`)

```cpp
void wizardOnFirstRun()           // data folder with an overdue note, dayStart 00:00, setupCompleted false → after start() and a processEvents:
                                  // wizard() visible, setupPending() true, reviewWindow() null
void wizardFinishViaHotkey()      // same folder; wizard to the last page, emit host hotkeyPressed(1) → wizard gone; settings().setupCompleted;
                                  // setupPending() false; captureWindow() visible with plain text starting "Welcome to noite";
                                  // after processEvents the review check has run (reviewWindow() non-null)
void wizardAbandonReturns()       // reject the wizard → setupCompleted false; destroy App; new App on the same folder → wizard() visible again
void completedSkipsWizard()       // setupCompleted true in settings → no wizard; hotkeys registered (fake backend has ids 1 and 2)
void settingsInTrayOrder()        // non-separator action texts of trayMenu() == {"New note", "Board", "Morning review", "Start with Windows",
                                  // "Settings", "Back up now", "Quit"}
void settingsOpensOnce()          // showSettings() twice → one dialog; showSettings() while the wizard is open → raises the wizard, no dialog
void runSetupAgainOpensWizard()   // dialog → runSetupRequested → dialog closed, wizard() visible
```

- [ ] **Step 2: Run to verify they fail**

Run: the test command. Expected: compile errors.

- [ ] **Step 3: Implement**

As in Interfaces. Spec edits are as listed in the design's "Spec edits", including D24's new note that the wizard registers the capture hotkey on leaving page 2.

- [ ] **Step 4: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`.

- [ ] **Step 5: Commit**

```bash
git add app docs
git commit -m "feat(app): first-run wizard and Settings window in the tray; review and jobs wait for setup; spec D24/D25/D29"
```
