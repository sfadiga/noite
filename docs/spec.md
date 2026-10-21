# noite — v1 specification

> Working name. A note-taking app for busy people: press a hotkey, write, and it's there tomorrow.
>
> Source requirements: [`requirements.md`](requirements.md). Decision numbers (D1–D30) come from the design session of 2026-10-15; the traceability table at the end maps each requirement to the decisions that satisfy it.
>
> **Revised 2026-10-17:** added the radar (D31–D34, §6). Capture is only half of noite; keeping notes in front of the user until they are dismissed is the core promise.
>
> **Revised 2026-10-16** after two spikes ([Tauri](spikes/2026-10-hotkey-latency.md), [Qt](spikes/2026-10-qt-capture.md)). The stack moved from Tauri 2 + React + TipTap to **Qt 6 Widgets + C++** (D3, D14, D15). The note format, performance metrics, default hotkey and test tooling changed with it (D5, D7, D24, D28).

## 1. Scope

**In v1:** setup wizard, global-hotkey capture, board, done + Completed area, trash with recovery window, tags + filter chips, image/clipboard paste, rich text, local persistence, folder backup, clone, bundles. (D1)

**Deferred to v2:** audio recording + speech-to-text, merge, split, Linux/macOS builds, code signing. (D1, D2, D23)

**Audience:** the author and a few friends. Distributed as an unsigned portable zip on GitHub Releases; users will see a SmartScreen "unknown publisher" prompt on first run. Each release has three assets: `noite-<v>-win64.zip` (a single `noite.exe` with the licence notices; no DLLs, no `data\`, no PDBs. Qt is linked statically from an unmodified build of qtbase published at `sfadiga/qt-static-windows`, pinned in `tools\qt-static.json`; noite's source is public under MIT, so it can be relinked against a modified Qt as the LGPL requires), `noite-<v>-win64-pdb.zip` (the PDB, for reading a friend's crash dump) and `SHA256SUMS.txt`. Pushing a tag `vX.Y.Z` makes CI create a draft release that the author checks and publishes by hand. (D23)

## 2. Platform & architecture

| Concern | Decision |
|---|---|
| OS | Windows only in v1; stack must not preclude Linux/macOS later. (D2) |
| Stack | **Qt 6 Widgets + C++20**, one native process, no WebView. Built with CMake + MSVC. (D3, D14) |
| Editor | **`QTextEdit` / `QTextDocument`**, with hand-written markdown typing shortcuts. The board's toolkit (Widgets, or one embedded QML view) is decided in M3 by measurement. (D15) |
| Data ownership | A UI-free C++ core library exclusively owns the database, attachments, backup and purge. Widgets never touch the filesystem; they call the core's API (e.g. `createScratch`, `listBoard`, `trash`). One writer, so no concurrent-write races. (D14) |
| Process model | Resident tray process. The global hotkey uses `RegisterHotKey` plus a native event filter. The capture window is created at startup and kept hidden; the hotkey only positions, shows and focuses it. (D5) |
| Single instance | One running instance per data folder. Launching a second copy against the same `data\` focuses the running one and exits. Separate portable folders may run side by side. (D27) |

### Performance budget (D5)

Enforced by automated benchmarks in CI (see §9).

| Metric | Target |
|---|---|
| Hotkey press → editor focused, caret ready | ≤ 100 ms |
| First summon after 15 min idle | ≤ 100 ms, target ≤ 50 ms. **Measured in M7b** (2026-10-20, real hotkey, one press per run). The shipped build was measured once: **119.8 ms** for a fresh process's first summon after 15 min idle, over the budget. Two runs on builds that don't ship give the used-process case: 86.5 ms (working-set floor on) and 82.5 ms (cloaked window); the shipped build without either is expected at or above those, not measured. The ~75 ms over a warm summon is the system waking: the first call into the compositor waits (in `ShowWindow`, or in the first present when the window is cloaked instead), with almost no page faults, and neither the floor nor cloaking moves it. A fresh process's first summon adds ~1,000 first-touch page faults (activation, first focus); with Start with Windows that is usually the day's first capture. **Known issue: the budget is missed by a fresh process's first summon after a long idle; the 50 ms target is not reachable from inside noite on this hardware.** Not gated in CI: no in-process simulation reproduces it. Details: [`spikes/2026-10-m7b-idle-summon.md`](spikes/2026-10-m7b-idle-summon.md). |
| Board open with 2,000 notes | ≤ 300 ms |
| Idle memory | ≤ 80 MB (Qt spike: 18.9 MB private) |

## 3. Portability & storage

### Layout (D4, D13)

```
noite\
  noite.exe
  Qt6*.dll, VC runtime DLLs, platforms\, styles\, imageformats\   (target ≈ 35 MB total)
  data\
    noite.db           SQLite database, attachments included (one file holds all the data)
    logs\              rotating logs
    migration-backups\ pre-migration database copies
```

- Nothing is installed; no registry writes, **except** the optional "Start with Windows" entry. When enabled, every launch rewrites that entry with the current exe path, so moving the folder (or a USB drive letter changing) doesn't break it. (D4)
- No network code anywhere in the app. (D13, D26, D29)

### Backup (D13)

- Periodic **consistent snapshot** of the database (`VACUUM INTO`), written to a user-chosen folder. Attachments live in the database, so each snapshot is one self-contained `.db` file (and each carries every attachment). The live database is never placed in a sync folder.
- Defaults: daily, keep last 14. If Google Drive for Desktop is installed, its folder is suggested in setup. No Google sign-in.
- Google Drive is used through Drive for Desktop's mounted folder (`G:\Meu Drive` etc.), like any local folder. Until the setup wizard exists, the first "Back up now" with no folder set offers the detected Drive folder in one click, else a folder picker; the choice is saved. Scheduled backups stay off until a folder is chosen.
- Each backup is snapshot, verify, prune: a snapshot that fails verification is deleted and the run counts as failed.
- Schedule: daily = once per local calendar day; weekly = 7 days after the last successful backup. Missed runs catch up at the next check (30 s after start, hourly, and on resume).
- "Back up now" is available from the tray and always toasts the result. Scheduled backups are silent, unless they have been failing for 3 days: then a toast, at most once a day.

### Attachment cleanup (D22)

The trash purge runs once a day, at the first check. When it runs, it scans every remaining note for attachment references and deletes only attachment rows no note references and that weren't pasted within the last day (mark-and-sweep), then returns the freed space to the filesystem (incremental auto-vacuum). No per-attachment usage counters.

### Updates & schema migrations (D26)

- The app never checks for updates. Updating = replace `noite.exe`; `data\` is untouched.
- On startup, if the schema needs migrating: take a backup snapshot first, then migrate. If the migration fails, refuse to start (data stays as it was, snapshot available).
- An older exe refuses to open a database written by a newer schema version.
- **Restoring a backup** is a manual copy: quit noite, move `data\noite.db`, and `data\noite.db-wal` and `data\noite.db-shm` if present, into a folder of your own (the database runs in WAL mode, and a stale `-wal` file would replay onto the restored snapshot), copy a snapshot from `<backup folder>\noite-backups\<id>\snapshots\` into `data\` and rename it `noite.db`, start noite. The release README.txt says so.
- **Upgrade guarantee:** committed golden data folders, one per schema version (`core/tests/golden/vN`), which the current build opens in `tst_upgrade` on every push. Before bumping the schema to vN+1, generate and commit `golden/vN` with the then-current build.

### Logs (D29)

Rotating logs in `data\logs\noite.log` (7 files × 1 MB). One line per message: `yyyy-MM-dd HH:mm:ss.zzz L [thread] message`, with L one of D/I/W/C/F; debug lines are dropped in release builds. Startup and quit are logged (`noite <version> started · data <dir> · Qt <ver> · Windows <ver>`, `noite quit`). A crash appends a `CRASH` line and a `module+offset` stack trace to the log and writes a minidump `crash-YYYYMMDD-HHMMSS.dmp` next to it (newest 3 kept), then the process ends without a dialog. Release builds produce PDBs, which are not in the portable zip; each release attaches them as `noite-<v>-win64-pdb.zip` so a dump can be read later in Visual Studio. No telemetry. Settings has "Open logs folder".

## 4. Data model

### Scratch (a note)

| Field | Source |
|---|---|
| `id` | generated |
| `content` | **Qt rich-text HTML** (`QTextDocument::toHtml`), the canonical format. Images and files are referenced as `noite-att:<hash>.<ext>` and stored in the database's `attachments` table. There is no `//`: with an authority, QUrl parses the hash as a hostname, rejects it, and every image ends up sharing one key. Markdown is export-only: `toMarkdown()` loses bold and tables. (D7) |
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
active ──defer──▶ deferred ──bring back──▶ active        (M8c, D36)
deferred ──done──▶ done
active   ─┐
done     ─┼trash─▶ trashed ──after N days──▶ permanently deleted
deferred ─┘
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
- `Esc` saves and hides it. Clicking away leaves it open on top, so you can keep typing while reading something else; if the board or the review opens or acts on the same note, the capture saves and hides first. A scratch closed while empty is discarded. (D6, revised in 1.0.1)
- Its margins move it; dragging any edge or corner resizes it (at least 320×200). The size lasts until noite quits, shrunk to fit a smaller monitor. (1.0.1)
- Editing: WYSIWYG with markdown typing shortcuts. (D7)
  - `#`/`##`/`###` give headings; `-`/`*` give bullets; `1.` gives a numbered list.
  - `[ ]`, `[]` and `[x]`, either after a bullet or at the start of a line, give a checkbox, and clicking it toggles it.
  - `**bold**`, `*italic*`/`_italic_`, `` `code` ``.
  - `[label](url)`, or a bare URL followed by a space, becomes a link; Ctrl+click opens it.
  - Pasted text is rendered as markdown only if it has at least two markdown constructs, and each line stays a line. A pasted bare URL becomes a link. `Ctrl+Shift+V` always pastes plain text.
  - Ctrl+click opens only `http`, `https` and `mailto` links. Pasted HTML can carry `file:` or other schemes, which Windows would run.
  - Clipboard precedence: HTML with content beats the bitmap that spreadsheets and OneNote add alongside it.
- Keys: `Ctrl+Enter` mark done and close · `Esc` hide · `Ctrl+Shift+C` colour. (D30)

### Paste handling (D21)

| Clipboard content | Result |
|---|---|
| Image | inline image (stored as attachment) |
| Copied file(s) | stored in the database, shown as a clickable file chip; max 25 MB each. Ctrl+click writes it to `%TEMP%\noite-open\` under the chip's name, always with the stored file type, and opens it (programs and untyped files open their folder instead); those copies are removed at startup |
| Rich HTML (browser, Word) | converted to the editor's supported formatting |
| Spreadsheet cells | simple table |
| Anything else | plain text |

## 6. Board

- A normal window opened by tray click or the board hotkey (default `Ctrl+Alt+B`). (D17)
- **Tabs:** Today · All · Completed · Deferred · Trash, each showing how many notes it holds under the current search. (D17)
- **Layout:** card grid (Google Keep–style). Bundles render as stacked cards that expand in place. Drag a card onto another card or a bundle to bundle it. (D17)
- **Sort:** automatic — overdue, due today, then undated oldest-first. Every active card shows its age ("3h" on its first day, then "12d"), next to its due label if it has one. No manual reordering in v1. (D9, D17)
- **Filter chips:** one per tag, one per colour, plus Overdue / Due today / No date. Chips of the same kind combine with OR; different kinds combine with AND. Chips that would yield zero results are hidden. (D18)
- **Search:** as-you-type full-text search over the current tab, combined with active chips. (D19)
- Keys: `/` search · `N` new · `D` done · `Del` trash · `Ctrl+Z` undo last board action · arrow keys move between cards · `L` defer / bring back · `Ctrl+1`–`Ctrl+5` Today / All / Completed / Deferred / Trash · `Ctrl+E` export. (D30; tab keys added in M7c, Deferred in M8c, export in M8a)
- Theme follows Windows light/dark. (D30)
- **D35 Export (M8a).** One note to a self-contained **HTML** file (images embedded) or an A4 **PDF** (20 mm margins, wide images fitted to the text width, links kept), from the card menu's **Export…** or `Ctrl+E`, on the board or in the note open over it (saved first), on any tab. A Save dialog suggests the title as a safe file name in the last export folder (Documents the first time). Light look, content only. A missing image becomes `[missing image]`; a failed write leaves no file and shows the reason. Design: `docs/superpowers/specs/2026-10-21-m8a-export-design.md`.

### The radar (D31–D34, added 2026-10-17)

The app's core promise: **what you capture stays under your radar until you dismiss it for good.**

- **D31 Radar rule.** Every active scratch stays on the radar until it is marked **done**, **trashed** or **deferred** (M8c); those are the only ways to dismiss one. Nothing expires or drops off on its own.
- **D32 Morning review.** Once a day (trigger: D16) a review steps through what needs a decision: overdue (most overdue first), due today, then stale (D33). The rest of the radar stays on the board. For each scratch the user can choose **Done**, **Snooze**, **Keep**, **Defer** (M8c) or **Trash**. Left goes back and undoes the previous choice. Esc ends early; the notes not reached stay as they are. (M4 R2, R3)
- **D33 Stale resurfacing.** An undated active scratch whose `modified_at` is older than *N* days (setting, default **7**, range 1–90) comes after the due-today notes in the review, so nothing quietly rots. **Keep** marks it as looked at (`modified_at` = now), so it returns only after another *N* untouched days. (M4 R2, R4)
- **D34 Snooze = due date.** Snoozing sets a future due date (quick picks: tomorrow, next Monday, pick a date). The scratch leaves Today and comes back on that day (D9). There is no separate snooze state.
- **D36 Deferred (M8c).** A note can be **deferred**: kept, but off the radar. It leaves Today, All and the morning review (the undated fallback too) and never turns overdue or stale, until it is brought back (`L` on the Deferred tab, or the card menu). Done and trash work from Deferred; restoring from Trash returns it there. Design: `docs/superpowers/specs/2026-10-21-m8c-deferred-design.md`.

### Start-of-day review (D16)

Configurable day-start hour (default 06:00). The first time the PC is awake and unlocked after that hour, the morning review (D32) opens directly, in front (the author's choice in M4, R1). Triggers: app start, session unlock, resume from sleep, and a timer at the day start. It counts as shown once it opens, so closing it early doesn't bring it back that day. If nothing needs a decision, nothing opens. The tray's "Morning review" opens it any time.

## 7. Setup wizard (D24)

Shown on first run — and on any existing data folder whose `setupCompleted` is false, prefilled with its current settings (M6 S1). While the wizard is open, the morning review check and background job timers wait until it closes (M6 S6). Every step except step 2 has **Skip setup**, which finishes with the values shown and the accepted capture hotkey, without the starter note.

1. **Welcome** — "Your notes live in this folder: `<data dir>`. Nothing leaves your PC." When `data\` sits inside a Google Drive, OneDrive or Dropbox folder, a warning explains that syncing a live database can corrupt it.
2. **Capture hotkey** — user presses the combination (default `Ctrl+Alt+J`; `Ctrl+Alt+N` and `Ctrl+Alt+Space` were already taken on the author's PC). Probed as the user types; if another app holds it (or it matches the board hotkey), flag the conflict and ask for another. The wizard registers the capture hotkey when leaving step 2.
3. **Board hotkey** — default `Ctrl+Alt+B`, same conflict check; registered when leaving step 3.
4. **Trash recovery window** — default 30 days (1–365).
5. **Mornings** — day-start hour (default 06:00) and stale resurfacing threshold ("Bring back untouched notes after *N* days", default 7, range 1–90; D33).
6. **Backup** — choose folder (suggest Google Drive for Desktop folder if found), frequency (daily), retention (14). The folder may stay unset; "Back up now" asks later.
7. **Start with Windows** — on by default (disabled on a `--data` run).
8. **Try it** — "Press `<capture hotkey>` now"; pressing the capture hotkey finishes the wizard and opens the real capture window prefilled with a starter how-to note (using the user's chosen hotkeys and stale days; M6 S2). Clicking **Finish** finishes without the starter note.

Closing the wizard with ✕ ("abandoned") applies what was collected so far, leaves `setupCompleted` false so the wizard returns at the next start, and registers the current capture hotkey if none was accepted yet (showing the tray warning if another program holds it).

## 8. Settings & tray (D25)

- **Settings window:** one grouped page (**Hotkeys**, **Notes**, **Backup**, **System**), plus the sync-folder warning at the top when applicable (M6 S3). **Save** applies all changes live without a restart; **Cancel** discards them and restores any paused hotkeys. Hotkey changes are applied all-or-nothing before saving — a combination that cannot be registered is never saved, and the previous hotkeys stay in force (M6 S5). Backup shows the last-backup status (`today`, `yesterday`, `MMM d`, `never`, or `failing since MMM d`) and a **Back up now** button. System has **Start with Windows**, **Open logs folder** (D29) and **Run setup again**.
- **Tray menu:** New note · Board · Morning review · Start with Windows (checkable) · Settings · Back up now (reads "Backing up…" and is disabled while a backup runs) · Quit. Left-click opens the board.

## 9. Testing & CI (D28)

| Layer | Tooling | Covers |
|---|---|---|
| Core | Qt Test, written test-first | storage, trash purge, attachment cleanup, backup snapshots, schema migrations, clone rules, title/tag derivation |
| UI logic | Qt Test on the `offscreen` platform | markdown shortcuts, paste handling, chip combination logic, `@date` parsing |
| End-to-end | `tools/e2e/run-e2e.ps1`: real keystrokes from a dedicated target window (leak detection), each step checked in `noite.db` read-only by `tools/e2e/noite_db.py`; CI on every push (M7c) | capture → board → done → trash → restore |
| Performance | `capture-bench` (summon by phase: shown, activated, painted, on screen; page faults) + `board-bench` (seeded 2,000 notes), on the real platform in CI; the after-idle summon is measured by hand with the keystroke harness (no in-process simulation reproduces it, M7b) | fails the build if the §2 budget is missed |
| CI | GitHub Actions, `windows-latest` | builds, tests, and on every push packages and checks the portable zip (`deploy.ps1`, `package.ps1 -DryRun`); a `vX.Y.Z` tag also creates a draft GitHub Release |

Keystroke and end-to-end runs send real input, so they only run on CI or while the author is away from the PC.

The first automated test is the attachment-safety case: paste image → clone → trash original → purge → open clone → image still loads.

## 10. Deferred to v2

- **Merge** and **split** — rules for combining or dividing content and metadata, designed on top of the D7 document model. The Qt spike moved a selection containing one image into a new document; more images, and tables, are untested.
- **Audio recording + speech-to-text** — offline, local model (whisper.cpp class) as an optional download to keep the portable zip small.
- **Linux / macOS** builds.
- **Code signing** (e.g. Azure Trusted Signing) at public release.

## 11. Requirements traceability

| Req ([`requirements.md`](requirements.md)) | Summary | Status | Decisions |
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
