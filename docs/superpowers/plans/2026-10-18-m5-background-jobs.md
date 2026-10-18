# M5: Background Jobs (Purge, Backup, Logs) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Purge and backups run by themselves without freezing the UI. "Back up now" offers the Google Drive folder in one click. Every log line lands in rotating files, and a crash leaves a trace and a minidump.

**Architecture:**
- **noite-core** gains `backup::run` (snapshot → verify → prune on its own read-only connection, cancellable), the `JobState` bookkeeping, and a shared list of Drive folder names.
- **The app** gains:
  - `JobScheduler`: the due rules, over an injected clock and a `BackupRunner` interface;
  - `ThreadBackupRunner`: runs `backup::run` on a `QThread`;
  - the Drive detection and folder dialog;
  - a chaining Qt message handler writing rotating files;
  - a Win32 crash handler.
- **`App`** wires it all together and adds the tray item.

**Tech Stack:** C++20, Qt 6.10 Widgets/Test, SQLite (WAL, progress handler), Win32 (`SetUnhandledExceptionFilter`, DbgHelp `StackWalk64`/`MiniDumpWriteDump`).

**Spec:** `docs/superpowers/specs/2026-10-18-m5-background-jobs-design.md` (B1–B8; project spec D13, D22, D29, D25)

## Global Constraints

- Checks: first 30 s after start (`kFirstCheckMs = 30'000`), then hourly (`kCheckIntervalMs = 3'600'000`), and on `NativeHost::resumed`.
- Purge due when `lastPurgeDay != today`. Daily backup due when `backupDir` is set and `lastBackupDay != today`. Weekly backup due when `backupDir` is set and `lastBackupDay` is empty, `lastBackupDay.daysTo(today) >= 7` or `< 0`.
- Alert after `kAlertAfterMs = 3 * kDayMs` of failing, at most once per local day; a success clears `backupFailingSinceMs` and `backupAlertDay`.
- Toast copy (title always `noite`):
  - `Backed up · %1 MB` (bytes / 1'048'576, one decimal)
  - `Backup failed: %1` (the error message)
  - `Backups have been failing since %1: %2` (date `MMM d` in `QLocale(QLocale::English)`, last error)
- Tray: `Back up now`, which reads `Backing up…` and is disabled while busy. Placed after `Start with Windows`, before `Quit`, each in its own separator group.
- Dialog: title `Back up noite`; label `Google Drive found:` and the path `<drive folder>\noite-backups` in native separators; buttons `Back up to Drive`, `Choose another folder…`, `Cancel`.
- Log:
  - `<logsDir>/noite.log`, rotating to `noite.1.log` … `noite.6.log` (7 files, 1 MB = 1'048'576 bytes each);
  - line `yyyy-MM-dd HH:mm:ss.zzz <L> [<thread>] <message>` with `L` ∈ D/I/W/C/F, thread `main`, `backup` or the numeric thread id;
  - early buffer up to 256 lines.
- Log lines: `noite started · data <dir> · Qt <ver> · Windows <ver>`, `noite quit`, `backup: ok <path> · %1 MB · %2 s · pruned %3`, `backup: failed: <msg>`, `backup: skipped, no folder` (once per day), `purge: %1 notes, %2 attachments`, `purge: failed: <msg>`.
- Crash: a `CRASH 0x<code> at <module>+0x<offset>` line, then one frame per line as `  <module>+0x<offset>`, then `dump: <file>`. Dump name `crash-yyyyMMdd-HHmmss.dmp` in `logsDir`, newest 3 kept (pruned at install).
- Release builds produce PDBs (`/Zi`, `/DEBUG`, `/OPT:REF`, `/OPT:ICF`); they are never copied into `dist`.
- Test command: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -Config Debug -Test` → `TOTAL failures: N`.
- Edit files only with the Edit/Write tools.

## Review Focus

1. **Clock set back** (or lastBackupDay later than today after a timezone change) → weekly backups must still run, not wait for the past date to come round. Test: Task 5 `weeklyWithFutureLastDayIsDue`.
2. **Backup folder with spaces and non-ASCII** (`G:\Meu Drive`, `Notas João`) → the snapshot is written and verified there. Test: Task 1 `runToNonAsciiFolder`.
3. **A rotated log file can't be replaced** (a `noite.1.log` that can't be removed, e.g. open elsewhere) → logging keeps going into `noite.log` and the app doesn't crash. Test: Task 2 `rotationFailureKeepsLogging`.
4. **Purge while the board shows the Trash tab** → the purged cards disappear from the open board instead of failing on click. Test: Task 7 `purgeReloadsOpenBoard`.
5. **Back up now with a folder set but missing** (Drive quit) → error toast; the folder dialog does *not* reopen and the setting is kept. Test: Task 7 `backUpNowWithMissingFolderToastsError`.

---

### Task 1: Core: backup::run, JobState, Drive folder names

**Files:**
- Create: `core/src/store_jobs.cpp`, `core/tests/tst_backup_job.cpp`, `core/tests/tst_job_state.cpp`
- Modify:
  - `core/include/noite/backup.h`, `core/src/backup.cpp` (`Job`, `Result`, `run`)
  - `core/include/noite/error.h` (`Cancelled`)
  - `core/include/noite/model.h` (`JobState`)
  - `core/include/noite/store.h`
  - `core/include/noite/data_dir.h`, `core/src/data_dir.cpp` (`googleDriveFolderNames`)
  - `core/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `ErrorCode::Cancelled`
  - `struct backup::Job { QString dbPath, installId, dest; int keep = 14; qint64 nowMs = 0; };`
  - `struct backup::Result { QString path; qint64 bytes = 0; int pruned = 0; };`
  - `backup::Result backup::run(const backup::Job&, const std::atomic<bool>* cancel = nullptr)`: thread-agnostic; opens `detail::Db::open(dbPath, true)`; never touches a `Store`
  - `struct JobState { std::optional<QDate> lastPurgeDay, lastBackupDay, backupAlertDay; std::optional<qint64> backupFailingSinceMs; bool operator==(const JobState&) const = default; };`
  - `JobState Store::jobState() const`, `void Store::saveJobState(const JobState&)`
  - `QStringList googleDriveFolderNames()`: the "my drive" family only (`My Drive`, `Meu Drive`, `Mi unidad`, `Mon Drive`, `Meine Ablage`, `Il mio Drive`, …), in the display case of each localisation, from the same table `syncProviderFor` uses

- [ ] **Step 1: Write the failing tests**

`tst_backup_job.cpp` (store fixture from `helpers.h`; `dest` a `QTemporaryDir`; `Job` built from `store.dataDir().dbPath()`, `store.installId()`, `store.nowMs()`):
```cpp
void runWritesVerifiesAndPrunes()  // 3 earlier snapshot() calls with the clock advancing; run(keep=2) → result.path exists
                                   // under <dest>/noite-backups/<id>/snapshots, bytes == QFileInfo(path).size(),
                                   // pruned == 2, 2 .db files remain, the newest is result.path
void runIsConsistentWhileWriting() // std::thread creating 200 scratches through the Store while run() executes on this
                                   // thread; open the snapshot: integrity_check "ok" and
                                   // COUNT(*) FROM scratches == COUNT(*) FROM scratch_fts
void runToNonAsciiFolder()         // dest = <tmp>/"Meu Drive"/"Notas João" (created) → result.path exists, verify() passes
void missingDestThrows()           // dest = <tmp>/absent → Error BackupDirMissing; <tmp> has no new entries
void cancelledRunLeavesNoDb()      // std::atomic<bool> cancel{true}; run(job, &cancel) → Error Cancelled;
                                   // snapshots dir has no *.db and no *.tmp
void invalidSnapshotIsDeleted()    // a scratch whose HTML has <img src="noite-att:<64 hex>.png"> never put → run → Error
                                   // BackupInvalid; snapshots dir has no *.db
```
`tst_job_state.cpp`:
```cpp
void emptyAtFirst()                 // jobState() == JobState{}
void roundTripsEveryField()         // all four set → saveJobState → jobState() equal
void settingsAndJobStateIndependent() // saveJobState(s); saveSettings(settings() with backupKeep 5) → jobState() == s;
                                      // setLastReviewDay(d) → jobState() == s; saveJobState(other) → settings().backupKeep 5
void driveFolderNamesHasMyDriveAndMeuDrive() // contains "My Drive" and "Meu Drive"; not "Shared drives"
```

- [ ] **Step 2: Run to verify they fail**

Run: the test command. Expected: compile errors (`backup::run`, `JobState`, `Cancelled`).

- [ ] **Step 3: Implement**

- **`run`:**
  - Throws `BackupDirMissing` like `snapshot`. `mkpath` the snapshots folder.
  - Open the read-only connection. Install `sqlite3_progress_handler(handle, 1000, …)` returning non-zero once `*cancel` is true, then `VACUUM INTO` the `.tmp` and rename it.
  - An interrupted step (`SQLITE_INTERRUPT`) becomes `Cancelled`.
  - On any failure, remove the `.tmp` (best effort).
  - Then `verify(final)`. On `BackupInvalid`, remove `final` and rethrow.
  - Then prune.
- **Sharing with the existing functions:** factor the snapshot and prune bodies into internal helpers taking `(installId, dest, nowMs)` so `snapshot(Store&, …)` and `prune(const Store&, …)` keep their signatures and tests.
- **`jobState`:** row `('job_state', JSON)` in `settings`, with keys `lastPurgeDay`, `lastBackupDay`, `backupAlertDay` (ISO dates) and `backupFailingSinceMs`. Missing keys are empty; unparseable JSON reads as empty.

- [ ] **Step 4: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`.

- [ ] **Step 5: Commit**

```bash
git add core
git commit -m "feat(core): cancellable backup run (snapshot, verify, prune) on its own connection; job state; Drive folder names"
```

---

### Task 2: Rotating log file

**Files:**
- Create: `app/src/log/Log.h/.cpp`, `app/tests/tst_log.cpp`
- Modify: `app/CMakeLists.txt`

**Interfaces:**
- Produces (`namespace applog`):
  - `class LogFile { public: LogFile(QString dir, qint64 maxBytes = 1'048'576, int files = 7); bool isOpen() const; void write(const QString& line); QString path() const; };` (appends `line` + `\r\n`, flushes, rotates first when the write would pass `maxBytes`)
  - `QString formatLine(QtMsgType type, const QDateTime& when, const QString& thread, const QString& message)`
  - `bool shouldWrite(QtMsgType type, bool debugBuild)`: false only for `QtDebugMsg` in release
  - `void setThreadName(const QString& name)` (thread_local; unnamed threads use the numeric id; the GUI thread is `main`)
  - `void startBuffering()`: installs the handler that keeps the first 256 lines and chains to the previous handler
  - `void install(const QString& logsDir)`: opens the `LogFile`, writes the buffer in order, then every message; still chains
  - `void uninstall()`: restores the handler that was there before `startBuffering`/`install`
  - `QString currentLogPath()` (empty when not installed)
  - `using FatalHook = void (*)(const char* message); void setFatalHook(FatalHook)`: called for `QtFatalMsg` after the line is written (Task 3 uses it)

- [ ] **Step 1: Write the failing tests** (`QTemporaryDir`; `LogFile` with a small `maxBytes` for rotation)

```cpp
void formatsLines()              // formatLine(QtWarningMsg, 2026-10-18 06:00:01.123, "backup", "x")
                                 // == "2026-10-18 06:00:01.123 W [backup] x"; D/I/C/F letters for the other types
void rotatesAtLimitKeepsSeven()  // LogFile(dir, 100, 7); write 60 lines of 30 chars → files are noite.log and noite.1–6.log,
                                 // none larger than 100 bytes + one line; the newest line is in noite.log, the oldest
                                 // surviving one in noite.6.log
void rotationFailureKeepsLogging() // a directory named noite.1.log in dir; LogFile(dir, 100) → write 10 lines → no crash,
                                   // the last line is in noite.log
void bufferedLinesComeFirst()    // startBuffering(); qWarning("early"); install(dir); qWarning("late") → noite.log lines
                                 // in order "early", "late"; uninstall()
void bufferKeeps256()            // startBuffering(); 300 qInfo lines; install → the file holds the first 256 and the later ones
                                 // written after install
void threadsDoNotTearLines()     // install; two QThreads × 500 qInfo of 80 chars → every line in the file matches the format
                                 // regex and has 80 chars of message
void debugDroppedInRelease()     // shouldWrite(QtDebugMsg, false) false; (QtDebugMsg, true) true; (QtWarningMsg, false) true
void uninstallRestoresPrevious() // a test handler installed first; startBuffering + install + uninstall → qWarning reaches the
                                 // test handler only
```

- [ ] **Step 2: Run to verify they fail**

Run: the test command. Expected: compile errors (`applog`).

- [ ] **Step 3: Implement**

- **State:** one mutex-guarded global (the buffer, the `LogFile`, the previous handler).
- **The handler:** formats, writes (if `shouldWrite(type, ` debug-build `)`), then calls the previous handler, so QTest's message checks keep working inside tests.
- **Rotation:** remove `noite.(files-1).log`, rename `i → i+1` downward, `noite.log → noite.1.log`, reopen. Any failed remove or rename is ignored: when `noite.log` can't be renamed, it is reopened in append mode and grows past the limit until the next try.

- [ ] **Step 4: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`.

- [ ] **Step 5: Commit**

```bash
git add app
git commit -m "feat(log): rotating log files in data/logs (7 x 1 MB) behind a chaining Qt message handler"
```

---

### Task 3: Crash handler, crash helper, Release PDBs

**Files:**
- Create: `app/src/platform/CrashHandler.h/.cpp`, `app/tests/crash_helper.cpp`, `app/tests/tst_crash.cpp`
- Modify:
  - `app/CMakeLists.txt`: link `dbghelp`; add the `noite_crash_helper` executable, built next to `app-tests` (`add_dependencies(app-tests noite_crash_helper)`)
  - `CMakeLists.txt`: Release PDB flags for MSVC

**Interfaces:**
- Consumes: Task 2's `applog::install`, `currentLogPath`, `setFatalHook`.
- Produces (`namespace crash`):
  - `void install(const QString& logsDir)`:
    - prunes dumps to the newest 3;
    - prepares the wide paths;
    - sets the hooks: `SetUnhandledExceptionFilter`, `std::set_terminate`, `_set_purecall_handler`, `_set_invalid_parameter_handler`, `signal(SIGABRT)`, `applog::setFatalHook`.
  - `void pruneDumps(const QString& logsDir, int keep = 3)`
  - `QString formatFrame(const QString& module, quint64 offset)`: `"noite.exe+0x1a2b3c"` (lower-case hex, no padding)
- `noite_crash_helper.exe <logsDir> av|throw|fatal`: `applog::install(logsDir)`, `crash::install(logsDir)`, then null write / `throw std::runtime_error` out of `main` / `qFatal("boom")`.

- [ ] **Step 1: Write the failing tests**

```cpp
void formatsFrames()            // formatFrame("noite.exe", 0x1a2b3c) == "noite.exe+0x1a2b3c"
void prunesDumpsToThree()       // 5 crash-*.dmp files with ascending names → pruneDumps(dir) → the 3 newest by name remain;
                                // noite.log untouched
void crashLeavesTraceAndDump_data() // rows "av", "throw", "fatal"
void crashLeavesTraceAndDump()  // QProcess runs <app-tests dir>/noite_crash_helper.exe <tmp> <kind>, waits ≤ 30 s →
                                // exit code != 0; noite.log has a line containing "CRASH 0x", at least 3 lines matching
                                // "^  \S+\+0x[0-9a-f]+$", a "dump: crash-" line; exactly one crash-*.dmp, size > 0
```

- [ ] **Step 2: Run to verify they fail**

Run: the test command. Expected: compile errors (`crash::`), and the helper target missing.

- [ ] **Step 3: Implement**

- **One report routine,** `writeReport(EXCEPTION_POINTERS*)`:
  - Writes with `CreateFileW(FILE_APPEND_DATA)` on the prepared log path, plus `WriteFile`, with no heap allocation (stack buffers, `GetModuleHandleExW` + `GetModuleFileNameW` for module names).
  - Walks the stack with `StackWalk64` from the context's copy.
  - Writes `MiniDumpWriteDump(MiniDumpNormal | MiniDumpWithThreadInfo)` to the prepared dump name (stamped at crash time with `GetLocalTime` into a stack buffer).
- **Hooks without a context** (terminate, purecall, invalid parameter, `SIGABRT`, the fatal hook): `RtlCaptureContext` into a local `CONTEXT`. They use code `0xE0000001` for terminate/abort/fatal and `0xE0000002` for purecall/invalid parameter.
- **Ending the process:**
  - After reporting: `TerminateProcess(GetCurrentProcess(), code)`.
  - The filter returns `EXCEPTION_EXECUTE_HANDLER`, so no Windows Error Reporting dialog appears.
  - `install` also calls `_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT)` and `SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX)`, so no dialog ever appears (the tests run unattended).
- **Recursion:** a second crash during the report terminates at once (an atomic flag).
- **Release PDBs:**

  ```cmake
  if(MSVC)
      add_compile_options("$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:/Zi>")
      add_link_options("$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>")
  endif()
  ```

  Check that `tools/` packaging copies only the exe and DLLs, never `*.pdb`. If it globs, exclude them.

- [ ] **Step 4: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`. Also `tools/build.ps1 -Config Release` produces `build/Release/app/noite.pdb`.

- [ ] **Step 5: Commit**

```bash
git add app CMakeLists.txt tools
git commit -m "feat(platform): crash handler writes a module+offset trace and a minidump to data/logs; Release PDBs"
```

---

### Task 4: Drive folder detection and the backup folder dialog

**Files:**
- Create: `app/src/platform/DriveFolder.h/.cpp`, `app/src/jobs/BackupFolderDialog.h/.cpp`, `app/tests/tst_drive_folder.cpp`, `app/tests/tst_backup_folder_dialog.cpp`
- Modify: `app/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1's `noite::googleDriveFolderNames()`.
- Produces:
  - `struct DriveVolume { QString root; QString label; };`
  - `std::optional<QString> suggestedDriveFolder(const QList<DriveVolume>& volumes, const std::function<bool(const QString&)>& dirExists)`: the first volume whose label equals `Google Drive` (case-insensitive); then the first existing `<root>/<name>`; else the root, with `/` separators
  - `QList<DriveVolume> mountedDriveVolumes()` (`QStorageInfo::mountedVolumes()`, ready and valid only)
  - `class BackupFolderDialog : public QDialog`, with `BackupFolderDialog(QString driveFolder, std::function<QString()> pickFolder, QWidget* parent = nullptr)`:
    - `std::optional<QString> chosenFolder() const`
    - `QPushButton* driveButton() const`, `otherButton() const`, `cancelButton() const`
    - `QString shownPath() const`
  - `std::optional<QString> askBackupFolder(QWidget* parent = nullptr)`: Drive found → the dialog; else `QFileDialog::getExistingDirectory`; empty → nothing

- [ ] **Step 1: Write the failing tests**

`tst_drive_folder.cpp`:
```cpp
void picksMeuDrive()     // {{"C:/", "Windows"}, {"G:/", "Google Drive"}}, exists("G:/Meu Drive") → "G:/Meu Drive"
void picksMyDrive()      // label "GOOGLE DRIVE", exists("G:/My Drive") → "G:/My Drive"
void fallsBackToRoot()   // label "Google Drive", nothing exists → "G:/"
void noDriveVolume()     // {{"C:/", "Windows"}} → nullopt
```
`tst_backup_folder_dialog.cpp`:
```cpp
void showsDrivePath()     // BackupFolderDialog("G:/Meu Drive", picker) → windowTitle "Back up noite";
                          // shownPath() == "G:\\Meu Drive\\noite-backups"; button texts as in Global Constraints
void driveButtonChoosesDrive() // click driveButton → result Accepted; chosenFolder() == "G:/Meu Drive"
void otherButtonUsesPicker()   // picker returns "D:/bk" → click otherButton → chosenFolder() == "D:/bk";
                               // picker returns "" → dialog stays open, chosenFolder() empty
void cancelChoosesNothing()    // click cancelButton → Rejected; chosenFolder() empty
```

- [ ] **Step 2: Run to verify they fail**

Run: the test command. Expected: compile errors (`suggestedDriveFolder`, `BackupFolderDialog`).

- [ ] **Step 3: Implement**

A plain `QDialog` (label, path label, button row). `driveButton` is the default button.

- [ ] **Step 4: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`.

- [ ] **Step 5: Commit**

```bash
git add app
git commit -m "feat(jobs): Google Drive folder detection and the one-click backup folder dialog"
```

---

### Task 5: BackupRunner and ThreadBackupRunner

**Files:**
- Create: `app/src/jobs/BackupRunner.h/.cpp`, `app/tests/tst_backup_runner.cpp`
- Modify: `app/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1's `backup::Job`, `Result`, `run`; Task 2's `applog::setThreadName`.
- Produces:
  - `class BackupRunner : public QObject`:
    - `virtual void start(const noite::backup::Job&) = 0;`
    - `virtual bool busy() const = 0;`
    - `virtual void cancelAndWait() = 0;`
    - signals `finished(noite::backup::Result)`, `failed(noite::ErrorCode, QString)`
  - `class ThreadBackupRunner final : public BackupRunner`: one `QThread` named `backup` (it calls `applog::setThreadName("backup")`). `start` while busy is ignored. The signals are queued to the runner's thread (main). The destructor calls `cancelAndWait()`.
  - `Q_DECLARE_METATYPE` / `qRegisterMetaType` for `Result` and `ErrorCode`

- [ ] **Step 1: Write the failing tests** (store fixture; real temp dirs)

```cpp
void finishesOnMainThread()   // start(job) → QSignalSpy(finished).wait(10'000); the slot connected to finished ran on
                              // QThread::currentThread() == qApp->thread(); result.path exists; busy() false afterwards
void failureIsReported()      // dest absent → failed(BackupDirMissing, message containing the path)
void busyIgnoresSecondStart() // start twice back to back → exactly one finished within 10 s
void cancelAndWaitIsPrompt()  // seed ~20 MB of attachments; start; cancelAndWait() returns within 2 s (QElapsedTimer);
                              // no *.db in the snapshots dir OR exactly one complete .db that verify() accepts
                              // (the run may finish first); no *.tmp
```

- [ ] **Step 2: Run to verify they fail**

Run: the test command. Expected: compile errors (`ThreadBackupRunner`).

- [ ] **Step 3: Implement**

- **Starting a run:** `start` stores the job, clears the `std::atomic<bool>` cancel flag, and posts the work to a worker `QObject` living on the thread.
- **Inside the run:** `backup::run` is wrapped in a try/catch that turns `noite::Error` into `failed(code, what)` and anything else into `failed(Io, what)`.
- **`cancelAndWait`:** sets the flag, `quit()`, `wait()`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`.

- [ ] **Step 5: Commit**

```bash
git add app
git commit -m "feat(jobs): backup runner on its own thread, cancellable"
```

---

### Task 6: JobScheduler

**Files:**
- Create: `app/src/jobs/JobScheduler.h/.cpp`, `app/tests/tst_job_scheduler.cpp` (with a `FakeBackupRunner` that records jobs and lets the test emit `finished`/`failed`)
- Modify: `app/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1's `jobState`/`saveJobState`, `purgeTrash`, `Settings::backupDir`/`backupFrequency`/`backupKeep`/`trashRetentionDays`; Task 5's `BackupRunner`.
- Produces: `JobScheduler(noite::Store&, std::function<QDateTime()> now, BackupRunner&, QObject* parent = nullptr)`
  - `void check()`, `void startTimers()` (the 30 s single shot plus the hourly timer; tests don't call it)
  - `bool backUpNow()`: false when busy or no `backupDir`; ignores the schedule; its result is always toasted
  - `bool isBusy() const`
  - signals `toast(QString title, QString text)`, `busyChanged(bool)`, `purged(noite::PurgeReport)`
  - constants `kFirstCheckMs`, `kCheckIntervalMs`, `kAlertAfterMs`

- [ ] **Step 1: Write the failing tests** (a mutable `QDateTime now` starting Thu 2026-10-15 09:00 local; `backupDir` set to a temp dir unless the test says otherwise)

```cpp
void purgesOncePerDay()          // a note trashed 31 days ago; check() → purged emitted with scratchesDeleted 1;
                                 // jobState().lastPurgeDay == 10-15; check() again → no second purged;
                                 // now 10-16 00:30 → check() → purged again (empty report)
void dailyBackupOncePerDay()     // check() → fake has 1 job (dest == backupDir, keep == 14, nowMs == now);
                                 // emit finished → lastBackupDay 10-15; check() → still 1 job
void catchesUpAfterDaysOff()     // lastBackupDay 10-10 → check() at 10-15 → a job
void weeklyDueOnDaySeven()       // Weekly, lastBackupDay 10-09 → check() at 10-15 → no job; at 10-16 → a job
void weeklyWithFutureLastDayIsDue() // Weekly, lastBackupDay 10-20 → check() at 10-15 → a job
void noFolderNoBackupNoToast()   // backupDir unset → check() → no job, no toast; backUpNow() false
void failureRetriesNextCheck()   // check → emit failed(BackupDirMissing, "backup folder not found: G:/x") → no toast;
                                 // backupFailingSinceMs == now; check() → a second job
void alertAfterThreeDaysOncePerDay() // failingSince = 10-12 09:00; check at 10-15 08:59 → failed → no toast;
                                     // 10-15 09:00 → failed → toast "Backups have been failing since Oct 12: <msg>";
                                     // 10-15 10:00 → failed → no toast; 10-16 09:00 → failed → toast again
void successClearsFailure()      // failing state set → finished → backupFailingSinceMs and backupAlertDay empty
void backUpNowToastsResult()     // backUpNow() true → job; finished with bytes 14'889'779 → toast ("noite", "Backed up · 14.2 MB");
                                 // lastBackupDay today; backUpNow → failed(…, "disk full") → toast "Backup failed: disk full"
void backUpNowWhileBusyRefused() // check() starts a job (busy) → backUpNow() false; busyChanged(true) then, after finished,
                                 // busyChanged(false)
void purgeFailureRetries()       // store with a pending write lock from a second connection (busy timeout) → check() logs,
                                 // lastPurgeDay stays empty; lock released → check() → purged
```
For the last test, if a held write lock isn't deterministic, substitute a store whose `trashRetentionDays` is rejected by `purgeTrash`, written through a raw `settings` row with `trashRetentionDays: 0`, and note the substitution.

- [ ] **Step 2: Run to verify they fail**

Run: the test command. Expected: compile errors (`JobScheduler`).

- [ ] **Step 3: Implement**

- **On each `check()`:** read `store.settings()` and `jobState()`, apply the rules in Global Constraints, and save `JobState` after each change.
- **The no-folder skip:** logged at most once per local day (an in-memory day).
- **Telling manual from scheduled runs:** a member flag says whether the running job came from `backUpNow` (toast always) or from the schedule (toast only for the alert).
- **Errors:**
  - purge errors: `purge: failed: <msg>`;
  - backup results: `backup: ok …` and `backup: failed: …`, as in Global Constraints;
  - store errors while saving `JobState` are logged and never thrown out of a slot.

- [ ] **Step 4: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`.

- [ ] **Step 5: Commit**

```bash
git add app
git commit -m "feat(jobs): job scheduler: daily purge, daily/weekly backup with catch-up, 3-day failure alert"
```

---

### Task 7: App wiring, tray, spec

**Files:**
- Modify:
  - `app/src/App.h/.cpp`, `app/src/AppOptions.h` (`bool crashHandler = false;`), `app/src/main.cpp`, `app/tests/tst_app.cpp`
  - `docs/spec.md` (D13, D22, D29, D25 tray)
  - `docs/testing/manual-capture-checks.md` (a new "Jobs" section, J1–J5, verbatim from the design)
  - `docs/superpowers/plans/2026-10-15-noite-roadmap.md` (the M5 row's plan link)

**Interfaces:**
- Consumes: Tasks 2–6.
- Produces:
  - `JobScheduler* App::jobs() const`
  - `QAction* App::backupAction() const`
  - `void App::backUpNow()`
  - `void App::setBackupFolderAsker(std::function<std::optional<QString>()>)` (defaults to `askBackupFolder`; tests replace it)
  - signal `App::toastShown(QString title, QString text)`, emitted with each toast (for tests, since there's no tray offscreen)

- [ ] **Step 1: Write the failing tests** (`tst_app.cpp`)

```cpp
void logFileAfterStart()          // start() → <data>/logs/noite.log contains "noite started · data "
void backupActionInTray()         // backupAction() text "Back up now", enabled; while jobs()->isBusy() → "Backing up…", disabled
void backUpNowAsksWhenNoFolder()  // asker returns "<tmp>" → backUpNow() → settings().backupDir == "<tmp>";
                                  // toastShown "Backed up · …" within 10 s; asker returns nullopt → nothing changes, no toast
void backUpNowWithMissingFolderToastsError() // backupDir = "<tmp>/gone" → backUpNow() → asker never called;
                                             // toastShown "Backup failed: backup folder not found: …"; backupDir unchanged
void purgeReloadsOpenBoard()      // a note trashed 31 days ago; showBoard() on the Trash tab with 1 card →
                                  // jobs()->check() → the board shows 0 cards
void quitDuringBackupIsClean()    // backupDir set; backUpNow(); destroy the App at once → no crash; snapshots dir has no *.tmp
```

- [ ] **Step 2: Run to verify they fail**

Run: the test command. Expected: compile errors (`jobs`, `backupAction`, `setBackupFolderAsker`).

- [ ] **Step 3: Implement**

- **`main.cpp`:** `applog::startBuffering()` before `QApplication`. The options come from `parseArgs` with `crashHandler = true`.
- **`App::start`,** after the data folder lock is held:
  - `applog::install(dir.logsDir())` and, if `options_.crashHandler`, `crash::install(...)`;
  - the `noite started …` line;
  - create the `ThreadBackupRunner` and the `JobScheduler` (`now = QDateTime::currentDateTime`) with `startTimers()`;
  - connect `host_` `resumed` → `jobs->check`;
  - `purged` → the board's `reload()` when it's open;
  - `toast` → `tray_->showMessage` (if there is a tray) and `toastShown`;
  - `busyChanged` → the action's text and enabled state.
- **`App::backUpNow`:**
  - no `backupDir` → the asker; on a path, save `Settings.backupDir`, then `jobs->backUpNow()`;
  - a set but missing folder goes straight to `jobs->backUpNow()`, whose failure toast covers it.
- **Tray:** the `Back up now` action in its place (Global Constraints).
- **Shutdown:** `~App` calls `runner->cancelAndWait()` before closing windows and the store, logs `noite quit`, then `applog::uninstall()` last.
- **Spec edits:** as listed in the design's "Spec edits".

- [ ] **Step 4: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`.

- [ ] **Step 5: Commit**

```bash
git add app docs
git commit -m "feat(app): scheduled purge and backup, Back up now with the Drive dialog, logs and crash handler at start; spec D13/D22/D29"
```
