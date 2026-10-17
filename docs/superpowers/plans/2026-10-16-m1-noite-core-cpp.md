# M1: `noite-core` C++ Storage Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A C++20 library that owns all of noite's persistent state (scratches, lifecycle, bundles, search, attachments, trash purge, settings, backups), tested end-to-end with Qt Test and no Widgets.

**Architecture:**
- **Library:** `core/` builds a static library `noite-core`. It links SQLite (compiled from the vendored amalgamation in `third_party/sqlite/`, with FTS5), `Qt6::Core` and `Qt6::Gui`. The Gui dependency is only for `QTextDocumentFragment::fromHtml` (HTML → plain text); there are no Widgets.
- **`Store`:** owns one `sqlite3*` connection plus a locked `DataDir`, and is the only writer (D14). Writes are non-const methods; errors are thrown as `noite::Error` carrying an `ErrorCode`.
- **Time:** comes from an injected `Clock`.
- **Note content:** Qt rich-text HTML. `content.h` derives the plain text, title, inline tags and `noite-att:` attachment references from it.

The Qt app (M2) links `noite-core` and calls `Store` on the GUI thread.

**Tech Stack:** C++20, MSVC (VS Build Tools 2026), CMake + Ninja, SQLite amalgamation (latest 3.x, vendored), Qt 6.10.3 (Core, Gui, Test), PowerShell build scripts, GitHub Actions (`windows-latest`).

**Spec:** [`docs/spec.md`](../../spec.md): §3 portability & storage, §4 data model, §6 board sort/search, §9 testing. Design input: the superseded [Rust M1 plan](2026-10-15-m1-noite-core-storage.md). Its schema, rules and test cases are carried over here, translated to C++. Planning decisions not in the spec are in the [roadmap](2026-10-15-noite-roadmap.md#decisions-made-while-planning-not-in-the-spec-confirm-on-review).

## Global Constraints

- No network code; no Qt Network or Widgets in `noite-core` (D13, D14).
- All state lives under the data dir: `noite.db`, `attachments\`, `logs\`, `migration-backups\`, `noite.lock` (D4).
- One `Store` per data dir; a second `DataDir::acquire` on the same root throws `ErrorCode::AlreadyRunning` (D27).
- Note content is **Qt rich-text HTML**. Markdown is export-only and not in M1 (D7).
- Attachment id = `<64 lowercase hex SHA-256>.<ext>`, referenced in HTML as `noite-att:<id>`, **never** `noite-att://` (D7). Size limit **26 214 400 bytes** (25 MiB) (D21).
- Title = first non-empty line of the plain text, trimmed, **truncated to 60 grapheme clusters** (D10).
- Colours: exactly `red`, `orange`, `yellow`, `green`, `blue`, `purple` (D10).
- Trash retention **1–365 days, default 30**. Delete when `trashedAt <= now − days × 86 400 000 ms` (D11). Done scratches are never deleted automatically (D11).
- Purge never deletes an attachment file younger than **24 h** (roadmap decision 4).
- Backups are `VACUUM INTO` snapshots, default keep **14**. The live DB is never copied raw (D13).
- Migrations: snapshot before migrating; all pending migrations in one transaction; a newer schema is refused (D26).
- Settings defaults: capture `Ctrl+Alt+J`, board `Ctrl+Alt+B`, retention 30, day start 06:00, backup daily, keep 14, start with Windows on, setup not completed (D24).
- Tests use `QTemporaryDir` only and run on `QT_QPA_PLATFORM=offscreen`. **Nothing in M1 sends OS input or shows windows.**

## Review Focus

1. **Unicode** (pt-BR accents, emoji with ZWJ): title truncation must not split a grapheme, `#reunião` is a tag, and searching `reuniao` finds `Reunião`. Tests in Tasks 4 and 8.
2. **Search box input with FTS5 syntax characters** (`"`, `-`, `*`, `(`, `AND`, `NEAR(`) returns results or an empty list, never throws. Test in Task 8.
3. **`#` that isn't a tag:** URL fragments, `C#`, `issue #12`, `a#b`. Test in Task 4.
4. **Image pasted while autosave is pending:** an unreferenced attachment younger than 24 h survives a purge. Test in Task 9.
5. **Portable folder inside a sync folder** (Google Drive / OneDrive / Dropbox) is detected. Test in Task 2.

---

## File structure

```
CMakeLists.txt                      product root: C + CXX, Qt6 Core/Gui/Test, add_subdirectory(third_party/sqlite, core)
tools/build.ps1                     VS dev shell + CMake/Ninja configure/build; -Test runs tools/run-tests.ps1
tools/run-tests.ps1                 runs each test exe offscreen with -o <file>,txt; prints results; exit code = failures
third_party/sqlite/                 sqlite3.c, sqlite3.h, VERSION (version + SHA3-256 of the zip), CMakeLists.txt
core/CMakeLists.txt                 noite-core static lib + core-tests exe
core/include/noite/error.h          ErrorCode, Error
core/include/noite/clock.h          Clock, SystemClock, FixedClock, kDayMs
core/include/noite/data_dir.h       DataDir, SyncProvider, syncProviderFor
core/include/noite/model.h          aliases, enums, Scratch, ScratchSummary, Bundle, ReviewCounts, PurgeReport
core/include/noite/content.h        pure HTML/text helpers
core/include/noite/settings.h       Settings, BackupFrequency
core/include/noite/store.h          Store
core/include/noite/backup.h         backup::snapshot, prune, verify, PruneReport
core/src/db.h / db.cpp              thin RAII over sqlite3: Db, Stmt, Transaction, vacuumInto
core/src/migrate.h / migrate.cpp    Migration, kMigrations, kLatest, migrate()
core/src/schema_v1.sql              v1 DDL (embedded via a generated header)
core/src/*.cpp                      one file per header + store_*.cpp split by area
core/tests/tst_*.cpp                one QTest class per area, all linked into core-tests via a small main
core/tests/helpers.h                test helpers
.github/workflows/ci.yml
```

### Shared test helpers (`core/tests/helpers.h`, created in Task 2, extended as needed)

- `qint64 t0()`: `QDateTime(QDate(2026,10,15), QTime(8,0), QTimeZone::UTC).toMSecsSinceEpoch()`
- `QDate today()`: `QDate(2026,10,15)`
- `struct Fixture { QTemporaryDir tmp; std::shared_ptr<FixedClock> clock; std::unique_ptr<Store> store; }` and `Fixture fixture()`: data dir at `tmp/data`, clock at `t0()`
- `std::unique_ptr<Store> reopen(Fixture&)`: drops the store, then opens it again on the same dir
- `QString html(QStringList lines)`: `<p>…</p>` per line, with lines HTML-escaped (`<p></p>` for an empty line)
- `QString htmlWithImage(QString text, QString attachmentId)`: `<p>text<img src="noite-att:ID"/></p>`
- `void setMtime(QString path, qint64 ms)`: `QFile::setFileTime(…, QFileDevice::FileModificationTime)`
- `void expectError(ErrorCode code, std::function<void()> f)`: `QFAIL` unless `f` throws `noite::Error` with that code

---

### Task 1: Project skeleton, vendored SQLite with FTS5, build and test scripts

**Files:**
- Create: `CMakeLists.txt`, `third_party/sqlite/{sqlite3.c,sqlite3.h,VERSION,CMakeLists.txt}`, `core/CMakeLists.txt`, `core/include/noite/error.h`, `core/src/error.cpp`, `core/tests/main.cpp`, `core/tests/tst_sqlite.cpp`, `tools/build.ps1`, `tools/run-tests.ps1`

**Interfaces:**
- Produces:
  - `enum class noite::ErrorCode { AlreadyRunning, NewerSchema, MigrationFailed, NotFound, InvalidTransition, InvalidTag, AttachmentTooLarge, InvalidAttachmentId, InvalidSetting, BackupDirMissing, BackupInvalid, Database, Io }`. All values are added now.
  - `class noite::Error : public std::runtime_error { public: Error(ErrorCode, const QString& message); ErrorCode code() const; }`
  - The `sqlite3` static library target and the `noite-core` and `core-tests` targets.
  - `core/tests/main.cpp` runs each registered test class with `QTest::qExec` and returns the total failure count. A file-level registry macro `NOITE_TEST(Class)` adds a class.
  - If the env var `NOITE_TEST_OUT` names a directory, each class gets the arguments `-o <dir>/<ClassName>.txt,txt`. With one shared `-o` file, every class would overwrite the previous one's results.

- [ ] **Step 1: Vendor SQLite.**
  1. Read https://sqlite.org/download.html and find the newest `sqlite-amalgamation-3XXYYZZ.zip` and its SHA3-256.
  2. Download it into a **new empty** `.superpowers/downloads/sqlite-<ver>/` directory (git-ignored).
  3. Verify the hash with `python -I -c "import hashlib,sys;print(hashlib.sha3_256(open(sys.argv[1],'rb').read()).hexdigest())" <zip>` using the user-scope Python 3.13.
  4. Copy only `sqlite3.c` and `sqlite3.h` into `third_party/sqlite/`. Write `VERSION` with the version, the zip name, its SHA3-256 and the download date.

  **A hash mismatch stops the task.**

- [ ] **Step 2: CMake.**
  - **Root:** `project(noite LANGUAGES C CXX)`, C++20, `find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Test)`, `qt_standard_project_setup()`, `enable_testing()`, then `add_subdirectory(third_party/sqlite)` and `add_subdirectory(core)`.
  - **`third_party/sqlite`:** `add_library(sqlite3 STATIC sqlite3.c)` with compile definitions `SQLITE_ENABLE_FTS5 SQLITE_DQS=0 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DEFAULT_WAL_SYNCHRONOUS=1 SQLITE_LIKE_DOESNT_MATCH_BLOBS SQLITE_USE_URI=0` and `PUBLIC` include dir.
  - **`core`:** `noite-core` STATIC with `PUBLIC include/`, linking `sqlite3 Qt6::Core Qt6::Gui`; `core-tests` linking `noite-core Qt6::Test`; `add_test(NAME core COMMAND core-tests)`.

- [ ] **Step 3: Scripts.**
  - `tools/build.ps1 [-Config Debug|Release] [-QtDir …] [-Test]`: the same approach as `spikes/qt-capture/build.ps1` (dev shell, Ninja, build dir `build/<Config>`).
  - `tools/run-tests.ps1 [-Config …] [-QtDir …]`:
    - runs `core-tests.exe` with `QT_QPA_PLATFORM=offscreen`, `QT_PLUGIN_PATH`/`PATH` pointing at the Qt dir, and `NOITE_TEST_OUT=build/<Config>/test-results` (emptied first),
    - reads `$p.Handle` straight after `Start-Process`,
    - prints every result file and a final `TOTAL failures: N` line,
    - exits with the exe's exit code.

- [ ] **Step 4: Write the failing test** `tst_sqlite.cpp`:

```cpp
void fts5IsCompiledIn();   // sqlite3_compileoption_used("ENABLE_FTS5") == 1
void fts5TableWorks();     // :memory: → CREATE VIRTUAL TABLE t USING fts5(x, tokenize='unicode61 remove_diacritics 2');
                           // INSERT 'Reunião amanhã'; SELECT count(*) WHERE t MATCH '"reuniao"*' == 1
void errorCarriesCode();   // noite::Error(ErrorCode::NotFound, "x").code() == ErrorCode::NotFound
```

- [ ] **Step 5: Run to verify it fails**
Run: `powershell -File tools/build.ps1 -Config Debug -Test`
Expected: a build failure (no `noite::Error`), or the tests FAIL.

- [ ] **Step 6: Implement `Error`, then run again**
Run: `powershell -File tools/build.ps1 -Config Debug -Test`
Expected: `TOTAL failures: 0` with the 3 `tst_sqlite` tests passing, and exit code 0.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt third_party core tools .gitignore
git commit -m "feat(core): CMake skeleton, vendored SQLite with FTS5, build and test scripts"
```

### Task 2: Clock and DataDir

**Files:**
- Create: `core/include/noite/clock.h`, `core/src/clock.cpp`, `core/include/noite/data_dir.h`, `core/src/data_dir.cpp`, `core/tests/helpers.h`, `core/tests/tst_data_dir.cpp`

**Interfaces:**
- Produces:
  - `inline constexpr qint64 kDayMs = 86'400'000;`
  - `struct Clock { virtual ~Clock() = default; virtual qint64 nowMs() const = 0; };`, `struct SystemClock final : Clock`, and `class FixedClock final : public Clock` with `explicit FixedClock(qint64 ms)`, `void set(qint64)`, `void advance(qint64)` (backed by an `std::atomic<qint64>`)
  - `class DataDir` (move-only):
    - `static DataDir acquire(const QString& root)`
    - accessors `root()`, `dbPath()`, `attachmentsDir()`, `logsDir()`, `migrationBackupsDir()` (all returning `QString`)
    - `std::optional<SyncProvider> syncWarning() const`
  - `enum class SyncProvider { GoogleDrive, OneDrive, Dropbox };` and `std::optional<SyncProvider> syncProviderFor(const QString& path, const QStringList& oneDriveRoots)`
- Rules:
  - **`acquire`:** creates the 4 directories, then takes a `QLockFile` on `<root>/noite.lock` with `tryLock(0)`; failure throws `AlreadyRunning`. `QLockFile` also reclaims a lock left by a crashed process.
  - **`syncProviderFor`:** compares path components case-insensitively, on both `\` and `/`. `My Drive` or `Google Drive` means GoogleDrive; `Dropbox` means Dropbox; a component-wise prefix of any OneDrive root means OneDrive.
  - **`syncWarning`:** reads the env vars `OneDrive`, `OneDriveConsumer` and `OneDriveCommercial`.

- [ ] **Step 1: Write the failing tests**

```cpp
void acquireCreatesLayout();                   // attachments/logs/migration-backups exist; dbPath == root + "/noite.db"
void secondAcquireFailsUntilReleased();        // expectError(AlreadyRunning, acquire same root); after the first is destroyed, acquire succeeds
void differentRootsCoexist();
void detectsSyncFolders() {
    const QStringList od = {R"(C:\Users\u\OneDrive)"};
    QCOMPARE(syncProviderFor(R"(G:\My Drive\noite\data)", {}), SyncProvider::GoogleDrive);
    QCOMPARE(syncProviderFor(R"(C:\Users\u\Google Drive\noite)", {}), SyncProvider::GoogleDrive);
    QCOMPARE(syncProviderFor(R"(d:\my drive\x)", {}), SyncProvider::GoogleDrive);
    QCOMPARE(syncProviderFor(R"(C:\Users\u\Dropbox\noite)", {}), SyncProvider::Dropbox);
    QCOMPARE(syncProviderFor(R"(c:\users\u\onedrive\noite)", od), SyncProvider::OneDrive);
    QCOMPARE(syncProviderFor(R"(D:\tools\noite\data)", od), std::nullopt);
    QCOMPARE(syncProviderFor(R"(C:\Users\u\OneDriveX\noite)", od), std::nullopt);  // prefix must be whole components
}
void fixedClockAdvances();                     // set/advance/nowMs
```

- [ ] **Step 2: Run, verify it fails.** Run `tools/build.ps1 -Config Debug -Test`. Expected: compile errors.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, verify it passes.** Same command. Expected: all pass.
- [ ] **Step 5: Commit** with `feat(core): clock and locked data dir with sync-folder detection`.

### Task 3: SQLite wrapper, schema v1, migrations, `Store::open`

**Files:**
- Create: `core/src/db.h`, `core/src/db.cpp`, `core/src/migrate.h`, `core/src/migrate.cpp`, `core/src/schema_v1.sql`, `core/include/noite/model.h`, `core/include/noite/store.h`, `core/src/store.cpp`, `core/tests/tst_migrate.cpp`, `core/tests/tst_store_open.cpp`

**Interfaces:**
- Produces (internal, `core/src`):
  - `class Db` (move-only, owns `sqlite3*`):
    - `static Db open(const QString& path)`. It sets `journal_mode=WAL`, `synchronous=NORMAL`, `foreign_keys=ON` and `busy_timeout=5000`.
    - `void exec(const QString& sql)` (multi-statement), `Stmt prepare(const QString& sql)`
    - `int userVersion()`, `void setUserVersion(int)`, `sqlite3* handle()`
  - `class Stmt`: `bind(int, …)` overloads for `qint64`, `const QString&`, null via `std::nullopt` and `std::optional<T>`; `bool step()`; `qint64 int64(int)`; `QString text(int)`; `bool isNull(int)`; `void reset()`.
  - `class Transaction` (RAII): `BEGIN IMMEDIATE` on construction, `commit()`, `ROLLBACK` in the destructor if not committed.
  - `void vacuumInto(Db&, const QString& dest)`: must not be called inside a transaction.
  - Every SQLite error throws `Error(ErrorCode::Database, sqlite3_errmsg)`.
  - `struct Migration { int version; const char* sql; };`, `extern const std::vector<Migration> kMigrations;`, `inline constexpr int kLatest = 1;`
  - `void migrate(Db&, const std::vector<Migration>&, const QString& snapshotDir, qint64 nowMs)`
- Produces (public):
  - `model.h`:
    - `using ScratchId = QString; using BundleId = QString;`
    - `enum class Status { Active, Done, Trashed }`, `enum class Color { Red, Orange, Yellow, Green, Blue, Purple }`, `enum class Tab { Today, All, Completed, Trash }`
    - `QString toString(Status|Color)` and `std::optional<Status|Color> parseStatus/parseColor(const QString&)`, with the lowercase DB strings
  - `class Store` (non-copyable): `static std::unique_ptr<Store> open(DataDir, std::shared_ptr<const Clock>)`, `int schemaVersion() const`, `const DataDir& dataDir() const`
  - Internal accessors for other `store_*.cpp` files and `backup.cpp`: `Db& db()`, `qint64 nowMs() const`
- Schema v1 (`schema_v1.sql`), unchanged from the Rust plan except `content` holds HTML:

```sql
CREATE TABLE bundles (id TEXT PRIMARY KEY, name TEXT NOT NULL, created_at INTEGER NOT NULL);
CREATE TABLE scratches (
  id TEXT PRIMARY KEY,
  content TEXT NOT NULL,          -- Qt rich-text HTML
  plain_text TEXT NOT NULL,
  title TEXT NOT NULL,
  first_image TEXT,
  created_at INTEGER NOT NULL,
  modified_at INTEGER NOT NULL,
  due_date TEXT,                  -- 'YYYY-MM-DD'
  color TEXT CHECK (color IN ('red','orange','yellow','green','blue','purple')),
  bundle_id TEXT REFERENCES bundles(id) ON DELETE SET NULL,
  status TEXT NOT NULL CHECK (status IN ('active','done','trashed')),
  trashed_from TEXT CHECK (trashed_from IN ('active','done')),
  done_at INTEGER,
  trashed_at INTEGER
);
CREATE INDEX scratches_status ON scratches(status);
CREATE TABLE scratch_tags (
  scratch_id TEXT NOT NULL REFERENCES scratches(id) ON DELETE CASCADE,
  tag TEXT NOT NULL,
  source TEXT NOT NULL CHECK (source IN ('inline','manual')),
  PRIMARY KEY (scratch_id, tag, source)
);
CREATE VIRTUAL TABLE scratch_fts USING fts5(id UNINDEXED, body, tokenize = 'unicode61 remove_diacritics 2');
CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
```

  Embed it with `file(READ …)` plus `configure_file` into a generated header, so the SQL stays a real `.sql` file.

- [ ] **Step 1: Write the failing tests.**
  - `tst_migrate.cpp` (calls `migrate()` directly, on temp-file DBs):

```cpp
void freshDbMigratesWithoutSnapshot();   // version 0 → kLatest; snapshotDir empty
void upgradeSnapshotsOldVersionFirst();  // at v1, run {v1, {2,"CREATE TABLE extra(x);"}} → version 2; exactly one
                                         // "noite-v1-*.db" in snapshotDir whose own user_version == 1
void failingMigrationRollsBack();        // at v1, run {v1, {2,"CREATE TABLE ok(x); CREATE TABLE broken("}} →
                                         // MigrationFailed; user_version == 1; table ok absent; one "noite-v1-*.db"
void newerSchemaRefused();               // user_version 99 → NewerSchema
```

  - `tst_store_open.cpp`:

```cpp
void openCreatesLatestSchema();          // schemaVersion() == 1
void dbUsesWal();                        // reopen raw: PRAGMA journal_mode == "wal"
void newerDbRefusedViaStore();           // raw PRAGMA user_version = 99 → Store::open throws NewerSchema
```

- [ ] **Step 2: Run, verify it fails.** Expected: compile errors.
- [ ] **Step 3: Implement `migrate`.**
  1. Read `user_version`. If it's > the max version in the list, throw `NewerSchema`.
  2. If 0 < version < target, call `vacuumInto` to `snapshotDir/noite-v{old}-{nowMs}.db` **before** the transaction.
  3. Run every pending migration plus `PRAGMA user_version = N` in one `Transaction`. On failure, roll back and throw `MigrationFailed`, with the version in the message.
- [ ] **Step 4: Run, verify it passes.** Expected: all pass.
- [ ] **Step 5: Commit** with `feat(core): sqlite wrapper, v1 schema, transactional migrations, Store::open`.

### Task 4: Content derivation (`content.h`)

**Files:**
- Create: `core/include/noite/content.h`, `core/src/content.cpp`, `core/tests/tst_content.cpp`

**Interfaces:**
- Produces (namespace `noite::content`, all pure):
  - `QString plainText(const QString& html)`
  - `QString deriveTitle(const QString& plainText)`
  - `QStringList inlineTags(const QString& plainText)` (sorted, unique)
  - `std::optional<QString> normalizeTag(const QString& raw)`
  - `QStringList attachmentRefs(const QString& html)` (sorted, unique)
  - `std::optional<QString> firstImageRef(const QString& html)`
  - `bool isEmpty(const QString& html)`
  - `QString preview(const QString& plainText)`
  - `bool isValidAttachmentId(const QString& id)`
- Rules:
  - **`plainText`:** `QTextDocumentFragment::fromHtml(html).toPlainText()` with U+FFFC removed, `\u2029`/`\u2028` turned into `\n`, and trailing whitespace trimmed.
  - **`deriveTitle`:** first line that's non-empty after trimming, trimmed and cut to 60 graphemes (`QTextBoundaryFinder::Grapheme`); `""` if there's none.
  - **Tags:**
    - A tag starts at a `#` that's at the start of the text or preceded by whitespace or `(`.
    - The next char must be a Unicode letter; the tag then continues over letters, digits, `_` and `-`.
    - A trailing `-` is dropped, and the result is lowercased.
    - `normalizeTag` applies the same rule to a manual tag (leading `#` optional); anything else returns `nullopt`.
  - **`attachmentRefs`:** every match of `noite-att:([0-9a-f]{64}\.[a-z0-9]{1,8})` in the HTML.
  - **`firstImageRef`:** the first `<img … src="noite-att:ID"` in document order.
  - **`isEmpty`:** the plain text is empty and there are no attachment refs.
  - **`preview`:** the first 200 graphemes.
  - **`isValidAttachmentId`:** `^[0-9a-f]{64}\.[a-z0-9]{1,8}$`.

- [ ] **Step 1: Write the failing tests**

```cpp
void plainTextFromQtHtml();        // html of a QTextDocument with <h1>Title</h1>, <p>a<br/>b</p>, a checkbox list item "x"
                                   // (build it with QTextCursor, export toHtml) → "Title\na\nb\nx"
void titleFirstNonEmptyLine();     // deriveTitle("\n\n  Buy milk  \nmore") == "Buy milk"; deriveTitle("") == ""
void titleTruncatesByGrapheme();   // QString(70, u'é') → 60 é; "👨‍👩‍👧" x61 → x60, no split surrogate/ZWJ
void inlineTagRules();             // "buy milk #home #Work" → {home, work}; "#reunião amanhã" → {reunião};
                                   // "(#todo) #a-b_c-" → {a-b_c, todo};
                                   // "see https://x.com/#section, C# and issue #12, a#b" → {}
void normalizeTagRules();          // "#Urgent"→"urgent"; "urgent"→"urgent"; "two words", "", "#12", "#" → nullopt
void attachmentRefsAndFirstImage();// html with two <img src="noite-att:A.png"> and a file link "noite-att:B.pdf" →
                                   // refs {A.png, B.pdf}; firstImageRef == A.png; html without images → nullopt
                                   // "noite-att://<hex>.png" (old invalid form) is NOT matched
void emptiness();                  // "<p></p>" and "<p>  </p>" empty; image-only NOT empty; "<p>x</p>" NOT empty
void attachmentIdValidation();     // valid 64-hex + .png; "../noite.db", "abc.png", "", "<64hex>.PNG" → invalid
```

- [ ] **Step 2: Run, verify it fails.** **Step 3: Implement.** **Step 4: Run, verify it passes.**
- [ ] **Step 5: Commit** with `feat(core): derive plain text, title, tags and attachment refs from note HTML`.

### Task 5: Scratch create / read / update / discard

**Files:**
- Modify: `core/include/noite/model.h`, `core/include/noite/store.h`
- Create: `core/src/store_scratch.cpp`, `core/tests/tst_scratch.cpp`

**Interfaces:**
- Produces:
  - `struct Scratch { ScratchId id; QString html; QString title; qint64 createdAt{}; qint64 modifiedAt{}; std::optional<QDate> due; std::optional<Color> color; QStringList tags; QStringList manualTags; std::optional<BundleId> bundleId; Status status{}; std::optional<qint64> doneAt; std::optional<qint64> trashedAt; };`. `tags` is the sorted, unique union of inline and manual tags.
  - On `Store`:
    - `Scratch createScratch(const QString& html)`
    - `Scratch updateContent(const ScratchId&, const QString& html)`
    - `Scratch get(const ScratchId&) const`
    - `bool discardIfEmpty(const ScratchId&)`
    - `void setDue(const ScratchId&, std::optional<QDate>)`
    - `void setColor(const ScratchId&, std::optional<Color>)`
    - `void setManualTags(const ScratchId&, const QStringList&)`
  - Internal: `void writeDerived(Db&, const ScratchId&, const QString& html)`. In one place it rewrites `content`, `plain_text`, `title`, `first_image`, the inline `scratch_tags` rows and the `scratch_fts` row (delete + insert). Clone (Task 7) reuses it.
- Ids are `QUuid::createUuidV7().toString(QUuid::WithoutBraces)`. Every mutation runs in one `Transaction` and sets `modified_at = nowMs()`. `discardIfEmpty` hard-deletes the row, its tags and its FTS row, and never touches trash.

- [ ] **Step 1: Write the failing tests**

```cpp
void createDerivesFields();          // html({"Buy milk #home","and eggs #Errands"}) → title "Buy milk #home",
                                     // tags {errands, home}, status Active, createdAt == modifiedAt == t0()
void updateRederivesAndBumpsModified();  // advance 1000; "Call bob #work" → tags {work}, modified t0+1000, created t0
void manualTagsSurviveContentUpdates();  // setManualTags({"#Urgent"}) → tags has "urgent"; update without inline tags → tags == {urgent}
void invalidManualTagRejected();     // setManualTags({"two words"}) → InvalidTag; tags unchanged
void dueAndColorRoundTrip();         // set, get, clear
void discardIfEmptyRules();          // empty → true, get → NotFound; "x" → false; htmlWithImage("", validId) → false
void missingIdIsNotFound();
void persistsAcrossReopen();         // create, reopen(fixture) → get(id).html == original html
```

- [ ] **Steps 2–4:** RED, implement, GREEN (same commands as above).
- [ ] **Step 5: Commit** with `feat(core): scratch create, update, metadata and empty discard`.

### Task 6: Lifecycle

**Files:**
- Create: `core/src/store_lifecycle.cpp`, `core/tests/tst_lifecycle.cpp`
- Modify: `core/src/store_scratch.cpp` (read-only guard)

**Interfaces:**
- Produces on `Store` (each `void f(const ScratchId&)`): `markDone`, `reopen`, `trash`, `restore`.
- Transitions:
  - `markDone`: Active → Done, sets `doneAt`.
  - `reopen`: Done → Active, clears `doneAt`.
  - `trash`: Active or Done → Trashed, sets `trashedAt` and `trashed_from`.
  - `restore`: Trashed → `trashed_from`, clears both, keeps `doneAt`.
  - Anything else throws `InvalidTransition`.
  - `updateContent`, `setDue`, `setColor` and `setManualTags` on a Trashed scratch throw `InvalidTransition`.
- Implement with `UPDATE … WHERE id=? AND status=?`. Zero changed rows means `InvalidTransition` if the id exists, `NotFound` otherwise.

- [ ] **Step 1: Failing tests:**
  - `doneThenReopen`
  - `restoreReturnsPreviousStatus`: active → trash → restore → Active; done → trash → restore → Done with the original `doneAt`
  - `invalidTransitions`: reopen(active), restore(active), trash(trashed), markDone(trashed), updateContent(trashed), setColor(trashed)
- [ ] **Steps 2–4:** RED, implement, GREEN.
- [ ] **Step 5: Commit** with `feat(core): lifecycle transitions with restore-to-previous`.

### Task 7: Bundles and clone

**Files:**
- Create: `core/src/store_bundles.cpp`, `core/tests/tst_bundles.cpp`
- Modify: `model.h`, `store.h`

**Interfaces:**
- Produces:
  - `struct Bundle { BundleId id; QString name; qint64 createdAt{}; };`
  - On `Store`:
    - `BundleId bundleTogether(const ScratchId& target, const ScratchId& dragged)`
    - `void moveToBundle(const ScratchId&, std::optional<BundleId>)`
    - `void renameBundle(const BundleId&, const QString&)`
    - `QList<Bundle> listBundles() const`
    - `Scratch cloneScratch(const ScratchId&)`
  - Internal: `int deleteEmptyBundles(Db&)`. It deletes bundles with no member of any status, and runs after every bundle change and from purge (Task 9).
- Rules:
  - **`bundleTogether`:** joins `target`'s bundle if it has one; otherwise creates a bundle named after `target`'s title, or `"Bundle"` if the title is empty. `target == dragged` throws `InvalidTransition`.
  - **Clone (D20):** copies html, manual tags, colour and bundle. The due date is cleared, status is Active, and `createdAt = modifiedAt = now`. Attachments are shared, not copied.

- [ ] **Step 1: Failing tests:**
  - `dropCreatesBundleNamedAfterTarget`
  - `dropOntoBundledJoinsIt`
  - `dropOntoSelfRejected`
  - `lastMemberLeavingDeletesBundle`
  - `trashedMemberKeepsBundleAlive`
  - `renameBundleWorks`
  - `cloneFollowsD20`: original "Plan #a" plus manual {"b"}, Blue, due 2026-10-20, in a bundle, Done; advance 5000 → clone has a new id, the same html/tags/manualTags/colour/bundle, no due date, status Active, no `doneAt`, `createdAt == t0()+5000`
- [ ] **Steps 2–4:** RED, implement, GREEN.
- [ ] **Step 5: Commit** with `feat(core): bundles via drag semantics and D20 clone`.

### Task 8: Board queries, search, review counts

**Files:**
- Create: `core/src/store_board.cpp`, `core/tests/tst_board.cpp`
- Modify: `model.h`, `store.h`

**Interfaces:**
- Produces:
  - `struct ScratchSummary { ScratchId id; QString title; QString preview; std::optional<QString> firstImage; std::optional<Color> color; QStringList tags; std::optional<QDate> due; std::optional<BundleId> bundleId; Status status{}; qint64 createdAt{}; std::optional<qint64> doneAt; std::optional<qint64> trashedAt; };`
  - `struct ReviewCounts { int overdue{}; int dueToday{}; int open{}; };`
  - On `Store`: `QList<ScratchSummary> list(Tab, QDate today, const QString& search = {}) const` and `ReviewCounts reviewCounts(QDate today) const`
  - Internal: `std::optional<QString> ftsQuery(const QString& input)`. It splits on whitespace and turns each term into `"` + term with `"` doubled + `"*`, joined by spaces. Empty or whitespace-only input → `nullopt` (no text filter).
- Ordering (D9 + roadmap decision 1):
  - **Today:** active scratches with `due ≤ today` or no due date: overdue (due asc, created asc) → due today (created asc) → undated (created asc).
  - **All:** every active scratch, in the Today order, then upcoming (due asc, created asc).
  - **Completed:** done, `doneAt` desc.
  - **Trash:** trashed, `trashedAt` desc.
  - `open` = count of all active scratches. Chips filter in the UI (M3), not here.
- Implementation:
  - One query per tab with a `CASE` sort bucket; `today` is bound as `YYYY-MM-DD`.
  - Search adds `AND id IN (SELECT id FROM scratch_fts WHERE scratch_fts MATCH ?)`.
  - Tags load in one grouped query for the result ids.

- [ ] **Step 1: Failing tests.** Fixture: overdueA (due 10-13), overdueB (10-14), dueToday (10-15), upcoming (10-20), undatedOld (created t0), undatedNew (t0 + 1 h), one done, one trashed.
  - `todayTabOrder`: overdueA, overdueB, dueToday, undatedOld, undatedNew
  - `allTabOrder`: …the same, then upcoming
  - `completedAndTrashNewestFirst`
  - `searchPrefixAndAccentInsensitive`: "mil" → milk; "reuniao" → Reunião; "REUN" → Reunião; a trashed "milk" isn't in All
  - `searchNeverThrowsOnFtsSyntax`: `"`, `-`, `*`, `(`, `AND`, `a OR`, `NEAR(`, `"open`, `#home`, and whitespace-only equals no filter
  - `reviewCountsMatchFixture`: {2, 1, 6}
  - `summaryPreviewAndFirstImage`
  - `ftsQueryEscaping`: `ftsQuery("a \"b") == "\"a\"* \"\"\"b\"*"`; `ftsQuery(" ") == nullopt`
- [ ] **Steps 2–4:** RED, implement, GREEN.
- [ ] **Step 5: Commit** with `feat(core): board tab queries, safe FTS search and review counts`.

### Task 9: Attachments and trash purge

**Files:**
- Create: `core/src/store_attachments.cpp`, `core/tests/tst_attachments.cpp`
- Modify: `model.h`, `store.h`

**Interfaces:**
- Produces:
  - `inline constexpr qint64 kMaxAttachmentBytes = 26'214'400;`, `inline constexpr qint64 kAttachmentGraceMs = kDayMs;`
  - `struct PurgeReport { int scratchesDeleted{}; int attachmentsDeleted{}; };`
  - On `Store`:
    - `QString putAttachment(const QByteArray& bytes, const QString& ext)`: returns the id
    - `QString attachmentPath(const QString& id) const`
    - `PurgeReport purgeTrash(int retentionDays)`
- Rules:
  - **Ids and writing:** id = SHA-256 hex + `.` + ext. The ext is lowercased; if it isn't 1–8 ASCII alphanumerics it becomes `bin`. Write through `QSaveFile` (atomic). If the file already exists, skip the write (dedupe).
  - **Size:** over the limit throws `AttachmentTooLarge`.
  - **`attachmentPath`:** rejects anything that fails `content::isValidAttachmentId` with `InvalidAttachmentId`.
  - **Purge order:**
    1. Check `retentionDays` is in 1–365, else `InvalidSetting`.
    2. Delete trashed scratches with `trashedAt <= now − days·kDayMs`, with their tags and FTS rows.
    3. `deleteEmptyBundles`.
    4. Collect `attachmentRefs` over **all** remaining scratches.
    5. Delete unreferenced valid-id files, and any leftover temp files, but only those whose mtime is `<= now − kAttachmentGraceMs`.

- [ ] **Step 1: Failing tests:**
  - `putDedupesAndNamesByHash`
  - `badExtensionBecomesBin` (`"../evil"`)
  - `rejectsOversized`: 26 214 401 bytes
  - `attachmentPathRejectsTraversal`
  - `retentionBoundaries`: 29 d kept, 30 d deleted
  - `shortenedRetentionAppliesToExistingTrash`
  - `retentionOutOfRangeRejected`: 0, 366
  - `doneIsNeverPurged`: 400 d
  - `cloneKeepsSharedImageAfterOriginalPurged`: spec §9's first test; mtime t0 − 2 d
  - `unreferencedOldAttachmentDeleted`
  - `unreferencedFreshAttachmentSurvives`: mtime t0 − 1 h
  - `staleTempFilesCleaned`
  - `purgeDeletesBundleLeftEmpty`
- [ ] **Steps 2–4:** RED, implement, GREEN.
- [ ] **Step 5: Commit** with `feat(core): content-addressed attachments and mark-and-sweep trash purge`.

### Task 10: Settings

**Files:**
- Create: `core/include/noite/settings.h`, `core/src/settings.cpp`, `core/tests/tst_settings.cpp`
- Modify: `store.h`

**Interfaces:**
- Produces:
  - `enum class BackupFrequency { Daily, Weekly };`
  - `struct Settings { QString captureHotkey = "Ctrl+Alt+J"; QString boardHotkey = "Ctrl+Alt+B"; int trashRetentionDays = 30; QTime dayStart{6, 0}; std::optional<QString> backupDir; BackupFrequency backupFrequency = BackupFrequency::Daily; int backupKeep = 14; bool startWithWindows = true; bool setupCompleted = false; };`
  - `void validate(const Settings&)`: throws `InvalidSetting` for retention outside 1–365 or `backupKeep < 1`
  - On `Store`: `Settings settings() const` and `void saveSettings(const Settings&)`. They're stored as a `QJsonObject` under key `"settings"`; missing keys take the defaults; `saveSettings` validates first.

- [ ] **Step 1: Failing tests:**
  - `defaultsMatchSpec`
  - `roundTrip`, across a reopen
  - `validation`: 0, 366, `backupKeep` 0; the stored value is unchanged afterwards
  - `partialJsonFillsDefaults`: raw insert `{"trashRetentionDays":7}`
- [ ] **Steps 2–4:** RED, implement, GREEN.
- [ ] **Step 5: Commit** with `feat(core): typed settings with spec defaults and validation`.

### Task 11: Backup snapshots, prune, verify

**Files:**
- Create: `core/include/noite/backup.h`, `core/src/backup.cpp`, `core/tests/tst_backup.cpp`

**Interfaces:**
- Produces (namespace `noite::backup`):
  - `QString snapshot(Store&, const QString& dest)`
  - `PruneReport prune(const QString& dest, int keep)`
  - `void verify(const QString& snapshotDb)`
  - `struct PruneReport { int snapshotsDeleted{}; int attachmentsDeleted{}; };`
- Layout (`dest` must already exist and is never created):

```
<dest>\noite-backups\snapshots\noite-YYYYMMDDTHHMMSSzzzZ.db
<dest>\noite-backups\attachments\<id>
```

- **Snapshot steps:**
  1. If `dest` doesn't exist, throw `BackupDirMissing`.
  2. Create the subfolders.
  3. `vacuumInto` to `<name>.db.tmp`.
  4. Copy each attachment the snapshot references that's missing from the backup (temp + rename).
  5. Rename the `.tmp` to `.db` **last**, so a visible `.db` implies its attachments are present.

  The name comes from `store.nowMs()` in UTC.
- **Prune:** keep the `keep` newest `.db` files by name, delete the rest and every `*.tmp`, then delete backup attachments no retained snapshot references. Read each snapshot read-only (`SQLITE_OPEN_READONLY`) with `SELECT content FROM scratches`.
- **Verify:** `PRAGMA integrity_check` must return `ok`, and every referenced attachment must exist in the sibling `attachments\`; otherwise throw `BackupInvalid`.

- [ ] **Step 1: Failing tests:**
  - `snapshotLayoutAndContents`: the name is `noite-20261015T080000000Z.db`; verify passes
  - `missingDestIsAnError`: and the dest is not created
  - `snapshotRestoresIntoFreshStore`: copy the snapshot + attachments into a new data dir → `Store::open` → `get` matches and `attachmentPath` exists
  - `verifyDetectsMissingAttachment`
  - `pruneKeepsNewest`: 4 snapshots, keep 2
  - `pruneSweepsUnreferencedBackupAttachments`
  - `pruneRemovesStrayTmp`
- [ ] **Steps 2–4:** RED, implement, GREEN.
- [ ] **Step 5: Commit** with `feat(core): consistent backup snapshots with prune and verify`.

### Task 12: Windows CI

**Files:**
- Create: `.github/workflows/ci.yml`

- [ ] **Step 1: Write the workflow.** On `push` and `pull_request`, one job on `windows-latest`:
  1. `actions/checkout@v4`
  2. `jurplel/install-qt-action@v4` with `version: 6.10.3`, `arch: win64_msvc2022_64`, `cache: true`
  3. `ilammy/msvc-dev-cmd@v1`
  4. `cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release`, then `cmake --build build/Release`
  5. `powershell -File tools/run-tests.ps1 -Config Release -QtDir $env:QT_ROOT_DIR`
  6. Upload `build/Release/test-results/` as an artifact.

  `QT_QPA_FONTDIR` is set to `C:\Windows\Fonts`.
- [ ] **Step 2: Commit** with `ci: build noite-core and run its tests on Windows`.
- [ ] **Step 3: Push the branch and check the run.** Watch it at https://github.com/sfadiga/noite/actions. Expected: green. If the user's machine and CI disagree, fix CI; don't skip tests.
