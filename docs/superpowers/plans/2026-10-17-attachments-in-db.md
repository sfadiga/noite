# Attachments in the Database Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pasted images and copied files live in `noite.db` (an `attachments` table) instead of `data\attachments\`, so one file holds all of the data.

**Architecture:**
- Schema v2 adds the table, and the store keeps the database in incremental auto-vacuum.
- `Store::putAttachment` writes rows; `Store::attachmentBytes` replaces `attachmentPath`. The purge deletes rows and reclaims space. Backups become self-contained `VACUUM INTO` files.
- The app reads bytes, not paths: images via `QImage::fromData`. File chips are written to `%TEMP%\noite\` to open.

**Tech Stack:** C++20, Qt 6.10 (Core/Gui/Widgets/Test), SQLite (vendored), CMake + Ninja + MSVC.

**Spec:** `docs/superpowers/specs/2026-10-17-attachments-in-db-design.md`

## Global Constraints

- Attachment ids stay `<sha256>.<ext>`; note HTML (`noite-att:<id>`) is never rewritten (A2).
- 25 MiB limit (`kMaxAttachmentBytes`) and extension rules unchanged (D21).
- Grace period `kAttachmentGraceMs` (1 day) now runs on `attachments.touched_at` (ms, from the store's clock).
- No import of existing `data\attachments\` files; no v1-backup compatibility code (A4).
- A snapshot is one self-contained `.db`; prune's keep count is unchanged (A3).
- Ctrl+click temp files go to `%TEMP%\noite\<first 12 hex of id>\<name>`; runnable types return the folder; `%TEMP%\noite` is emptied at startup (A5).
- Test command: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -Config Debug -Test`, which prints `TOTAL failures: N`.
- Edit files with the Edit/Write tools only (no PowerShell `Get-Content` round-trips).

## Review Focus

1. Blob missing for a referenced id (e.g. an old note pasted before this change) → the editor shows a broken image and the card shows no thumbnail; no exception escapes. Test: Task 2 `missingBlobGivesNullImage`.
2. A chip name with characters Windows rejects (`a:b?.pdf`, `..\x.pdf`, empty) → a safe file name inside the temp folder, never outside it. Test: Task 3 `chipNameIsSanitised`.
3. Re-pasting bytes that already exist after their row aged → `touched_at` restarts, so the purge spares them. Test: Task 2 `rePastedOldAttachmentSurvivesPurge` (rewritten).
4. Purge on a database with big blobs → the file actually shrinks. Test: Task 2 `purgeSweepsRowsAfterGraceAndShrinksFile`.
5. The same chip opened twice → the second open reuses the file (no "file in use" error from overwriting an open PDF). Test: Task 3 `openTargetReusesExistingFile`.

---

### Task 1: Schema v2 and the data folder layout

**Files:**
- Create: `core/src/schema_v2.sql`
- Modify: `core/src/schema.h.in`, `core/CMakeLists.txt:2-4,21`, `core/src/migrate.h` (`kLatest`), `core/src/migrate.cpp` (`kMigrations`), `core/src/store.cpp` (`Store::open`), `core/include/noite/data_dir.h` (drop `attachmentsDir()`), `core/src/data_dir.cpp` (`acquire`)
- Test: `core/tests/tst_migrate.cpp`, `core/tests/tst_store_open.cpp`, `core/tests/tst_data_dir.cpp`

**Interfaces:**
- Produces: table `attachments(id TEXT PRIMARY KEY, data BLOB NOT NULL, size INTEGER NOT NULL, touched_at INTEGER NOT NULL)`; `kLatest == 2`; `Store::schemaVersion() == 2`; `PRAGMA auto_vacuum` returns 2 (INCREMENTAL) on every opened store. `DataDir::attachmentsDir()` no longer exists.

- [ ] **Step 1: Write the failing tests**

`tst_migrate.cpp`:
```cpp
void v2AddsAttachmentsTable()
{
    QTemporaryDir tmp;
    Db db = Db::open(tmp.filePath("noite.db"));
    migrate(db, kMigrations, tmp.filePath("snap"), 1000);
    QCOMPARE(kLatest, 2);
    QCOMPARE(db.userVersion(), 2);
    QCOMPARE(tableCount(db, "attachments"), 1);
}
```
`tst_store_open.cpp`: change `openCreatesLatestSchema` to expect `2`; add:
```cpp
void autoVacuumIsIncremental()   // fresh store, then reopened: rawQuery(..., "PRAGMA auto_vacuum") == "2"
void v1DatabaseOpensAsV2()       // Db::open + migrate(db, {kMigrations[0]}, ...) + a scratches row inserted by SQL;
                                 // Store::open → schemaVersion()==2, the scratch is listed, auto_vacuum "2",
                                 // a migration-backups/noite-v1-*.db exists
void emptyAttachmentsFolderRemoved() // mkpath root/attachments, Store::open → !exists
void nonEmptyAttachmentsFolderKept() // root/attachments/x.png written, Store::open → file still there
```
`tst_data_dir.cpp`: `acquireCreatesLayout` and `secondAcquireFailsUntilReleased` assert `!QDir(root + "/attachments").exists()` instead of the `attachmentsDir()` checks.

- [ ] **Step 2: Run the tests to verify they fail**

Run: the test command. Expected: a compile error (`attachmentsDir` is still used by the tests, which were changed to the root path) or FAIL in `v2AddsAttachmentsTable` (`kLatest` 1) and `openCreatesLatestSchema` (actual 1).

- [ ] **Step 3: Implement**

- `schema_v2.sql`: the `CREATE TABLE attachments` above. Generate `kSchemaV2` like `kSchemaV1` (`file(READ …)`, `schema.h.in`, `CMAKE_CONFIGURE_DEPENDS`, source list). `kMigrations = {{1, kSchemaV1}, {2, kSchemaV2}}`, `kLatest = 2`.
- `Store::open`, after `migrate`: if `PRAGMA auto_vacuum` ≠ 2, `exec("PRAGMA auto_vacuum = INCREMENTAL; VACUUM;")`; then `QDir().rmdir(dir.root() + "/attachments")` (it only removes an empty folder; ignore the result).
- `DataDir::acquire` stops creating `/attachments`; remove `attachmentsDir()`. Every remaining user of `attachmentsDir()` (`store_attachments.cpp`) keeps compiling by using `dataDir().root() + "/attachments"` until Task 2 deletes that code.

- [ ] **Step 4: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`. Old attachment tests still pass, because `putAttachment` creates the folder through `QSaveFile`. If not, add `QDir().mkpath` in `putAttachment` for this task only.

- [ ] **Step 5: Commit**

```bash
git add core
git commit -m "feat(core): schema v2 adds the attachments table; incremental auto-vacuum; no attachments folder"
```

---

### Task 2: Attachments stored in the database

**Files:**
- Modify: `core/include/noite/error.h` (add `AttachmentMissing`), `core/src/error.cpp` if codes are named there, `core/include/noite/store.h:72-80`, `core/src/store_attachments.cpp`, `core/src/backup.cpp`, `core/include/noite/backup.h` (comments: layout, verify)
- Modify: `app/src/editor/AttachmentStore.h/.cpp`, `app/tests/fake_attachments.h`, `app/src/editor/NoteEdit.h/.cpp` (`loadResource`, `openTargetFor`), `app/src/board/CardDelegate.cpp` (`thumbnail`)
- Test: `core/tests/tst_attachments.cpp`, `core/tests/tst_backup.cpp`, `app/tests/tst_paste.cpp`, `app/tests/tst_card_delegate.cpp`, `app/tests/tst_editor.cpp`

**Interfaces:**
- Consumes: Task 1's table and auto-vacuum.
- Produces:
  - `QString Store::putAttachment(const QByteArray& bytes, const QString& ext)` (unchanged signature).
  - `QByteArray Store::attachmentBytes(const QString& id) const`: throws `InvalidAttachmentId` or `AttachmentMissing`.
  - `Store::attachmentPath` is removed.
  - `AttachmentStore::bytes(const QString& id) const → QByteArray`, which may throw `noite::Error`, replaces `pathFor`.
  - `NoteEdit::setOpenFolder(const QString& dir)` (default `QDir::tempPath() + "/noite"`).
  - `QUrl NoteEdit::openTargetFor(const QString& attachmentId) const` writes the bytes to `<openFolder>/<first 12 hex>/<id>` and returns that file, or its folder for runnable extensions. Task 3 adds the chip name.

- [ ] **Step 1: Write the failing core tests** (rewrite `tst_attachments.cpp` and `tst_backup.cpp` against the database; no test may reference files under `data/attachments`)

```cpp
void putDedupsAndStampsTouchedAt()    // put same bytes twice → same id; SELECT count(*) FROM attachments == 1;
                                      // attachmentBytes(id) == bytes; advance clock 1000, put again → touched_at == t0()+1000
void attachmentBytesErrors()          // "../x", "", "zz.png" → InvalidAttachmentId; fakeAttachmentId() → AttachmentMissing
void rejectsOversized()               // kMaxAttachmentBytes+1 → AttachmentTooLarge; table empty
void purgeSweepsRowsAfterGraceAndShrinksFile() // put 4 MiB unreferenced; set touched_at = t0()-2*kDayMs via SQL helper;
                                      // file size before > 4 MiB; purgeTrash(30) → attachmentsDeleted==1, row gone,
                                      // noite.db size (after store.reset() + WAL checkpoint, or PRAGMA page_count) shrank by ≥ 3 MiB
void unreferencedFreshAttachmentSurvives()     // touched_at = t0()-1h → kept
void cloneKeepsSharedImageAfterOriginalPurged() // the spec's attachment-safety case, bytes still readable
void rePastedOldAttachmentSurvivesPurge()      // touched_at 40 days old, referenced by a trashed-and-expired note,
                                               // re-put → survives the purge that deletes the note
```
Delete `staleTempFilesCleaned` and `attachmentPathRejectsTraversal` (no files any more; covered by `attachmentBytesErrors`). Keep the retention/lifecycle tests as they are.

`tst_backup.cpp`:
```cpp
void snapshotIsSelfContained()  // put image + note; snapshot; no "<install>/attachments" folder in dest;
                                // copy snapshot to a fresh data root (no attachments folder), Store::open →
                                // attachmentBytes(image) == original bytes; verify(path) passes
void verifyMissingRowFails()    // snapshot, then DELETE FROM attachments in the snapshot file → verify throws BackupInvalid
```
Replace `snapshotLayoutAndContents`/`snapshotRestoresIntoFreshStore`/`verifyDetectsMissingAttachment` with these; delete `pruneSweepsUnreferencedBackupAttachments`. Keep the count/tmp/two-installations prune tests.

- [ ] **Step 2: Run to verify they fail**

Run: the test command. Expected: compile errors (`attachmentBytes`, `AttachmentMissing` undefined).

- [ ] **Step 3: Implement core**

- `putAttachment`: same size and extension checks and id. `INSERT OR IGNORE INTO attachments(id, data, size, touched_at)`, then `UPDATE attachments SET touched_at = ? WHERE id = ?` (now).
- `attachmentBytes`: validate the id, then `SELECT data`.
- `purgeTrash` sweep: `DELETE FROM attachments WHERE touched_at <= ?` for ids not in the referenced set (loop the candidates in a transaction), then `exec("PRAGMA incremental_vacuum")`. Remove the file sweep and `stampMtime`.
- `backup::snapshot`: `VACUUM INTO` the tmp, then rename; no attachments dir. `verify`: `integrity_check`, plus every `attachmentRefs` id must exist in the snapshot's `attachments` table. `prune`: drop the attachments-folder handling and its tmp cleanup.

- [ ] **Step 4: Write the failing app tests**

- `tst_paste.cpp`: replace `QFile stored(store->pathFor(...))` with `store->bytes(id)` comparisons. Replace the `openTargetFor` checks with:
```cpp
void openTargetWritesTempFile() // edit->setOpenFolder(tmp.path()); target = openTargetFor(pdf);
                                // target.toLocalFile() starts with tmp.path(), file content == store->bytes(pdf);
                                // openTargetFor(exe) == QUrl::fromLocalFile(folder holding the written exe)
```
- `tst_card_delegate.cpp` `paintsCardsWithoutCrashing`: replace the `QFile::remove(attachmentPath(gone))` with `DELETE FROM attachments WHERE id = gone` through a core test helper (add `void deleteAttachmentRow(Store&, const QString& id)` to `core/tests/helpers.h`, using `Store::db()` the same way `backup.cpp` does).
- `tst_editor.cpp`:
```cpp
void missingBlobGivesNullImage() // NoteEdit with StoreAttachments over a fixture; loadResource(ImageResource,
                                 // QUrl("noite-att:" + fakeAttachmentId())) is a null QImage / invalid QVariant; no throw
void imageLoadsFromBytes()       // put pngBytes via the store, loadResource returns a non-null 8x8 QImage
```
`FakeAttachments` keeps a `QHash<QString, QByteArray>`. `bytes()` throws `noite::Error(ErrorCode::AttachmentMissing, …)` for unknown ids; `ids()` returns the hash keys.

- [ ] **Step 5: Run to verify they fail**

Run: the test command. Expected: compile errors (`bytes`, `setOpenFolder` undefined).

- [ ] **Step 6: Implement app**

`AttachmentStore::bytes` replaces `pathFor`. `StoreAttachments::bytes` forwards to `attachmentBytes`. `loadResource` and `CardDelegate::thumbnail` use `QImage::fromData(bytes)` inside `try/catch (const noite::Error&)`, and log missing blobs with `qWarning`. `openTargetFor` writes the bytes with `QSaveFile` and returns `{}` on any error (logged).

- [ ] **Step 7: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`.

- [ ] **Step 8: Commit**

```bash
git add core app
git commit -m "feat: attachments live in noite.db; images load from bytes; self-contained backups"
```

---

### Task 3: Opening file chips, startup cleanup, spec

**Files:**
- Modify: `app/src/editor/NoteEdit.h/.cpp` (`openTargetFor` takes the chip name; `mousePressEvent` passes it), `app/src/App.cpp` (startup cleanup), `docs/spec.md` (D4/D13 layout, D7, D13 backup, D21, D22), `docs/testing/manual-capture-checks.md` (rows 6, 7, 9, 10 wording if they mention files on disk)
- Create: `app/src/editor/OpenFolder.h/.cpp`
- Test: `app/tests/tst_paste.cpp`, `app/tests/tst_app.cpp`

**Interfaces:**
- Consumes: Task 2's `NoteEdit::setOpenFolder` and `openTargetFor`.
- Produces:
  - `QUrl NoteEdit::openTargetFor(const QString& attachmentId, const QString& name = {}) const`
  - `QString safeFileName(const QString& name, const QString& fallback)` in `OpenFolder.h`
  - `void clearOpenFolder(const QString& dir)` in `OpenFolder.h` (best-effort, removes the contents)
  - `QString defaultOpenFolder()` returns `QDir::tempPath() + "/noite"`.

- [ ] **Step 1: Write the failing tests**

`tst_paste.cpp`:
```cpp
void chipOpensUnderItsName()       // openTargetFor(pdf, "Report Q3.pdf").fileName() == "Report Q3.pdf", under <tmp>/<12 hex>/
void chipNameIsSanitised()         // safeFileName("a:b?.pdf", id) == "a_b_.pdf"; safeFileName("..\\x.pdf", id) has no
                                   // '\\' or '/' and isn't ".." ; safeFileName("  ", id) == id; result never contains
                                   // any of <>:"/\|?* or control chars, and doesn't end with '.' or ' '
void openTargetReusesExistingFile() // open twice → same path; second call doesn't rewrite (mtime unchanged, file kept open
                                    // with QFile for reading during the second call succeeds)
void ctrlClickPassesChipText()     // paste a copied file (existing test helper), Ctrl+click the chip with
                                   // edit->setOpenFolder(tmp) and QDesktopServices handler for "file" registered to a
                                   // test slot → the opened URL's fileName() == the pasted file's name
```
`tst_app.cpp`:
```cpp
void startupClearsOpenFolder()     // write <defaultOpenFolder()>/abc/x.txt, construct the App via the existing
                                   // prepare(...) helper → the file is gone
```

- [ ] **Step 2: Run to verify they fail**

Run: the test command. Expected: compile errors (`safeFileName`, the two-argument `openTargetFor`).

- [ ] **Step 3: Implement**

- `safeFileName`: replace `<>:"/\|?*` and chars < 0x20 with `_`, trim, strip trailing dots and spaces; if the result is empty, `.` or `..`, return `fallback`.
- `openTargetFor`: path `<openFolder>/<id.left(12)>/<safeFileName(name, id)>`. If a file with the same size already exists, return it unchanged; otherwise write it with `QSaveFile`. Runnable extensions are judged on the **id's** extension, as today, and return the folder.
- `mousePressEvent`: get the chip text by walking the block fragments at `cursorForPosition(pos)` until one's `charFormat().anchorHref()` equals the href.
- `App`: call `clearOpenFolder(defaultOpenFolder())` once during startup, before showing anything.
- Spec edits as listed in the design's "Spec edits".

- [ ] **Step 4: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`.

- [ ] **Step 5: Commit**

```bash
git add app docs
git commit -m "feat(editor): file chips open from %TEMP%\\noite under their name; spec: attachments in the database"
```
