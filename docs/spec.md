# noite — v1 specification

> Working name. A note-taking app for busy people: press a hotkey, write, and it's there tomorrow.
>
> Source requirements: [`requirements.txt`](../requirements.txt). Decision numbers (D1–D30) come from the design session of 2026-10-15; the traceability table at the end maps each requirement to the decisions that satisfy it.

## 1. Scope

**In v1:** setup wizard, global-hotkey capture, board, done + Completed area, trash with recovery window, tags + filter chips, image/clipboard paste, rich text, local persistence, folder backup, clone, bundles. (D1)

**Deferred to v2:** audio recording + speech-to-text, merge, split, Linux/macOS builds, code signing. (D1, D2, D23)

**Audience:** the author and a few friends. Distributed as an unsigned portable zip on GitHub Releases; users will see a SmartScreen "unknown publisher" prompt on first run. (D23)

## 2. Platform & architecture

| Concern | Decision |
|---|---|
| OS | Windows only in v1; stack must not preclude Linux/macOS later. (D2) |
| Shell | **Tauri 2.** (D3, D14) |
| UI | **React + TypeScript + TipTap** (ProseMirror). (D15) |
| Data ownership | The Rust side exclusively owns the database, attachments, global hotkeys, tray, autostart, backup and purge. The UI never touches the filesystem; it calls a small, typed command API (e.g. `createScratch`, `listBoard`, `trash`). Single writer → no concurrent-write races. (D14) |
| Process model | Resident tray process. The capture window is created at startup and kept hidden; the hotkey only shows and focuses it. (D5) |
| Single instance | One running instance per data folder. Launching a second copy against the same `data\` focuses the running one and exits. Separate portable folders may run side by side. (D27) |

### Performance budget (D5)

Enforced by automated benchmarks in CI (see §9).

| Metric | Target |
|---|---|
| Hotkey press → editor focused, caret ready | ≤ 100 ms |
| Board open with 2,000 notes | ≤ 300 ms |
| Idle memory | ≤ 80 MB |

## 3. Portability & storage

### Layout (D4, D13)

```
noite\
  noite.exe
  data\
    noite.db           SQLite database
    attachments\       content-addressed files (name = hash of contents)
    logs\              rotating logs
```

- Nothing is installed; no registry writes, **except** the optional "Start with Windows" entry. When enabled, every launch rewrites that entry with the current exe path, so moving the folder (or a USB drive letter changing) doesn't break it. (D4)
- No network code anywhere in the app. (D13, D26, D29)

### Backup (D13)

- Periodic **consistent snapshot** of the database (SQLite backup API / `VACUUM INTO`) plus attachments, written to a user-chosen folder. The live database is never placed in a sync folder.
- Defaults: daily, keep last 14. If Google Drive for Desktop is installed, its folder is suggested in setup. No Google sign-in.
- "Back up now" is available from the tray.

### Attachment cleanup (D22)

When the trash purge runs, it scans every remaining note for attachment references and deletes only attachments no note references (mark-and-sweep). No per-file usage counters.

### Updates & schema migrations (D26)

- The app never checks for updates. Updating = replace `noite.exe`; `data\` is untouched.
- On startup, if the schema needs migrating: take a backup snapshot first, then migrate. If the migration fails, refuse to start (data stays as it was, snapshot available).
- An older exe refuses to open a database written by a newer schema version.

### Logs (D29)

Rotating logs in `data\logs\` (7 files × 1 MB). Crashes logged with a stack trace. No telemetry. Settings has "Open logs folder".

## 4. Data model

### Scratch (a note)

| Field | Source |
|---|---|
| `id` | generated |
| `content` | TipTap/ProseMirror JSON document — the canonical format. Markdown is export-only. (D7) |
| `title` | derived from the first line of text, truncated to 60 chars; no AI. (D10) |
| `created_at`, `modified_at` | automatic (D10) |
| `due_date` | optional (D9) |
| `color` | optional, one of 6 swatches; none by default (D10) |
| `tags` | set; from inline `#tag` in the text and/or a tag field (D10) |
| `bundle_id` | optional; at most one bundle per scratch (D12) |
| `status` | `active` \| `done` \| `trashed` (D11) |
| `trashed_at` | set on trash; drives the purge (D11) |

- **Unit of state:** the whole scratch is what's marked done, trashed or given a due date. Checkboxes inside a scratch are formatting only and do not affect board state. (D8)
- **Full-text index:** SQLite FTS5 over the plain text extracted from `content`. (D19)

### Bundle (D12)

A named group, shown on the board as a stack of cards. A scratch belongs to at most one bundle; tags cover anything needing multiple membership.

### Lifecycle (D11)

```
active ──done──▶ done        (reopenable; never auto-deleted)
active ─┐
done   ─┴trash─▶ trashed ──after N days──▶ permanently deleted
trashed ──restore──▶ previous status
```

N = trash recovery window, default 30, range 1–365. No archive state.

### Due dates (D9)

Set by typing `@tomorrow`, `@fri`, etc. in the text, or via a date picker.

### Clone (D20)

| Copied | Not copied |
|---|---|
| content, tags, colour, bundle (clone placed next to original) | due date (cleared), status (clone is always `active`), `created_at` (new) |

Attachments are shared, not duplicated (safe because of D22).

## 5. Capture window

- The hotkey **always opens a new blank scratch**, always-on-top, centred on the monitor with the active window. (D6)
- Autosave on every keystroke (debounced). No Save button. (D6)
- `Esc` or clicking away saves and hides it. A scratch closed while empty is discarded. (D6)
- Editing: WYSIWYG with markdown shortcuts — `**bold**`, `- [ ]` → checkbox, `#` → heading, clickable links. (D7)
- Keys: `Ctrl+Enter` mark done and close · `Esc` hide · `Ctrl+Shift+C` colour. (D30)

### Paste handling (D21)

| Clipboard content | Result |
|---|---|
| Image | inline image (stored as attachment) |
| Copied file(s) | copied into `attachments\`, shown as a clickable file chip; max 25 MB each |
| Rich HTML (browser, Word) | converted to the editor's supported formatting |
| Spreadsheet cells | simple table |
| Anything else | plain text |

## 6. Board

- A normal window opened by tray click or the board hotkey (default `Ctrl+Alt+B`). (D17)
- **Tabs:** Today · All · Completed · Trash. (D17)
- **Layout:** card grid (Google Keep–style). Bundles render as stacked cards that expand in place. Drag a card onto another card or a bundle to bundle it. (D17)
- **Sort:** automatic — overdue, due today, then undated oldest-first with an age badge (e.g. "12d"). No manual reordering in v1. (D9, D17)
- **Filter chips:** one per tag, one per colour, plus Overdue / Due today / No date. Chips of the same kind combine with OR; different kinds combine with AND. Chips that would yield zero results are hidden. (D18)
- **Search:** as-you-type full-text search over the current tab, combined with active chips. (D19)
- Keys: `/` search · `N` new · `D` done · `Del` trash · `Ctrl+Z` undo last board action · arrow keys move between cards. (D30)
- Theme follows Windows light/dark. (D30)

### Start-of-day review (D16)

Configurable day-start hour (default 06:00). The first time the PC is awake and unlocked after that hour, show a Windows notification: "3 overdue · 2 due today · 14 open". Clicking it opens the board on the Today tab. It never steals focus.

## 7. Setup wizard (D24)

Shown on first run. Every step except step 2 can be skipped.

1. **Welcome** — "Your notes live in this folder. Nothing leaves your PC."
2. **Capture hotkey** — user presses the combination (default `Ctrl+Alt+N`). Registered immediately; if another app holds it, flag the conflict and ask for another.
3. **Board hotkey** — default `Ctrl+Alt+B`, same conflict check.
4. **Trash recovery window** — default 30 days (1–365).
5. **Day-start hour** — default 06:00.
6. **Backup** — choose folder (suggest Google Drive for Desktop folder if found), frequency (daily), retention (14).
7. **Start with Windows** — on by default.
8. **Try it** — "Press your hotkey now"; the wizard finishes when the user does.

## 8. Settings & tray (D25)

- Every wizard setting is editable later in a Settings window; the wizard can be re-run from Settings.
- Tray menu: New scratch · Open board · Settings · Back up now · Quit.

## 9. Testing & CI (D28)

| Layer | Tooling | Covers |
|---|---|---|
| Rust | `cargo test`, written test-first | storage, trash purge, attachment cleanup, backup snapshots, schema migrations, clone rules |
| UI logic | Vitest | chip combination logic, `@date` parsing, title derivation |
| End-to-end | `tauri-driver` / WebDriver on Windows | capture → board → done → trash → restore |
| Performance | instrumented timestamps ("hotkey received", "editor focused") + seeded 2,000-note benchmark | fails the build if the §2 budget is missed |
| CI | GitHub Actions, `windows-latest` | builds the portable zip |

The first automated test is the attachment-safety case: paste image → clone → trash original → purge → open clone → image still loads.

## 10. Deferred to v2

- **Merge** and **split** — rules for combining or dividing content and metadata to be designed on top of the D7 document model.
- **Audio recording + speech-to-text** — offline, local model (whisper.cpp class) as an optional download to keep the portable zip small.
- **Linux / macOS** builds.
- **Code signing** (e.g. Azure Trusted Signing) at public release.

## 11. Requirements traceability

| Req (`requirements.txt`) | Summary | Status | Decisions |
|---|---|---|---|
| 1 | Windows (Linux, mac if possible) | v1 Windows; others v2 | D2 |
| 2 | Step-by-step first-run configuration | v1 | D24 |
| 3 | Configure capture keystroke first | v1 | D24 step 2 |
| 4 | Hotkey brings up modal writing area | v1 | D5, D6 |
| 5 | Context metadata (date/time, title, color) | v1 | D10 |
| 6 | Start-of-day reminder, oldest first / due | v1 | D9, D16 |
| 7 | Paste images and other objects | v1 | D21 |
| 8 | Persist data | v1 | D13 |
| 9 | Works offline | v1 | D13, D26, D29 |
| 10 | Blazing fast | v1 | D5, D28 |
| 11 | For to-dos and small notes | v1 | D8 |
| 12 | Bundle scratches on the board | v1 | D12, D17 |
| 13 | Portable | v1 | D4 |
| 14 | Mark items done | v1 | D11 |
| 15 | Trash scratches | v1 | D11 |
| 16 | Trash recoverable for a configurable window | v1 | D11, D24 step 4 |
| 17 | Automatic Google Drive backup | v1 (via synced folder) | D13 |
| 18 | Done items hidden from board, shown in Completed area | v1 | D11, D17 |
| 19 | Audio recording + speech-to-text | **v2** | D1 |
| 20 | Markdown / rich text / links | v1 | D7 |
| 21 | Clone and merge | clone v1, merge **v2** | D1, D20 |
| 22 | Split by selection | **v2** | D1, D7 |
| 23 | Metadata for quick filtering | v1 | D10 |
| 24 | Chip filtering on the board | v1 | D18 |
