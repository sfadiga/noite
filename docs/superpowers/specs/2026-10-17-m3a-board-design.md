# M3a: Board (browse and act) — Design

**Date:** 2026-10-17 · **Roadmap:** M3 Board, moved ahead of the radar so captured notes have a way back. M3 is split in two:

- **M3a (this design):** board window, tabs, card grid, search, open and edit a note, done/trash/restore, keys, undo, theme.
- **M3b (separate design later):** bundles (stacked cards, drag to bundle, expand in place), filter chips (D18), manual tag editing.

**Spec:** [`docs/spec.md`](../../spec.md) §6 (D17, D19, D30), §4 lifecycle (D11), clone (D20), due dates (D9), performance (D5). Builds on M1 (`noite-core`) and M2 (capture app).

## Intent

The user captures notes all day with `Ctrl+Alt+J` but has no way to see them again. M3a delivers the backlog: open the board, find a note by tab or search, read and edit it, mark it done, trash it, recolour it or set its due date, all from the keyboard or the mouse.

**Success:**
- `Ctrl+Alt+B` or the tray shows every active note, sorted overdue → due today → undated oldest first.
- Any note can be found by typing part of it.
- Any note can be opened, edited and closed with no lost text.
- The board opens in ≤ 300 ms with 2,000 notes (D5).

## Decisions

| # | Decision | Source |
|---|---|---|
| B1 | Toolkit: **Qt Widgets**. `QListView` in icon mode with a custom card delegate (virtualised; fixed-size cards, no masonry). No Qt Quick. Task 1 is a measured gate; QML is reconsidered only if it fails, and only after asking the user. | User, 2026-10-17 |
| B2 | Clicking a card (or `Enter`) opens it in an **editor overlay on top of the board** (Keep-style), not in the capture window or a side pane. | User, 2026-10-17 |
| B3 | Emptying an existing note and closing the overlay **hard-deletes** it (`discardIfEmpty`), the same as in capture. | Design, approved |
| B4 | Tab and search text are remembered for the app session only, not across restarts. | Design, approved |
| B5 | Tray left-click now opens the **board**. "New note" stays in the tray menu, and "Board" is added. This changes M2 behaviour. | Design, approved |
| B6 | The board window is created on first open and destroyed on close (idle memory). If that misses the 300 ms gate, it's kept hidden after the first open instead. | Design, approved |
| B7 | Clone (D20) is in M3a's context menu, because the core already supports it. | Design, approved |

## Architecture

```text
app/src/board/
├── BoardModel        # QAbstractListModel over Store::list(tab, today, search); one row per card
├── CardDelegate      # paints a fixed-size card: colour tint, title, ~4-line preview,
│                     #   first-image thumbnail, due/age badge ("12d", "overdue"), tags
├── BoardWindow       # top-level window: tab bar, search field, QListView grid, status line
├── NoteOverlay       # editor on top of the board: NoteEdit + footer (due, colour)
└── UndoLog           # stack of inverse board actions
```

- **Reuse:** `NoteOverlay` hosts a `NoteEdit` driven by a `CaptureController`. The controller gains `open(ScratchId)`, which loads an existing note (HTML, due date, colour) without marking it dirty. Autosave, flush-on-close, `@date` tokens, colour cycling and discard-if-empty then behave as in capture.
- **Change notification:** `CaptureController` gains a `saved(ScratchId)` signal. The App connects the capture controller's and the overlay controller's `saved` and `finished` to `BoardModel::reload()` while the board exists. A note captured while the board is open appears without reopening it.
- **Data ownership (D14):** board widgets only call `Store`. Thumbnails load through `AttachmentStore::pathFor`, as the editor already does.
- **Hotkey:** `Settings::boardHotkey` (default `Ctrl+Alt+B`) is registered through `NativeHost` with its own id. If it's taken, the tray shows a warning and the app keeps running, the same rule as the capture hotkey.
- **Today:** the board uses the same injected `today` function as capture, so tests can pin the date.

## Interaction

**Tabs:** Today · All · Completed · Trash. Each is one `Store::list` call. The core owns the order; the UI never re-sorts.

**Search:**
- `/` focuses the search field. Typing re-queries after a 120 ms debounce, within the current tab.
- `Esc` in a non-empty field clears it. `Esc` with the field empty, or with the grid focused, closes the board.

**Grid keys (D30):**

| Key | Action |
|---|---|
| Arrows | Move the selection |
| `Enter` | Open the selected note in the overlay |
| `N` | New note in the overlay |
| `D` | Done; Reopen in the Completed tab |
| `Del` | Trash; Restore in the Trash tab |
| `Ctrl+Z` | Undo the last board action |
| `Ctrl+Shift+C` | Cycle the selected note's colour |

**Mouse:**
- Click opens the note.
- Right-click menu: Open · Done/Reopen · Trash/Restore · Colour ▸ (none + 6 swatches) · Due date ▸ (Today, Tomorrow, Pick…, Clear) · Clone.
- "Pick…" opens a `QCalendarWidget` popup.

**After an action:**
- A card that no longer belongs to the tab is removed right away (e.g. Done in Today), and the selection moves to the next card, or the previous one at the end.
- The status line shows what happened, e.g. "Marked done · Ctrl+Z to undo".

**Undo (`UndoLog`):**
- One stack per board session (cleared when the board closes).
- It records the inverse of each action: done↔reopen, trash↔restore, colour (previous value), due date (previous value), and clone (trash the copy).
- It doesn't cover text edits, which use the editor's own undo.

**Overlay:**
- Covers the board's client area with a dimmed backdrop and a centred editor panel, with the editor focused.
- The footer shows the due date and colour, the same as the capture window.
- `Esc` or a click on the backdrop saves and closes. `Ctrl+Enter` marks done and closes. `Ctrl+Shift+C` cycles the colour. `@tokens` set the due date.
- On close the board reloads and reselects the card if it's still in the tab.
- `N` opens the overlay blank, with `begin()` instead of `open(id)`.

**Cards:**
- Fixed size.
- Content: title (bold, one line, elided); preview (up to 4 lines); first image as a thumbnail when present; a badge that shows "overdue", "today", a due date, or the age ("12d") for undated notes; tags (one line, elided).
- Colour: a tint from a fixed per-colour table, with a light and a dark variant chosen by `QStyleHints::colorScheme()`. With no colour, the palette's base.
- The selection is drawn with the palette highlight.

**Theme (D30):** follows Windows light/dark through the palette. Note HTML stores no colours (M2), so text follows the theme.

## Errors

- A `noite::Error` from a board action logs a warning, shows the message in the status line, and reloads the model, so the board never shows a state the store doesn't have.
- Overlay saves fail as capture saves do: the text is kept and `errorOccurred` reaches the status line.
- A thumbnail that fails to load is skipped; the card renders without it.

## Performance gate (Task 1)

1. A seed tool fills a data folder with 2,000 realistic notes: mixed lengths, about 10% with an image, a spread of due dates, colours and ages, and some done or trashed.
2. A probe (env-gated, like M2's `LatencyProbe`) measures tray/hotkey → board painted with the Today tab filled, cold (window not yet created), in a Release build.
3. **Pass:** p95 ≤ 300 ms over 20 opens, and private memory after closing the board back to ≤ 80 MB.
4. **If it fails:** try B6's fallback (keep the window hidden after the first open). If that still fails, stop and report to the user before any QML work.

## Testing

Qt Test, offscreen, a real `Store` on a temp dir, the registry pattern in `app/tests`.

- **`BoardModel`:** rows match `Store::list` per tab; search narrows the rows; `reload()` picks up a note created elsewhere.
- **Board actions:**
  - `D`, `Del` and `Ctrl+Shift+C` change the store, remove the card when it leaves the tab, and move the selection to the next card;
  - Reopen and Restore work in Completed and Trash;
  - `Ctrl+Z` reverses each action, clone included.
- **Keys:** `/` focuses search; `Esc` clears and then closes; arrows plus `Enter` open the selected card; `N` opens a blank overlay.
- **Overlay:**
  - opening loads the note's HTML;
  - edit then `Esc` saves, including inside the debounce;
  - `Ctrl+Enter` marks done;
  - emptying and closing deletes the note;
  - the board reloads on close.
- **`CaptureController::open(id)`:** opening doesn't save; existing due date and colour are reported; cycling the colour updates the store; `saved` fires on save.
- **`CardDelegate`:** `sizeHint` is fixed. Painting covers no image, missing image file, long title, overdue, and dark palette without crashing (a smoke test, not pixel comparisons).
- **App:** the board hotkey is registered; when it's taken, `hotkeyUnavailable` fires and the app keeps running; a capture saved while the board is open shows on the board.
- **Manual checklist:** a Board section in `docs/testing/manual-capture-checks.md` covering keyboard-only use, dark mode, scrolling 2,000 notes, and display scale.

## Out of scope (M3a)

- Bundles, filter chips and manual tag editing (M3b).
- Radar and morning review (M4).
- Settings UI for the board hotkey (M6).
