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

**Audience:** the author and a few friends. Distributed as an unsigned portable zip on GitHub Releases; users will see a SmartScreen "unknown publisher" prompt on first run. (D23)

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
| First summon after 15 min idle | ≤ 100 ms, target ≤ 50 ms. **Known issue:** the M2 app measured 103 ms (keep-warm off) and 91 ms (keep-warm on), one press each (2026-10-17). Keep-warm (offscreen render while hidden) ships on but doesn't reach the target. A phase-by-phase breakdown and the next fix are in M7. |
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
- "Back up now" is available from the tray.

### Attachment cleanup (D22)

When the trash purge runs, it scans every remaining note for attachment references and deletes only attachment rows no note references and that weren't pasted within the last day (mark-and-sweep), then returns the freed space to the filesystem (incremental auto-vacuum). No per-attachment usage counters.

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
- **Tabs:** Today · All · Completed · Trash. (D17)
- **Layout:** card grid (Google Keep–style). Bundles render as stacked cards that expand in place. Drag a card onto another card or a bundle to bundle it. (D17)
- **Sort:** automatic — overdue, due today, then undated oldest-first with an age badge (e.g. "12d"). No manual reordering in v1. (D9, D17)
- **Filter chips:** one per tag, one per colour, plus Overdue / Due today / No date. Chips of the same kind combine with OR; different kinds combine with AND. Chips that would yield zero results are hidden. (D18)
- **Search:** as-you-type full-text search over the current tab, combined with active chips. (D19)
- Keys: `/` search · `N` new · `D` done · `Del` trash · `Ctrl+Z` undo last board action · arrow keys move between cards. (D30)
- Theme follows Windows light/dark. (D30)

### The radar (D31–D34, added 2026-10-17)

The app's core promise: **what you capture stays under your radar until you dismiss it for good.**

- **D31 Radar rule.** Every active scratch stays on the radar until it is marked **done** or **trashed**; those are the only two ways to dismiss one. Nothing expires or drops off on its own.
- **D32 Morning review.** Once a day (trigger: D16) a review presents the radar: overdue, due today, then undated oldest-first. For each scratch the user can choose **Done**, **Snooze**, **Keep** or **Trash**. Open question for the M4 plan: open the review directly, or notify first and open on click (D16 says the notification never steals focus).
- **D33 Stale resurfacing.** An undated active scratch whose `modified_at` is older than *N* days (setting, default **7**) is moved to the top of the review, so nothing quietly rots at the bottom of the list.
- **D34 Snooze = due date.** Snoozing sets a future due date (quick picks: tomorrow, next Monday, pick a date). The scratch leaves Today and comes back on that day (D9). There is no separate snooze state.

### Start-of-day review (D16)

Configurable day-start hour (default 06:00). The first time the PC is awake and unlocked after that hour, show a Windows notification: "3 overdue · 2 due today · 14 open". Clicking it opens the board on the Today tab. It never steals focus.

## 7. Setup wizard (D24)

Shown on first run. Every step except step 2 can be skipped.

1. **Welcome** — "Your notes live in this folder. Nothing leaves your PC."
2. **Capture hotkey** — user presses the combination (default `Ctrl+Alt+J`; `Ctrl+Alt+N` and `Ctrl+Alt+Space` were already taken on the author's PC). Registered immediately; if another app holds it, flag the conflict and ask for another.
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
| Core | Qt Test, written test-first | storage, trash purge, attachment cleanup, backup snapshots, schema migrations, clone rules, title/tag derivation |
| UI logic | Qt Test on the `offscreen` platform | markdown shortcuts, paste handling, chip combination logic, `@date` parsing |
| End-to-end | keystroke harness from the spikes (dedicated target window, leak detection) | capture → board → done → trash → restore |
| Performance | instrumented timestamps ("hotkey received", "editor painted with focus") + seeded 2,000-note benchmark + an idle-then-summon scenario | fails the build if the §2 budget is missed |
| CI | GitHub Actions, `windows-latest` | builds the portable zip |

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
