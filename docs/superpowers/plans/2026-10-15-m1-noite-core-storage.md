# M1: `noite-core` Storage Library Implementation Plan

> **SUPERSEDED (2026-10-16):** the stack moved to Qt 6 Widgets + C++ (see the [roadmap](2026-10-15-noite-roadmap.md)). Don't execute this Rust plan. Its schema, rules, interfaces and test cases are the input to the C++ rewrite of M1.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Rust library crate that owns all of noite's persistent state (scratches, lifecycle, bundles, search, attachments, trash purge, settings, backups), tested end-to-end with `cargo test` and no UI.

**Architecture:** `crates/noite-core` is a plain Rust library with no Tauri dependency. A `Store` wraps one SQLite connection (bundled SQLite with FTS5) plus a locked `DataDir`. Its write methods take `&mut self`, so there's exactly one writer (D14). Time comes from an injected `Clock` so retention and ordering can be tested deterministically. Note content is opaque ProseMirror JSON; `doc` derives plain text, title, inline tags and attachment references from it. The Tauri app (M2) will wrap `Store` in a `Mutex` and expose its methods as commands.

**Tech Stack:** Rust stable ≥ 1.89, `rusqlite` (feature `bundled`), `serde` + `serde_json`, `chrono` (feature `serde`), `uuid` (feature `v7`), `blake3`, `thiserror`, `unicode-segmentation`; dev: `tempfile`.

**Spec:** [`docs/spec.md`](../../spec.md) — §3 portability & storage, §4 data model, §6 board sort/search, §9 testing. Planning decisions not in the spec are listed in [the roadmap](2026-10-15-noite-roadmap.md#decisions-made-while-planning-not-in-the-spec-confirm-on-review).

## Global Constraints

- No network code or networking dependencies (D13).
- All state lives under the data dir: `noite.db`, `attachments\`, `logs\`, `migration-backups\`, `noite.lock` (D4).
- One `Store` per data dir. A second `DataDir::acquire` on the same root fails with `Error::AlreadyRunning` (D27).
- Note content is stored as ProseMirror JSON. Markdown is export-only and not in M1 (D7).
- Title = first non-empty line, trimmed, **truncated to 60 grapheme clusters** (D10).
- Colours: exactly `red`, `orange`, `yellow`, `green`, `blue`, `purple` (D10: 6 swatches).
- Trash retention: **1–365 days, default 30**. Deletion happens when `trashed_at <= now − days × 86 400 000 ms` (D11).
- Done scratches are never deleted automatically (D11).
- Attachment size limit **25 MiB = 26 214 400 bytes**. Attachment id = `<64 lowercase hex blake3>.<ext>` (D13, D21).
- An attachment is referenced by any JSON node with a string `attrs.attachmentId` (crate-wide convention, M2 editor nodes must set it).
- Purge never deletes an attachment file whose mtime is younger than **24 h** (roadmap decision 4).
- Backups: consistent `VACUUM INTO` snapshots, default keep **14**; the live DB is never copied raw (D13).
- Migrations: snapshot before migrating; all pending migrations run in one transaction; a newer schema is refused (D26).
- Every test uses `tempfile` dirs; no test touches a real user folder.

## Review Focus

1. **Unicode text** (pt-BR accents, emoji with ZWJ): title truncation must not panic or split a character, `#reunião` must be a tag, and searching `reuniao` must find `Reunião`. Tests in Tasks 3 and 7.
2. **Search box input with FTS5 syntax characters** (`"`, `-`, `*`, `(`, `AND`, `NEAR(`) must return results or an empty list, never an error. Test in Task 7.
3. **`#` that isn't a tag:** URL fragments (`https://x.com/#a`), `C#`, `issue #12`. Test in Task 3.
4. **Image pasted while autosave is pending:** an unreferenced attachment younger than 24 h survives a purge. Test in Task 8.
5. **Portable folder placed inside a sync folder** (Google Drive / OneDrive / Dropbox): detected rather than silently risking corruption. Test in Task 1.

---

## File structure

```
Cargo.toml                         workspace root (members: crates/noite-core; exclude: spikes)
crates/noite-core/
  Cargo.toml
  src/lib.rs                       module tree; re-exports every pub type, Store, clock and data-dir items at the crate root (`doc` and `backup` stay modules)
  src/error.rs                     Error, Result
  src/clock.rs                     Clock, SystemClock, FixedClock, DAY_MS
  src/data_dir.rs                  DataDir, SyncProvider, sync_provider_for
  src/db/mod.rs                    open_connection (pragmas), vacuum_into
  src/db/migrate.rs                Migration, MIGRATIONS, migrate (+ unit tests)
  src/db/schema_v1.sql             v1 DDL
  src/doc.rs                       pure ProseMirror JSON helpers
  src/model.rs                     ids, enums, Scratch, ScratchSummary, Bundle, ReviewCounts
  src/store.rs                     Store::open, scratch CRUD, clone
  src/lifecycle.rs                 done / reopen / trash / restore
  src/bundles.rs                   bundle operations
  src/board.rs                     list, review_counts, fts_query (+ unit tests)
  src/attachments.rs               put, path, purge
  src/settings.rs                  Settings, BackupFrequency
  src/backup.rs                    snapshot, prune, verify
  tests/common/mod.rs              shared helpers
  tests/{data_dir,store_open,doc,scratch,lifecycle,bundles,board,attachments,settings,backup}.rs
.github/workflows/ci.yml
```

### Shared test helpers (`tests/common/mod.rs`, created in Task 2, extended as needed)

- `pub fn t0() -> i64`: `Utc.with_ymd_and_hms(2026, 10, 15, 8, 0, 0).unwrap().timestamp_millis()`
- `pub fn today() -> NaiveDate`: 2026-10-15
- `pub fn store() -> (TempDir, Store, Arc<FixedClock>)`: data dir at `tmp/data`, clock at `t0()`
- `pub fn reopen(tmp: &TempDir, clock: Arc<FixedClock>) -> Store`
- `pub fn doc(lines: &[&str]) -> Value`: `{"type":"doc","content":[paragraph per line]}`; an empty line becomes an empty paragraph
- `pub fn doc_with_image(text: &str, attachment_id: &str) -> Value`: one paragraph followed by `{"type":"image","attrs":{"attachmentId": id}}`
- `pub fn set_mtime(path: &Path, ms: i64)`: via `File::options().write(true).open(path)?.set_modified(...)`

---

### Task 1: Workspace, crate skeleton, clock, errors, data dir

**Files:**
- Create: `Cargo.toml`, `crates/noite-core/Cargo.toml`, `src/lib.rs`, `src/error.rs`, `src/clock.rs`, `src/data_dir.rs`
- Test: `crates/noite-core/tests/data_dir.rs`

**Interfaces:**
- Produces:
  - `pub enum Error`: variants `AlreadyRunning(PathBuf)`, `NewerSchema { found: u32, supported: u32 }`, `MigrationFailed { version: u32, source: rusqlite::Error }`, `NotFound(String)`, `InvalidTransition(&'static str)`, `InvalidTag(String)`, `AttachmentTooLarge { size: u64, limit: u64 }`, `InvalidAttachmentId(String)`, `InvalidSetting(&'static str)`, `BackupDirMissing(PathBuf)`, `BackupInvalid(String)`, `Db(rusqlite::Error)`, `Io(std::io::Error)`, `Json(serde_json::Error)`. Use `thiserror`; the last three are `#[from]`. Add each variant now so later tasks don't edit this file.
  - `pub type Result<T> = std::result::Result<T, Error>`
  - `pub const DAY_MS: i64 = 86_400_000`
  - `pub trait Clock: Send + Sync { fn now_ms(&self) -> i64; }`, `pub struct SystemClock`, and `pub struct FixedClock` with `new(ms: i64)`, `set(&self, ms: i64)`, `advance(&self, ms: i64)` (interior `AtomicI64`)
  - `pub struct DataDir` with `acquire(root: impl Into<PathBuf>) -> Result<DataDir>`, `root()`, `db_path()`, `attachments_dir()`, `logs_dir()`, `migration_backups_dir()` (all `-> PathBuf`, except `root() -> &Path`), and `sync_warning(&self) -> Option<SyncProvider>`
  - `pub enum SyncProvider { GoogleDrive, OneDrive, Dropbox }` and `pub fn sync_provider_for(path: &Path, onedrive_roots: &[PathBuf]) -> Option<SyncProvider>`

- [ ] **Step 1: Workspace + crate.** Run `cargo new --lib crates/noite-core` first, then write the root `Cargo.toml`: `[workspace] resolver = "2"`, `members = ["crates/noite-core"]`, `exclude = ["spikes"]`. Then add deps: `cargo add -p noite-core rusqlite --features bundled`, `cargo add -p noite-core serde --features derive`, `cargo add -p noite-core serde_json thiserror blake3 unicode-segmentation`, `cargo add -p noite-core chrono --features serde`, `cargo add -p noite-core uuid --features v7`, `cargo add -p noite-core --dev tempfile`.

- [ ] **Step 2: Write the failing tests** in `tests/data_dir.rs`:

```rust
#[test] fn acquire_creates_layout() {
    let tmp = tempfile::tempdir().unwrap();
    let d = DataDir::acquire(tmp.path().join("data")).unwrap();
    assert!(d.attachments_dir().is_dir());
    assert!(d.logs_dir().is_dir());
    assert!(d.migration_backups_dir().is_dir());
    assert_eq!(d.db_path(), tmp.path().join("data").join("noite.db"));
}
#[test] fn second_acquire_on_same_root_fails_until_dropped() {
    let tmp = tempfile::tempdir().unwrap();
    let a = DataDir::acquire(tmp.path()).unwrap();
    assert!(matches!(DataDir::acquire(tmp.path()), Err(Error::AlreadyRunning(_))));
    drop(a);
    assert!(DataDir::acquire(tmp.path()).is_ok());
}
#[test] fn different_roots_coexist() { /* acquire tmp/a and tmp/b, both Ok */ }
#[test] fn detects_sync_folders() {
    let od = [PathBuf::from(r"C:\Users\u\OneDrive")];
    assert_eq!(sync_provider_for(Path::new(r"G:\My Drive\noite\data"), &[]), Some(SyncProvider::GoogleDrive));
    assert_eq!(sync_provider_for(Path::new(r"C:\Users\u\Google Drive\noite"), &[]), Some(SyncProvider::GoogleDrive));
    assert_eq!(sync_provider_for(Path::new(r"d:\my drive\x"), &[]), Some(SyncProvider::GoogleDrive));
    assert_eq!(sync_provider_for(Path::new(r"C:\Users\u\Dropbox\noite"), &[]), Some(SyncProvider::Dropbox));
    assert_eq!(sync_provider_for(Path::new(r"c:\users\u\onedrive\noite"), &od), Some(SyncProvider::OneDrive));
    assert_eq!(sync_provider_for(Path::new(r"D:\tools\noite\data"), &od), None);
}
```

- [ ] **Step 3: Run to verify they fail**
Run: `cargo test -p noite-core --test data_dir`
Expected: compile errors (`DataDir` and friends not defined).

- [ ] **Step 4: Implement.**
  - `DataDir::acquire` creates the four directories, opens `noite.lock` (create, write), and calls `std::fs::File::try_lock()`. `TryLockError::WouldBlock` maps to `AlreadyRunning(root)`. The `File` is kept in the struct so the lock lives as long as the `DataDir`.
  - `sync_provider_for` compares path components case-insensitively: a component equal to `My Drive` or `Google Drive` means GoogleDrive, `Dropbox` means Dropbox, and a path starting with any OneDrive root (component-wise, case-insensitive) means OneDrive.
  - `sync_warning` reads the env vars `OneDrive`, `OneDriveConsumer` and `OneDriveCommercial` as the roots.

- [ ] **Step 5: Run to verify they pass**
Run: `cargo test -p noite-core --test data_dir`
Expected: 4 passed.

- [ ] **Step 6: Commit**

```bash
git add Cargo.toml Cargo.lock crates/noite-core
git commit -m "feat(core): add crate skeleton, clock, errors and locked data dir"
```

### Task 2: Schema v1, migrations, `Store::open`

**Files:**
- Create: `src/db/mod.rs`, `src/db/migrate.rs`, `src/db/schema_v1.sql`, `src/store.rs`, `src/model.rs`
- Create: `tests/common/mod.rs`
- Test: `tests/store_open.rs`; unit tests in `src/db/migrate.rs`

**Interfaces:**
- Consumes: `DataDir`, `Clock`, `Error` (Task 1).
- Produces:
  - `pub struct Store` with `open(data: DataDir, clock: Arc<dyn Clock>) -> Result<Store>`, `schema_version(&self) -> Result<u32>`, `data_dir(&self) -> &DataDir`
  - `pub(crate)` accessors `conn(&self) -> &Connection`, `conn_mut(&mut self) -> &mut Connection`, `now_ms(&self) -> i64`
  - `pub(crate) fn open_connection(path: &Path) -> Result<Connection>`, which sets `journal_mode=WAL`, `synchronous=NORMAL`, `foreign_keys=ON`, `busy_timeout=5000`
  - `pub(crate) fn vacuum_into(conn: &Connection, dest: &Path) -> Result<()>`, which runs `VACUUM INTO ?1`. It must not be called inside a transaction.
  - `pub(crate) struct Migration { pub version: u32, pub sql: &'static str }`, `pub(crate) const MIGRATIONS: &[Migration]` (v1 = `include_str!("schema_v1.sql")`), `pub(crate) const LATEST: u32 = 1`
  - `pub(crate) fn migrate(conn: &mut Connection, migrations: &[Migration], snapshot_dir: &Path, now_ms: i64) -> Result<()>`
  - `model.rs`: `ScratchId(pub String)`, `BundleId(pub String)` (Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize, Display). Enums `Status { Active, Done, Trashed }`, `Color { Red, Orange, Yellow, Green, Blue, Purple }`, `Tab { Today, All, Completed, Trash }`, all `#[serde(rename_all = "lowercase")]` with `as_str()`/`FromStr` matching the DB strings. The structs come in later tasks.

- [ ] **Step 1: Write `schema_v1.sql`.** This is the decided schema:

```sql
CREATE TABLE bundles (id TEXT PRIMARY KEY, name TEXT NOT NULL, created_at INTEGER NOT NULL);
CREATE TABLE scratches (
  id TEXT PRIMARY KEY,
  content TEXT NOT NULL,
  plain_text TEXT NOT NULL,
  title TEXT NOT NULL,
  first_image TEXT,
  created_at INTEGER NOT NULL,
  modified_at INTEGER NOT NULL,
  due_date TEXT,
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

- [ ] **Step 2: Write the failing unit tests** in `src/db/migrate.rs` (in-memory or temp-file connections):

```rust
#[test] fn fresh_db_migrates_without_snapshot()        // version 0 → LATEST; snapshot_dir stays empty
#[test] fn upgrade_snapshots_old_version_first()        // at v1, run [v1, v2 "CREATE TABLE extra(x);"] → Ok;
                                                         // user_version == 2; snapshot_dir has exactly one
                                                         // "noite-v1-*.db" whose own user_version == 1
#[test] fn failing_migration_rolls_back() {
    // at v1, run [v1, Migration { version: 2, sql: "CREATE TABLE ok(x); CREATE TABLE broken(" }]
    // assert matches!(err, Error::MigrationFailed { version: 2, .. })
    // assert user_version == 1 and table `ok` does not exist
    // assert one "noite-v1-*.db" snapshot exists
}
#[test] fn newer_schema_is_refused()                    // user_version 99 → Err(NewerSchema { found: 99, supported: LATEST })
```

- [ ] **Step 3: Write the failing integration tests** in `tests/store_open.rs` (and create `tests/common/mod.rs` with the helpers listed above):

```rust
#[test] fn open_creates_latest_schema()   // store().1.schema_version() == 1
#[test] fn db_uses_wal()                  // drop store; raw rusqlite on db_path: PRAGMA journal_mode == "wal"
#[test] fn newer_db_refused_via_store()   // drop store; raw: PRAGMA user_version = 99; Store::open → Err(NewerSchema{found:99,supported:1})
```

- [ ] **Step 4: Run to verify they fail**
Run: `cargo test -p noite-core`
Expected: compile errors for `migrate`, `Store`.

- [ ] **Step 5: Implement `migrate`.**
  1. Read `user_version`. If it's > the max version in `migrations`, return `NewerSchema`.
  2. If it's 0 < version < target, call `vacuum_into` to `snapshot_dir/noite-v{old}-{now_ms}.db` **before** opening a transaction.
  3. Run every pending migration's SQL plus `PRAGMA user_version = N` inside one transaction. On error, roll back and return `MigrationFailed { version }`.

  `Store::open` = `open_connection(data.db_path())` → `migrate(.., MIGRATIONS, data.migration_backups_dir(), clock.now_ms())`.

- [ ] **Step 6: Run to verify they pass**
Run: `cargo test -p noite-core`
Expected: all pass (7 new tests).

- [ ] **Step 7: Commit**

```bash
git add crates/noite-core
git commit -m "feat(core): add v1 schema, transactional migrations and Store::open"
```

### Task 3: Document helpers (`doc.rs`)

**Files:**
- Create: `src/doc.rs`
- Test: `tests/doc.rs`

**Interfaces:**
- Produces (all `pub`, pure):
  - `plain_text(doc: &Value) -> String`
  - `derive_title(plain_text: &str) -> String`
  - `inline_tags(plain_text: &str) -> BTreeSet<String>`
  - `normalize_tag(raw: &str) -> Option<String>`
  - `attachment_refs(doc: &Value) -> BTreeSet<String>`
  - `first_image_ref(doc: &Value) -> Option<String>`
  - `is_empty(doc: &Value) -> bool`
  - `preview(plain_text: &str) -> String`, which returns the first 200 graphemes

Rules:
- **`plain_text`:** each `paragraph`, `heading` or `codeBlock` node produces one line made of its descendant `text` nodes; `hardBreak` → `\n`. Lines are joined with `\n` in document order, and trailing whitespace is trimmed.
- **`derive_title`:** first line that isn't empty after trimming, trimmed and cut to 60 graphemes; `""` if there's none.
- **Tags:** a tag starts at `#` that is at the start of the text or preceded by whitespace or `(`. The next char must be alphabetic (Unicode). The tag continues over alphanumerics, `_` and `-`, a trailing `-` is dropped, and the result is lowercased. `normalize_tag` applies the same rule to a manual tag; a leading `#` is optional and anything else invalid returns `None`.
- **`is_empty`:** true when the trimmed plain text is empty **and** there are no attachment refs.

- [ ] **Step 1: Write the failing tests**

```rust
#[test] fn plain_text_joins_blocks() {
    let d = json!({"type":"doc","content":[
        {"type":"heading","attrs":{"level":1},"content":[{"type":"text","text":"Title"}]},
        {"type":"paragraph","content":[{"type":"text","text":"a"},{"type":"hardBreak"},{"type":"text","text":"b"}]},
        {"type":"taskList","content":[{"type":"taskItem","attrs":{"checked":false},
            "content":[{"type":"paragraph","content":[{"type":"text","text":"x"}]}]}]}]});
    assert_eq!(doc::plain_text(&d), "Title\na\nb\nx");
}
#[test] fn title_is_first_nonempty_line_trimmed()  // derive_title("\n\n  Buy milk  \nmore") == "Buy milk"; derive_title("") == ""
#[test] fn title_truncates_by_grapheme() {
    assert_eq!(doc::derive_title(&"é".repeat(70)), "é".repeat(60));
    let fam = "👨‍👩‍👧";
    assert_eq!(doc::derive_title(&fam.repeat(61)), fam.repeat(60));
}
#[test] fn inline_tags_rules() {
    assert_eq!(tags("buy milk #home #Work"), ["home", "work"]);
    assert_eq!(tags("#reunião amanhã"), ["reunião"]);
    assert_eq!(tags("(#todo) #a-b_c-"), ["a-b_c", "todo"]);
    assert!(tags("see https://x.com/#section, C# and issue #12, a#b").is_empty());
}
#[test] fn normalize_tag_rules()  // "#Urgent"→Some("urgent"); "urgent"→Some; "two words"/""/"#12"/"#"→None
#[test] fn attachment_refs_and_first_image() {
    // nested image + fileChip nodes with attrs.attachmentId → refs has both;
    // first_image_ref returns the image's id; doc without images → None
}
#[test] fn emptiness()  // doc(&[""]) empty; doc(&["  "]) empty; doc_with_image("", id) NOT empty; doc(&["x"]) NOT empty
```

(`tags(s)` = a local helper that collects `inline_tags(s)` into a `Vec<String>`.)

- [ ] **Step 2: Run to verify they fail**
Run: `cargo test -p noite-core --test doc`
Expected: compile errors (`doc` module missing).

- [ ] **Step 3: Implement `doc.rs`** using `unicode_segmentation::UnicodeSegmentation::graphemes(true)` for truncation and preview.

- [ ] **Step 4: Run to verify they pass**
Run: `cargo test -p noite-core --test doc`
Expected: 7 passed.

- [ ] **Step 5: Commit**

```bash
git add crates/noite-core
git commit -m "feat(core): derive plain text, title, tags and attachment refs from documents"
```

### Task 4: Scratch create / read / update / discard

**Files:**
- Modify: `src/model.rs`, `src/store.rs`
- Test: `tests/scratch.rs`

**Interfaces:**
- Consumes: `doc::*` (Task 3), `Store` (Task 2).
- Produces:
  - `pub struct Scratch { pub id: ScratchId, pub content: Value, pub title: String, pub created_at: i64, pub modified_at: i64, pub due_date: Option<NaiveDate>, pub color: Option<Color>, pub tags: Vec<String>, pub manual_tags: Vec<String>, pub bundle_id: Option<BundleId>, pub status: Status, pub done_at: Option<i64>, pub trashed_at: Option<i64> }`. `tags` = sorted, de-duplicated union of inline and manual tags; `manual_tags` is sorted.
  - On `Store`:
    - `create_scratch(&mut self, content: &Value) -> Result<Scratch>`
    - `update_content(&mut self, id: &ScratchId, content: &Value) -> Result<Scratch>`
    - `get(&self, id: &ScratchId) -> Result<Scratch>`
    - `discard_if_empty(&mut self, id: &ScratchId) -> Result<bool>`
    - `set_due(&mut self, id: &ScratchId, due: Option<NaiveDate>) -> Result<()>`
    - `set_color(&mut self, id: &ScratchId, color: Option<Color>) -> Result<()>`
    - `set_manual_tags(&mut self, id: &ScratchId, tags: &[String]) -> Result<()>`
  - `pub(crate) fn write_derived(tx: &Transaction, id: &ScratchId, content: &Value) -> Result<()>`. In one place it rewrites `content`, `plain_text`, `title`, `first_image`, the inline rows of `scratch_tags`, and the `scratch_fts` row (delete + insert). Clone (Task 6) reuses it.

- [ ] **Step 1: Write the failing tests**

```rust
#[test] fn create_derives_fields() {
    let (_t, mut s, _c) = store();
    let x = s.create_scratch(&doc(&["Buy milk #home", "and eggs #Errands"])).unwrap();
    assert_eq!(x.title, "Buy milk #home");
    assert_eq!(x.tags, ["errands", "home"]);
    assert_eq!((x.status, x.created_at, x.modified_at), (Status::Active, t0(), t0()));
}
#[test] fn update_rederives_and_bumps_modified()   // advance 1000; update to "Call bob #work" → title, tags ["work"], modified t0+1000, created t0
#[test] fn manual_tags_survive_content_updates()   // set_manual_tags(["#Urgent"]) → tags has "urgent"; update with no inline tags → tags == ["urgent"], manual_tags == ["urgent"]
#[test] fn invalid_manual_tag_rejected()           // set_manual_tags(["two words"]) → Err(InvalidTag(_)); tags unchanged
#[test] fn due_and_color_round_trip()              // set_due(2026-10-20), set_color(Blue) → get reflects; set None clears
#[test] fn discard_if_empty_rules() {
    // empty → true and get → Err(NotFound); "x" → false and still present;
    // doc_with_image("", "<64 hex>.png") → false
}
#[test] fn missing_id_is_not_found()               // get(&ScratchId("nope".into())) → Err(NotFound(_))
#[test] fn persists_across_reopen()                // create, drop store, reopen(&tmp, clock) → get == original
```

- [ ] **Step 2: Run to verify they fail**
Run: `cargo test -p noite-core --test scratch`
Expected: compile errors (`create_scratch` etc. missing).

- [ ] **Step 3: Implement.** Ids are `Uuid::now_v7().to_string()`. Every mutation runs in one transaction and sets `modified_at = now_ms()`. `discard_if_empty` does a hard delete of the row, its tags and its FTS row, and never touches trash.

- [ ] **Step 4: Run to verify they pass**
Run: `cargo test -p noite-core --test scratch`
Expected: 8 passed.

- [ ] **Step 5: Commit**

```bash
git add crates/noite-core
git commit -m "feat(core): scratch create, update, metadata and empty discard"
```

### Task 5: Lifecycle: done, reopen, trash, restore

**Files:**
- Create: `src/lifecycle.rs`
- Modify: `src/store.rs` (read-only guard on trashed scratches)
- Test: `tests/lifecycle.rs`

**Interfaces:**
- Produces on `Store` (each `(&mut self, id: &ScratchId) -> Result<()>`): `mark_done`, `reopen`, `trash`, `restore`.
- Transitions:
  - `mark_done`: Active → Done, sets `done_at`.
  - `reopen`: Done → Active, clears `done_at`.
  - `trash`: Active or Done → Trashed, sets `trashed_at` and `trashed_from`.
  - `restore`: Trashed → `trashed_from`, clears `trashed_at` and `trashed_from`, and keeps `done_at`.
  - Anything else returns `InvalidTransition`. `update_content`, `set_due`, `set_color` and `set_manual_tags` on a Trashed scratch also return `InvalidTransition("trashed scratches are read-only")` (add that guard to Task 4's methods).

- [ ] **Step 1: Write the failing tests**

```rust
#[test] fn done_then_reopen()                       // done_at == Some(t0); reopen → Active, done_at None
#[test] fn restore_returns_previous_status() {
    // active → trash → restore → Active
    // active → done → trash → restore → Done with original done_at
}
#[test] fn invalid_transitions() {
    // reopen(active), restore(active), trash(trashed), mark_done(trashed),
    // update_content(trashed), set_color(trashed) → all Err(InvalidTransition(_))
}
```

- [ ] **Step 2: Run to verify they fail**
Run: `cargo test -p noite-core --test lifecycle`
Expected: compile errors.

- [ ] **Step 3: Implement** using a single `UPDATE … WHERE id = ? AND status = ?` per transition. Zero rows updated means: if the id exists, return `InvalidTransition`; otherwise `NotFound`.

- [ ] **Step 4: Run to verify they pass**
Run: `cargo test -p noite-core --test lifecycle`
Expected: 3 passed (and `--test scratch` still passes).

- [ ] **Step 5: Commit**

```bash
git add crates/noite-core
git commit -m "feat(core): scratch lifecycle transitions with restore-to-previous"
```

### Task 6: Bundles and clone

**Files:**
- Create: `src/bundles.rs`
- Modify: `src/model.rs`, `src/store.rs`
- Test: `tests/bundles.rs`

**Interfaces:**
- Produces:
  - `pub struct Bundle { pub id: BundleId, pub name: String, pub created_at: i64 }`
  - On `Store`:
    - `bundle_together(&mut self, target: &ScratchId, dragged: &ScratchId) -> Result<BundleId>`
    - `move_to_bundle(&mut self, id: &ScratchId, bundle: Option<&BundleId>) -> Result<()>`
    - `rename_bundle(&mut self, id: &BundleId, name: &str) -> Result<()>`
    - `list_bundles(&self) -> Result<Vec<Bundle>>`
    - `clone_scratch(&mut self, id: &ScratchId) -> Result<Scratch>`
  - `pub(crate) fn delete_empty_bundles(tx: &Transaction) -> Result<usize>`. It deletes bundles with no member of any status, and is called after every bundle change and by purge (Task 8).
- Rules:
  - `bundle_together`: if `target` is already in a bundle, `dragged` joins it. Otherwise a new bundle is created, named after `target`'s title, or `"Bundle"` if the title is empty.
  - `target == dragged` returns `InvalidTransition`.
  - Clone (D20) copies content, manual tags, colour and bundle. Its due date is `None`, its status Active, and `created_at = modified_at = now`.

- [ ] **Step 1: Write the failing tests**

```rust
#[test] fn drop_creates_bundle_named_after_target()   // a "Groceries", b → both bundle_id == Some(id); list_bundles()[0].name == "Groceries"
#[test] fn drop_onto_bundled_joins_it()                // c onto a → same id; list_bundles().len() == 1
#[test] fn drop_onto_self_rejected()                   // Err(InvalidTransition(_))
#[test] fn last_member_leaving_deletes_bundle()        // move a and b to None → list_bundles() empty
#[test] fn trashed_member_keeps_bundle_alive()         // trash a; move b out → bundle still listed
#[test] fn rename_bundle_works()
#[test] fn clone_follows_d20() {
    // original: "Plan #a" + manual ["b"], Blue, due 2026-10-20, in bundle, Done
    // clock.advance(5000); let c = s.clone_scratch(&o.id)
    // assert c.id != o.id, c.content == o.content, c.tags == o.tags, c.manual_tags == o.manual_tags
    // assert c.color == Some(Color::Blue), c.bundle_id == o.bundle_id
    // assert c.due_date == None, c.status == Status::Active, c.done_at == None, c.created_at == t0() + 5000
}
```

- [ ] **Step 2: Run to verify they fail**
Run: `cargo test -p noite-core --test bundles`
Expected: compile errors.

- [ ] **Step 3: Implement.** Clone inserts a new row, then calls `write_derived` and copies the manual tag rows, all in one transaction.

- [ ] **Step 4: Run to verify they pass**
Run: `cargo test -p noite-core --test bundles`
Expected: 7 passed.

- [ ] **Step 5: Commit**

```bash
git add crates/noite-core
git commit -m "feat(core): bundles via drag semantics and D20 clone"
```

### Task 7: Board queries, search, review counts

**Files:**
- Create: `src/board.rs`
- Modify: `src/model.rs`
- Test: `tests/board.rs`; unit tests for `fts_query` in `src/board.rs`

**Interfaces:**
- Produces:
  - `pub struct ScratchSummary { pub id: ScratchId, pub title: String, pub preview: String, pub first_image: Option<String>, pub color: Option<Color>, pub tags: Vec<String>, pub due_date: Option<NaiveDate>, pub bundle_id: Option<BundleId>, pub status: Status, pub created_at: i64, pub done_at: Option<i64>, pub trashed_at: Option<i64> }`
  - `pub struct ReviewCounts { pub overdue: u32, pub due_today: u32, pub open: u32 }`
  - On `Store`: `list(&self, tab: Tab, today: NaiveDate, search: Option<&str>) -> Result<Vec<ScratchSummary>>` and `review_counts(&self, today: NaiveDate) -> Result<ReviewCounts>`
  - `pub(crate) fn fts_query(input: &str) -> Option<String>`
- Ordering (D9 + roadmap decision 1):
  - **Today:** active scratches with `due ≤ today` or no due date, in the order overdue (due asc, then created asc) → due today (created asc) → undated (created asc).
  - **All:** every active scratch: the same order as Today, followed by upcoming (due asc, then created asc).
  - **Completed:** done, `done_at` desc.
  - **Trash:** trashed, `trashed_at` desc.
- `open` = count of all active scratches. Chip filtering is **not** here; the UI does it in M3 (D28).
- `fts_query`: split on whitespace, turn each term into `"` + term with `"` doubled + `"*`, and join with a space. Empty or whitespace-only input → `None`, which means no text filter.

- [ ] **Step 1: Write the failing tests.** Fixture: `overdue_a` (due 10-13), `overdue_b` (due 10-14), `due_today` (10-15), `upcoming` (10-20), `undated_old` (created t0), `undated_new` (created t0 + 1 h), one done, one trashed; `today()` = 10-15.

```rust
#[test] fn today_tab_order()      // ids == [overdue_a, overdue_b, due_today, undated_old, undated_new]
#[test] fn all_tab_order()        // ids == [overdue_a, overdue_b, due_today, undated_old, undated_new, upcoming]
#[test] fn completed_and_trash_tabs_sorted_newest_first()
#[test] fn search_prefix_and_accent_insensitive() {
    // "Buy milk", "Reunião amanhã": Some("mil") → [milk]; Some("reuniao") → [reunião]; Some("REUN") → [reunião]
    // search is scoped to the tab: a trashed "milk" note does not appear in All
}
#[test] fn search_never_errors_on_fts_syntax() {
    for q in ["\"", "-", "*", "(", "AND", "a OR", "NEAR(", "\"open", "#home", "   "] {
        assert!(s.list(Tab::All, today(), Some(q)).is_ok(), "query {q:?}");
    }
    assert_eq!(ids(s.list(Tab::All, today(), Some("   "))), ids(s.list(Tab::All, today(), None)));
}
#[test] fn review_counts_match_fixture()  // ReviewCounts { overdue: 2, due_today: 1, open: 6 }
#[test] fn summary_preview_and_first_image() // preview == first 200 graphemes; first_image == Some(id) for an image scratch
```

Unit test in `board.rs`: `fts_query("a \"b") == Some("\"a\"* \"\"\"b\"*".into())` and `fts_query(" ") == None`.

- [ ] **Step 2: Run to verify they fail**
Run: `cargo test -p noite-core --test board`
Expected: compile errors.

- [ ] **Step 3: Implement.** Use one SQL query per tab with a `CASE` sort bucket. Bind `today` as a `YYYY-MM-DD` string; dates compare correctly as text. Search adds `AND id IN (SELECT id FROM scratch_fts WHERE scratch_fts MATCH ?)`. Load tags with one grouped query for the result ids, not one query per row.

- [ ] **Step 4: Run to verify they pass**
Run: `cargo test -p noite-core --test board; cargo test -p noite-core --lib board::`
Expected: 7 integration tests + 1 unit test pass.

- [ ] **Step 5: Commit**

```bash
git add crates/noite-core
git commit -m "feat(core): board tab queries, safe FTS search and review counts"
```

### Task 8: Attachments and trash purge

**Files:**
- Create: `src/attachments.rs`
- Test: `tests/attachments.rs`

**Interfaces:**
- Produces:
  - `pub const MAX_ATTACHMENT_BYTES: u64 = 26_214_400`, `pub const ATTACHMENT_GRACE_MS: i64 = DAY_MS`
  - `pub struct PurgeReport { pub scratches_deleted: u32, pub attachments_deleted: u32 }`
  - On `Store`:
    - `put_attachment(&self, bytes: &[u8], ext: &str) -> Result<String>`, which returns the attachment id
    - `attachment_path(&self, id: &str) -> Result<PathBuf>`
    - `purge_trash(&mut self, retention_days: u32) -> Result<PurgeReport>`
- Rules:
  - **Ids and writing:** id = blake3 hex + `.` + ext. The ext is lowercased; if it isn't 1–8 ASCII alphanumerics, it becomes `bin`. Write to `<id>.tmp` and rename. If the file already exists, skip the write (dedupe).
  - **Path validation:** `attachment_path` accepts only `^[0-9a-f]{64}\.[a-z0-9]{1,8}$`; anything else returns `InvalidAttachmentId`.
  - **Purge order:**
    1. Check `retention_days` is in 1–365, else `InvalidSetting`.
    2. Delete trashed scratches with `trashed_at <= now − days·DAY_MS`, along with their tags and FTS rows.
    3. Call `delete_empty_bundles`.
    4. Collect `attachment_refs` across **all** remaining scratches.
    5. Delete files in `attachments\` that are unreferenced valid ids, or any `*.tmp`, but only where mtime `<= now − ATTACHMENT_GRACE_MS`.

- [ ] **Step 1: Write the failing tests**

```rust
#[test] fn put_dedupes_and_names_by_hash()     // put(b"abc","PNG") twice → same id, len 68, ends ".png"; exactly 1 file in dir
#[test] fn bad_extension_becomes_bin()         // put(b"x", "../evil") → id ends ".bin"
#[test] fn rejects_oversized() {
    let big = vec![0u8; 26_214_401];
    assert!(matches!(s.put_attachment(&big, "bin"),
        Err(Error::AttachmentTooLarge { size: 26_214_401, limit: 26_214_400 })));
}
#[test] fn attachment_path_rejects_traversal()  // "../noite.db", "..\\x", "abc.png", "" → Err(InvalidAttachmentId(_))
#[test] fn retention_boundaries() {
    // trash at t0; clock = t0 + 29 d → purge(30).scratches_deleted == 0
    // clock = t0 + 30 d → == 1; get → NotFound
}
#[test] fn shortened_retention_applies_to_existing_trash() // trash at t0; clock t0 + 8 d; purge(7) → 1 deleted
#[test] fn retention_out_of_range_rejected()   // purge(0), purge(366) → Err(InvalidSetting(_))
#[test] fn done_is_never_purged()              // done at t0; clock t0 + 400 d; purge(30) → still present
#[test] fn clone_keeps_shared_image_after_original_purged() {
    // spec §9 first test: id = put(png); set_mtime(path, t0 - 2 d); o = create(doc_with_image("pic", &id));
    // c = clone(o); trash(o); clock t0 + 31 d; purge(30)
    // assert attachment_path(&id) exists; get(c) content still references id
}
#[test] fn unreferenced_old_attachment_deleted()       // mtime t0 - 2 d, clock t0 → attachments_deleted == 1
#[test] fn unreferenced_fresh_attachment_survives()    // mtime t0 - 1 h, clock t0 → file still exists (autosave pending)
#[test] fn stale_tmp_files_cleaned()                   // "x.tmp" with mtime t0 - 2 d → deleted
#[test] fn purge_deletes_bundle_left_empty()           // a, b bundled; b moved out; a trashed + purged → list_bundles() empty
```

- [ ] **Step 2: Run to verify they fail**
Run: `cargo test -p noite-core --test attachments`
Expected: compile errors.

- [ ] **Step 3: Implement** as specified. Get file mtimes with `fs::metadata(..).modified()`, convert to ms, and compare against `self.now_ms()`.

- [ ] **Step 4: Run to verify they pass**
Run: `cargo test -p noite-core --test attachments`
Expected: 13 passed.

- [ ] **Step 5: Commit**

```bash
git add crates/noite-core
git commit -m "feat(core): content-addressed attachments and mark-and-sweep trash purge"
```

### Task 9: Settings

**Files:**
- Create: `src/settings.rs`
- Test: `tests/settings.rs`

**Interfaces:**
- Produces:
  - `pub enum BackupFrequency { Daily, Weekly }` (`rename_all = "lowercase"`)
  - `pub struct Settings` (Serialize/Deserialize, `#[serde(default)]` at struct level) with fields `capture_hotkey: String`, `board_hotkey: String`, `trash_retention_days: u32`, `day_start: NaiveTime`, `backup_dir: Option<PathBuf>`, `backup_frequency: BackupFrequency`, `backup_keep: u32`, `start_with_windows: bool`, `setup_completed: bool`
  - `impl Default for Settings`, `Settings::validate(&self) -> Result<()>`
  - On `Store`: `settings(&self) -> Result<Settings>` and `save_settings(&mut self, s: &Settings) -> Result<()>`. Stored as JSON in `settings` under key `"settings"`. `save_settings` validates first.
- Defaults (D24): `"Ctrl+Alt+N"`, `"Ctrl+Alt+B"`, `30`, `06:00`, `None`, `Daily`, `14`, `true`, `false`.
- Validation: `trash_retention_days` 1–365 and `backup_keep` ≥ 1. Hotkey strings are validated by the app in M5, not here.

- [ ] **Step 1: Write the failing tests**

```rust
#[test] fn defaults_match_spec()         // assert every default above; fresh store.settings() == Settings::default()
#[test] fn round_trip()                  // change several fields, save, reopen store → equal
#[test] fn validation()                  // retention 0 / 366, backup_keep 0 → save_settings Err(InvalidSetting(_)); stored value unchanged
#[test] fn partial_json_fills_defaults() // drop store; raw INSERT value '{"trash_retention_days":7}'; reopen → retention 7, rest default
```

- [ ] **Step 2: Run to verify they fail**
Run: `cargo test -p noite-core --test settings`
Expected: compile errors.

- [ ] **Step 3: Implement.**

- [ ] **Step 4: Run to verify they pass**
Run: `cargo test -p noite-core --test settings`
Expected: 4 passed.

- [ ] **Step 5: Commit**

```bash
git add crates/noite-core
git commit -m "feat(core): typed settings with spec defaults and validation"
```

### Task 10: Backup snapshots, prune, verify

**Files:**
- Create: `src/backup.rs`
- Test: `tests/backup.rs`

**Interfaces:**
- Consumes: `vacuum_into` (Task 2), `attachment_refs` (Task 3), `Store` accessors.
- Produces:
  - `pub fn snapshot(store: &Store, dest: &Path) -> Result<PathBuf>`
  - `pub fn prune(dest: &Path, keep: usize) -> Result<PruneReport>`
  - `pub fn verify(snapshot_db: &Path) -> Result<()>`
  - `pub struct PruneReport { pub snapshots_deleted: u32, pub attachments_deleted: u32 }`
- Layout under `dest` (`dest` must already exist; it is never created):

```
<dest>\noite-backups\snapshots\noite-YYYYMMDDTHHMMSSmmmZ.db
<dest>\noite-backups\attachments\<id>
```

- **Snapshot steps:**
  1. If `dest` doesn't exist, return `BackupDirMissing(dest)`.
  2. Create the subfolders.
  3. `vacuum_into` to `<name>.db.tmp`.
  4. Copy each attachment the snapshot references that's missing from the backup (via `.tmp` + rename).
  5. Rename the db `.tmp` to `.db` **last**, so a visible `.db` implies its attachments are present.

  The name comes from `store.now_ms()` formatted in UTC.
- **Prune:** keep the `keep` newest `.db` files by name, delete the rest and every `*.tmp`, then delete backup attachments not referenced by any retained snapshot. Read each snapshot read-only with `SELECT content FROM scratches`.
- **Verify:** `PRAGMA integrity_check` must return `ok`, and every referenced attachment must exist in the sibling `attachments\`; otherwise return `BackupInvalid(reason)`.

- [ ] **Step 1: Write the failing tests**

```rust
#[test] fn snapshot_layout_and_contents()       // path == dest/noite-backups/snapshots/noite-20261015T080000000Z.db;
                                                 // referenced attachment copied; no *.tmp remains; verify(path) is Ok
#[test] fn missing_dest_is_an_error()           // dest = tmp/"gone" (not created) → Err(BackupDirMissing(_)); tmp/"gone" still doesn't exist
#[test] fn snapshot_restores_into_fresh_store() {
    // copy snapshot → new_tmp/data/noite.db and backup attachments → new_tmp/data/attachments
    // Store::open on new data dir → get(id).content equal; attachment_path(att) exists
}
#[test] fn verify_detects_missing_attachment()  // delete the backed-up attachment → verify → Err(BackupInvalid(_))
#[test] fn prune_keeps_newest()                 // 4 snapshots (advance 60 s each); prune(dest, 2) → snapshots_deleted 2; the two newest names remain
#[test] fn prune_sweeps_unreferenced_backup_attachments() {
    // snapshot 1 with image A; update scratch to drop A; snapshots 2, 3; prune(dest, 2)
    // → backup attachment A deleted, attachments_deleted == 1
}
#[test] fn prune_removes_stray_tmp()            // a leftover "x.db.tmp" in snapshots → removed
```

- [ ] **Step 2: Run to verify they fail**
Run: `cargo test -p noite-core --test backup`
Expected: compile errors.

- [ ] **Step 3: Implement.** Open snapshots with `OpenFlags::SQLITE_OPEN_READ_ONLY`.

- [ ] **Step 4: Run to verify they pass**
Run: `cargo test -p noite-core --test backup`
Expected: 7 passed.

- [ ] **Step 5: Commit**

```bash
git add crates/noite-core
git commit -m "feat(core): consistent backup snapshots with prune and verify"
```

### Task 11: Lint gate and Windows CI

**Files:**
- Create: `.github/workflows/ci.yml`

- [ ] **Step 1: Make the lint gate clean locally**
Run: `cargo fmt --all --check; cargo clippy --workspace --all-targets -- -D warnings; cargo test --workspace`
Expected: no diffs, no warnings, all tests pass. Fix any findings.

- [ ] **Step 2: Write `ci.yml`.** On `push` and `pull_request`, one job on `windows-latest`: `actions/checkout@v4`, `Swatinem/rust-cache@v2`, then the three commands from Step 1. `rust-toolchain.toml` makes rustup install stable plus clippy and rustfmt automatically.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: run fmt, clippy and tests on Windows"
```

The repo has no GitHub remote yet, so the first real CI run happens when one is added. Report that to the user rather than claiming CI passes.
