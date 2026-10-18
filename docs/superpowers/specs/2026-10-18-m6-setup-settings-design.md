# M6: setup wizard and Settings — design

**Date:** 2026-10-18 · **Status:** approved in conversation · **Spec:** D24, D25, D29 (logs folder), D33 (stale days)

## Goal

A first-run wizard walks through every setting in D24 order and ends with the real capture window. A Settings window lets every one of them change later, live, without a restart. The tray gains Settings. The sync-folder warning that noite-core already detects is finally shown.

noite-core needs no changes: `Settings` already holds every field, including `setupCompleted` and `staleDays`.

## Decisions

| # | Decision | Why |
|---|---|---|
| S1 | The wizard shows on every data folder whose `setupCompleted` is false, including existing ones with notes (prefilled with their current settings) | The user's choice: everyone sees the new options once. |
| S2 | "Try it" (page 8): pressing the capture hotkey closes the wizard and opens the real capture window, prefilled with a short how-to note (a real note) | The user's choice. |
| S3 | Settings is one grouped page, applied with Save; Cancel discards | The user's choice. About 11 settings fit on one page. |
| S4 | A `QWizard` plus a `QDialog`, sharing `HotkeyField` and `BackupFolderField` (approach A) | The user's choice. Native wizard paging for free; the conflict check and the Drive suggestion are written once. |
| S5 | Hotkey changes apply live. A combination that can't be registered is never saved, and the old one stays in force | No restart; noite is never left without a hotkey. |
| S6 | The review and the job timers wait while the wizard is open | Nothing pops over the wizard. |
| S7 | The "last backup" line shows a day, not a time | JobState stores the day only; a time isn't worth a core change. |

## Behaviour

### When the wizard appears

- **First run, or `setupCompleted` false:** the wizard opens after the store is open, before the hotkeys are registered. Pages are prefilled from `store.settings()`.
- **While it is open:** the review check and `jobs.startTimers()` are deferred. They run once it closes.
- **Closed with ✕ before finishing ("abandoned"):** what was collected is applied, `setupCompleted` stays false, and the wizard returns at the next start. If no capture hotkey was accepted, the current setting is registered as before; if that fails, the existing "already used by another program" toast shows.

### Pages

Back / Next / Finish, plus **Skip setup** on every page except page 2. Skip setup finishes with the values shown (defaults on a new folder) and the accepted capture hotkey, without the starter note.

| # | Page | Content |
|---|---|---|
| 1 | Welcome | "Your notes live in this folder: `<data dir>`. Nothing leaves your PC." When `DataDir::syncWarning()` names a provider, a warning (text below). |
| 2 | Capture hotkey | `HotkeyField`. Next is enabled only while the field shows ✓. |
| 3 | Board hotkey | `HotkeyField`, with the same rules. |
| 4 | Trash | "Keep trash for [30] days" (1–365). |
| 5 | Mornings | "Day starts at [06:00]" and "Bring back untouched notes after [7] days" (1–90). |
| 6 | Backup | `BackupFolderField`. The folder may stay unset; "Back up now" asks later, as in M5. |
| 7 | Start with Windows | "[x] Start noite with Windows". Disabled for a `--data` run (as the tray item). |
| 8 | Try it | "Press `<capture hotkey>` now." Pressing it finishes the wizard (`viaHotkey = true`). The Finish button finishes it without the starter note (`viaHotkey = false`). |

Finishing applies the settings with `setupCompleted = true`. With `viaHotkey`, the app creates the starter note and opens it in the capture window.

### Hotkey field

- **Recording:** click the field, press a combination. While it records, noite's own hotkeys are paused (unregistered) so the keys reach the field; they are resumed when recording ends.
- **Valid:** Ctrl and/or Alt (Shift optional) plus one key from the parser's set (A–Z, 0–9, F1–F24, Space). Win is never produced. Anything else: "Use Ctrl or Alt with a key."
- **Check:** the combination is probed (registered on a spare id and unregistered at once).
  - Free: "✓ Ctrl+Alt+J".
  - Taken: "Ctrl+Alt+N is used by another program. Try another."
  - Same as the other field: "That's already the board hotkey." / "That's already the new-note hotkey."
- **Nothing is registered by the field itself.** Registration happens when the wizard finishes or Settings saves.

### Settings window

```
┌─ noite settings ─────────────────────────────┐
│ (sync-folder warning, when it applies)        │
│ Hotkeys                                       │
│   New note    [ Ctrl+Alt+J ]  ✓              │
│   Board       [ Ctrl+Alt+B ]  ✓              │
│ Notes                                         │
│   Keep trash for      [ 30 ] days            │
│   Day starts at       [ 06:00 ]              │
│   Resurface untouched [  7 ] days            │
│ Backup                                        │
│   Folder  G:\Meu Drive        [Change…]      │
│   Every [day ▾]  Keep [ 14 ]  [Back up now]  │
│   Last backup: today                         │
│ System                                        │
│   [x] Start with Windows                     │
│   [Open logs folder]  [Run setup again]      │
│                         [ Cancel ] [ Save ]  │
└───────────────────────────────────────────────┘
```

- **Opening:** from the tray's Settings item. A second open raises the existing window. While the wizard is open, Settings raises the wizard instead.
- **Save:** `SettingsApplier::apply(next)`. On failure the dialog stays open with the reason under the hotkey field, or at the bottom for other errors. On success it closes.
- **Cancel:** discards everything; the manager is resumed, so the old hotkeys are in force.
- **Last backup:** from `jobState()` — "today", "yesterday", "Oct 15" (`MMM d`, English locale), "never"; when `backupFailingSinceMs` is set, "failing since Oct 15".
- **Buttons:**
  - **Back up now** works as the tray item does (folder dialog when unset; the toast reports the result).
  - **Open logs folder** opens `data\logs` in Explorer.
  - **Run setup again** closes the dialog and opens the wizard.

### Tray

New note · Board · Morning review · Start with Windows · **Settings** · Back up now · Quit.

### Texts

**Starter note** (`starterNoteHtml`, with the user's actual hotkeys and stale days):

> **Welcome to noite**
> - Ctrl+Alt+J opens this window from anywhere. Type, then Esc: it's saved.
> - Ctrl+Alt+B opens the board with all your notes. Ctrl+Enter marks a note done.
> - Type @tomorrow or @fri to give a note a date.
> - Each morning noite shows what's overdue, due today, or untouched for 7 days: Done, Snooze, Keep or Trash.
> - Delete this note once you've read it (Del on the board).

**Sync warning** (`syncWarningText`): "This folder is inside Google Drive / OneDrive / Dropbox. Syncing a live database can corrupt it. Move the noite folder somewhere that isn't synced; backups can still go to Drive."

## Components (app)

New folder `app/src/setup/`, plus `platform/HotkeyManager` and small extensions.

| Unit | Does | Depends on |
|---|---|---|
| `platform/HotkeyManager` | Owns the two global hotkeys (replaces `App::registerHotkey`). `apply(id, combo)` → `Ok / Taken / Invalid / SameAsOther`; on failure the old combination stays registered. `probe(combo)` → `Ok / Taken / Invalid`, leaving nothing registered. `pause()` / `resume()`. `combo(id)`. | `HotkeyBackend` interface (`registerHotkey`, `unregisterHotkey`); `NativeHost` implements it, tests use a fake |
| `platform/Hotkey` (extended) | `std::optional<QString> comboFromKey(int qtKey, Qt::KeyboardModifiers)`: text that `parseHotkey` accepts, per the field rules | — |
| `setup/HotkeyField` | Records a combination, shows ✓ or the message, pauses and resumes the manager, probes. `setOther(combo, name)` for the duplicate check. `combo()`, `isAccepted()`, signal `changed()`. | `HotkeyManager` |
| `setup/BackupFolderField` | Folder + "Change…" + every day/week + keep N; "Not set (Back up now will ask)" when unset. `setValues` / `values`. | injectable `std::function<std::optional<QString>()>` (default `askBackupFolder`) |
| `setup/SettingsApplier` | `std::optional<QString> apply(const Settings& next)`: hotkeys first (rolled back if anything later fails), then `saveSettings`, then autostart, then `reviewScheduler.check()`. Returns the error text, or nothing on success. | `Store`, `HotkeyManager`, `AutostartRegistry` (optional), `ReviewScheduler` |
| `setup/SetupWizard` | `QWizard` with the 8 pages. Signals `finished(Settings, bool viaHotkey)` and `abandoned(Settings)`. Slot `captureHotkeyPressed()` (acts only on page 8). | the fields |
| `setup/SettingsDialog` | The grouped page. Signals `runSetupRequested()`, `backUpNowRequested()`. Save calls the applier. | the fields, `SettingsApplier`, `Store` (job state) |
| `setup/SetupTexts` | `starterNoteHtml(const Settings&)`, `lastBackupText(const JobState&, QDate today)`, `syncWarningText(SyncProvider)` | — |
| `capture/CaptureWindow` (extended) | `summonNote(const ScratchId&)` opens an existing note like `summon()` opens a new one | `CaptureController::open` |

### App flow

```text
App::start
  store open → log/crash → HotkeyManager(host_)
  settings.setupCompleted
    yes → hotkeys.apply(capture), apply(board) → review check + jobs.startTimers()
    no  → showWizard()   (review check and jobs.startTimers() deferred)
showWizard
  SetupWizard(store.settings(), hotkeys, dataDir.syncWarning(), autostart managed?)
  host_ hotkeyPressed(capture) while the wizard is open → wizard.captureHotkeyPressed()
  finished(s, viaHotkey) → applier.apply(s with setupCompleted = true)
                           viaHotkey → id = store.createScratch(starterNoteHtml(s)); window_->summonNote(id)
  abandoned(partial)    → applier.apply(partial); missing capture hotkey → register the current one
  then → the deferred review check and jobs.startTimers() (once per process)
tray Settings → showSettings();  SettingsDialog runSetupRequested → close → showWizard()
```

## Errors

| Case | Behaviour |
|---|---|
| Capture hotkey can't be registered on page 2 | Next stays disabled with the field's message; no Skip setup on page 2. |
| ✕ with no capture hotkey accepted | Abandoned; the current setting is registered as before (toast if taken). |
| Save fails | The dialog stays open with the reason; hotkeys rolled back; nothing saved. |
| Cancel after probing | Nothing was registered; `resume()` restores the old hotkeys. |
| Settings or the wizard already open | Raise the existing window; never both at once. |
| `--data` run | Start with Windows disabled on page 7 and in Settings. |
| Starter note can't be created | Logged; the capture window opens blank; the wizard still counts as finished. |
| Review due while the wizard is open | Deferred `check()` runs right after it closes. |

## Testing (Qt Test, test-first, offscreen; hotkeys through a fake `HotkeyBackend`)

- **`tst_hotkey` (extended):** `comboFromKey` — Ctrl+Alt+J → "Ctrl+Alt+J"; Shift+J and plain J → nothing; Ctrl+F5 → "Ctrl+F5"; Win never produced; every result round-trips through `parseHotkey`.
- **`tst_hotkey_manager`:** `apply` returns Ok / Taken / Invalid / SameAsOther; a failed apply keeps the old combination; `pause()` unregisters both and `resume()` re-registers; `probe` leaves nothing registered.
- **`tst_hotkey_field`:** key presses give ✓ or the exact messages; recording pauses and resumes the manager; the duplicate message.
- **`tst_backup_folder_field`:** values round-trip; "Change…" uses the injected asker; unset shows "Not set (Back up now will ask)".
- **`tst_settings_applier`:** hotkey change → re-registered then saved; taken → nothing saved and old hotkeys kept; a failing save rolls the hotkeys back; a day-start change makes the review recheck; Start with Windows applied.
- **`tst_setup_wizard`:** prefilled from settings; page 2 Next disabled until accepted; Skip setup → shown values plus the capture hotkey; capture hotkey on page 8 → `finished(…, true)`; Finish → `finished(…, false)`; ✕ → `abandoned`; the sync warning only with a provider.
- **`tst_settings_dialog`:** shows the current settings; Save calls the applier and closes; a failed Save stays open with the message; Cancel changes nothing; the last-backup line; Run setup again emits its signal.
- **`tst_setup_texts`:** the starter note holds the actual hotkeys and stale days; `lastBackupText` for today, yesterday, a date, never and failing.
- **`tst_app`:** `setupCompleted` false → the wizard is open and the review and jobs haven't run; finishing via the hotkey → `setupCompleted` true, the starter note stored and shown in the capture window; abandoning → the wizard again on the next start; the Settings item in tray order; Settings opened twice → one window.

Manual checks (`docs/testing/manual-capture-checks.md`, new "Setup" section):

| # | Check | Expected |
|---|---|---|
| S1 | Start the new build on the existing data folder | The wizard opens, prefilled with the current settings |
| S2 | On page 2 press Ctrl+Alt+N (taken on this PC), then Ctrl+Alt+J | The "used by another program" message, then ✓ |
| S3 | On page 8 press the capture hotkey | The wizard closes; the capture window shows the starter note with your hotkeys |
| S4 | Settings: change the board hotkey, Save, press it; change it again, Cancel | The new hotkey works at once without a restart; after Cancel the saved one still works |

## Spec edits (spec.md)

- **D24:** existing folders see the wizard once (S1); page 5 also asks the stale days; "Try it" opens the capture window with the starter note (S2); ✕ behaviour.
- **D25:** the Settings layout, Save/Cancel, live hotkey changes (S5); Settings in the tray order.
- **D29:** "Open logs folder" is in Settings.
- **Roadmap:** link the M6 plan.
