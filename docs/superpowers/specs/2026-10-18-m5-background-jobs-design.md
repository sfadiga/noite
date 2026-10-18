# M5: background jobs (purge, backup, logs) — design

**Date:** 2026-10-18 · **Status:** approved in conversation · **Spec:** D13, D22, D29, D25 (tray)

## Goal

The jobs that keep data safe and tidy run without the user doing anything:
- trash past its recovery window and unreferenced attachments are purged;
- the database is backed up to a folder the user picked, usually their Google Drive for Desktop folder;
- everything the app logs ends up in rotating files, and a crash leaves a stack trace and a dump.

None of it may freeze the capture window or the board: the hotkey budget (D5) holds while a backup runs.

Most of the engine exists since M1 (`Store::purgeTrash`, `backup::snapshot`/`prune`/`verify`, `DataDir::logsDir`); M5 schedules it, moves the backup off the UI thread, and adds the log and crash handler.

## Decisions

| # | Decision | Why |
|---|---|---|
| B1 | Google Drive is used through Drive for Desktop's mounted folder (`G:\Meu Drive` etc.), like any local folder | No sign-in and no network code (D13). Drive uploads what noite writes. |
| B2 | Until M6's wizard exists, the first **Back up now** with no folder set offers the detected Drive folder in one click, else a folder picker; the choice is saved to `Settings.backupDir` | The user's choice. Scheduled backups stay off until a folder is chosen. |
| B3 | **Back up now** always shows a tray toast with the result; scheduled backups are silent unless they have been failing for 3 days, then a toast at most once a day | The user's choice: proof on demand, no daily popup, but a broken backup doesn't go unnoticed. |
| B4 | The snapshot runs on a worker thread with its own read-only SQLite connection (WAL); the purge stays on the main thread through the `Store` | The user's choice (approach A). Only option that keeps the hotkey budget without guessing idle times; the purge is a write and keeps the single writer (D14). |
| B5 | Each backup is snapshot → verify → prune; a snapshot that fails verify is deleted and the run counts as failed | A backup that can't be restored is not a backup. |
| B6 | Daily = once per local calendar day; weekly = 7 days after the last successful backup; missed runs catch up at the next check | Predictable, and a PC that was off still gets backed up. |
| B7 | A crash writes a `module+offset` stack trace to the log and a minidump (newest 3 kept); PDBs are built in Release but not shipped | The user's choice. Readable later in Visual Studio without growing the zip. |
| B8 | The log handler is installed only after the data folder lock is held | A second copy that only focuses the running one never writes into its logs. |

## Behaviour

### When jobs run

One `JobScheduler` calls `check()`:
- 30 s after start (keeps the first summon fast);
- on resume (`NativeHost::resumed`);
- from an hourly timer.

Each check runs what is due:

| Job | Due when | On failure |
|---|---|---|
| Purge | `lastPurgeDay != today` | Logged; `lastPurgeDay` stays unset, so the next check retries. |
| Backup, Daily | `backupDir` set and `lastBackupDay != today` | Logged; retried at the next check (hourly while e.g. Drive is down). |
| Backup, Weekly | `backupDir` set and `today - lastBackupDay >= 7` (or never backed up) | Same. |

With no `backupDir`, scheduled backups are skipped, logged once per day, and nothing pops up.

### Back up now

The tray menu, in the spec's order: New scratch · Open board · Morning review · Start with Windows · Settings · **Back up now** · Quit (Settings arrives in M6).

- **No folder set:**
  - **Drive detected:** a dialog

    ```
    Back up noite

    Google Drive found:
      G:\Meu Drive\noite-backups

    [ Back up to Drive ]  [ Choose another folder... ]  [ Cancel ]
    ```

    The path shown is the Drive folder plus `noite-backups`, which `backup::run` creates (with `<installId>\snapshots` under it).
  - **No Drive:** the standard folder picker.
  - **After a choice:** it is saved to `Settings.backupDir` and the backup runs.
  - **Cancel:** nothing changes.
- **Folder set:** backs up at once, regardless of the schedule.
- **While a backup runs:** the item reads "Backing up…" and is disabled.
- **Result:** always a toast: "Backed up · 14.2 MB" or "Backup failed: backup folder not found: G:\Meu Drive". A success counts as the day's backup.

### Failure alert

- When a scheduled or manual backup fails, `backupFailingSince` is set (if not already set).
- Once `now - backupFailingSince >= 3 days` and `backupAlertDay != today`, a toast says "Backups have been failing since Oct 15: <last error>", and `backupAlertDay = today`.
- A successful backup clears both.

### Quitting during a backup

`~App` cancels the worker and waits for it before the store closes. An SQLite progress handler stops `VACUUM INTO` within milliseconds. `backup::run` removes its half-written `.tmp` when it fails or is cancelled. One left behind by a killed process is removed by `prune` an hour later. A finished `.db` is never half-written (it only appears under its final name when complete).

## Core (noite-core)

```cpp
namespace backup {
struct Job    { QString dbPath, installId, dest; int keep; qint64 nowMs; };
struct Result { QString path; qint64 bytes; int pruned; };
// Safe on any thread: opens its own read-only connection to dbPath; never touches a Store.
// Snapshot -> verify -> prune. A snapshot that fails verify is deleted.
// Throws BackupDirMissing, BackupInvalid, Io, or Cancelled once *cancel becomes true.
Result run(const Job& job, const std::atomic<bool>* cancel = nullptr);
}

// Job bookkeeping; one JSON value under its own settings-table key (`job_state`),
// apart from Settings like last_review_day, so saving one never overwrites the other.
struct JobState {
    std::optional<QDate> lastPurgeDay, lastBackupDay, backupAlertDay;
    std::optional<qint64> backupFailingSinceMs;
};
JobState Store::jobState() const;
void Store::saveJobState(const JobState& state);

// The Google Drive folder names syncProviderFor already knows, as one list (My Drive,
// Meu Drive, ...), so the app's Drive detection shares them.
QStringList googleDriveFolderNames();
```

- `ErrorCode::Cancelled` is new.
- The existing `snapshot(Store&, dest)` and `prune(const Store&, dest, keep)` keep their signatures and share internals with `run`.
- **Consistency:** the read connection's `VACUUM INTO` reads one WAL snapshot, so a scratch written mid-backup is either wholly in it or not at all.

## App

New folder `app/src/jobs/`, plus `platform/DriveFolder` and `platform/CrashHandler`, and `app/src/log/`.

| Unit | Does | Depends on |
|---|---|---|
| `JobScheduler` (main thread) | The rules above: `check()`, `backUpNow()`, runs the purge, updates `JobState`; signals `toast(title, text)` and `busyChanged(bool)` | `Store&`, injected `now()`, `BackupRunner&` |
| `BackupRunner` (interface) | `start(Job)`, `busy()`, `cancelAndWait()`; signals `finished(Result)`, `failed(ErrorCode, QString)`, delivered on the main thread | — |
| `ThreadBackupRunner` | Runs `backup::run` on its own `QThread` with a cancel flag | noite-core |
| `BackupFolderDialog` | The Drive dialog above; returns a path or nothing; opens the folder picker for "Choose another folder…" | `DriveFolder` |
| `DriveFolder` | `suggestedDriveFolder(volumes, dirExists)`: pure over (root, label) pairs. The volume labelled "Google Drive" (any case), then the first existing child from `googleDriveFolderNames()`, else the volume root. `mountedDriveVolumes()` reads `QStorageInfo::mountedVolumes()`. | core |

```text
App::start
  JobScheduler(store, now, ThreadBackupRunner)
  singleShot(30 s) -> check;  hourly timer -> check;  host_ resumed -> check
check()
  purge due?   -> store.purgeTrash(retention); lastPurgeDay = today
  backup due?  -> runner.start(Job{dbPath, installId, backupDir, keep, now})
                   finished -> lastBackupDay = today; clear failingSince, alertDay
                   failed   -> failingSince ??= now; >= 3 days && alertDay != today -> toast
tray "Back up now" -> no folder? BackupFolderDialog -> save backupDir -> scheduler.backUpNow()
                      -> toast with the result
~App -> runner.cancelAndWait() before the store closes
```

App also gains the tray item, the toasts (`QSystemTrayIcon::showMessage`), and resume → `check()`.

## Logs

`app/src/log/Log.{h,cpp}`:
- **Handler:** `qInstallMessageHandler`, installed in `App::start` once the data folder lock is held (B8). Messages from before that are buffered (up to 256 lines) and written first.
- **File:** `data\logs\noite.log`. Before a write would take it past 1 MB, files shift `noite.log → noite.1.log → … → noite.6.log` and the oldest is dropped: 7 files at most (D29).
- **Line:** `2026-10-18 06:00:01.123 W [backup] backup: failed: backup folder not found: G:\Meu Drive`. Local time with ms, level letter (D/I/W/C/F), thread name (`main`, `backup`, or the thread id).
- **Thread safety and durability:** one mutex; a flush after every line.
- **Debug messages:** written only in Debug builds, which also copy every line to the debugger output.
- **New lines:**
  - `noite started · data D:\… · Qt 6.10.3 · Windows 10.0.26300`
  - `noite quit`
  - `backup: ok <path> · 14.2 MB · 1.3 s · pruned 1`
  - `backup: skipped, no folder` (once a day)
  - `purge: 3 notes, 2 attachments`

## Crashes

`app/src/platform/CrashHandler.{h,cpp}`, installed right after the log:
- **Hooks:**
  - `SetUnhandledExceptionFilter`
  - `std::set_terminate`
  - `_set_purecall_handler`, `_set_invalid_parameter_handler`
  - `SIGABRT`
  - `qFatal` (via the message handler)
- **Writes:**
  - a `CRASH` line with the exception code and the faulting `module+offset`;
  - the stack walked with `StackWalk64` from the crash context, one `module+offset` per frame;
  - `MiniDumpWriteDump` (normal + thread info) to `data\logs\crash-YYYYMMDD-HHMMSS.dmp`, named in the log.
- **Safe on the crash path:**
  - paths are prepared at install, and the crash path doesn't allocate;
  - it writes with `WriteFile` on its own handle;
  - it takes the log mutex only with `try_lock`.
- **Retention:** the newest 3 dumps are kept; pruning happens at startup, never on the crash path.
- **Symbols:** Release builds produce PDBs (`/Zi`, `/DEBUG`, `/OPT:REF`, `/OPT:ICF`, same optimisation). They are not shipped in the zip; M7's release workflow keeps them as CI artifacts.

**Not in M5:**
- "Open logs folder" belongs to M6's Settings (D29).
- No "noite crashed last time" prompt.

## Errors

| Case | Behaviour |
|---|---|
| Purge throws | Logged with the job name; retried at the next check. |
| Backup folder missing (Drive not running) | Logged; retried hourly; alert after 3 days (B3). |
| Snapshot fails verify | Snapshot deleted; logged; counts as a failure. |
| Back up now fails | Toast with the error, plus the log line. |
| `JobState` can't be saved | Logged; the job may run again at the next check (harmless for both jobs). |
| Log file can't be opened | Messages go to the debugger output only; the app runs normally. |

## Testing (Qt Test, test-first, offscreen)

Core:
- **`tst_backup_job`:**
  - `run` writes, verifies and prunes; `Result.bytes` is the file size;
  - runs while the main connection writes in a loop, and the snapshot is consistent;
  - a missing dest throws `BackupDirMissing` and leaves nothing;
  - cancel mid-run throws `Cancelled` and leaves no `.db`;
  - a corrupted snapshot fails verify, is deleted and throws `BackupInvalid`.
- **`tst_job_state`:**
  - every field round-trips;
  - a missing key gives an empty state;
  - `saveSettings` and `saveJobState` don't overwrite each other.

App:
- **`tst_job_scheduler`** (fake clock, fake runner):
  - purge once per calendar day;
  - daily backup once a day, catching up after days off;
  - weekly backup due on day 7, not day 6;
  - no folder → no backup and no toast;
  - a failure is retried at the next check;
  - the alert comes only after 3 days, at most once a day, and is cleared by a success;
  - `backUpNow` while busy is refused;
  - a manual success counts as the day's backup;
  - toast texts.
- **`tst_backup_runner`** (real thread, temp dirs):
  - `finished` arrives on the main thread;
  - `cancelAndWait` mid-run returns promptly with no `.db`.
- **`tst_drive_folder`:**
  - the "Google Drive" volume with `Meu Drive` → `G:/Meu Drive`;
  - with `My Drive`;
  - with neither → the volume root;
  - no Drive volume → nothing.
- **`tst_backup_folder_dialog`:**
  - Drive found → three buttons and the path;
  - "Back up to Drive" returns the path;
  - Cancel returns nothing.
- **`tst_log`:**
  - line format;
  - rotation at 1 MB keeps 7 files;
  - buffered lines come first and in order;
  - two threads never tear a line;
  - Debug is filtered in Release.
- **`tst_crash`:**
  - runs `noite_crash_helper.exe` as a child process: an access violation, an uncaught exception, `qFatal`;
  - each run logs `CRASH`, the code and at least 3 `module+offset` frames, and writes one `.dmp`;
  - startup keeps the newest 3 dumps.

  It only runs processes it starts itself.
- **`tst_app`:**
  - "Back up now" exists and is disabled with "Backing up…" while busy;
  - `~App` mid-backup quits cleanly;
  - the log file exists after `start()`.

Manual checks (`docs/testing/manual-capture-checks.md`, new "Jobs" section):

| # | Check | Expected |
|---|---|---|
| J1 | No folder set; tray → Back up now | The Drive dialog shows `G:\Meu Drive\noite-backups`; "Back up to Drive" → toast; the file appears in Drive on the web |
| J2 | Quit Google Drive; Back up now | Error toast; the log shows hourly retries afterwards |
| J3 | `trashRetentionDays` = 1, a note trashed over a day ago; restart | The note is gone after the first check; `purge:` line in the log |
| J4 | Open `data\logs\noite.log` | Startup, backup and purge lines |
| J5 | Run `noite_crash_helper.exe`, open its dump in Visual Studio with the matching PDB | The faulting function and line are shown |

## Spec edits (spec.md)

- **D13:** the Drive one-click flow (B1, B2), verify after each snapshot (B5), the schedule (B6), the 3-day alert (B3).
- **D22:** the purge runs once a day, at the first check.
- **D29:** line format, crash trace and dump (B7), PDB policy.
- **D25/tray:** Back up now in the tray order.
- **Roadmap:** link the M5 plan.
