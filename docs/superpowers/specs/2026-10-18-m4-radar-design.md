# M4: the radar (morning review) — design

**Date:** 2026-10-18 · **Status:** approved in conversation · **Spec:** D16, D31–D34, D4 (autostart)

## Goal

Keep the core promise (D31): what you capture stays in front of you until you mark it done or trash it. Once a day, a review steps through what needs a decision (overdue, due today, stale) and you choose Done, Snooze, Keep or Trash for each. noite starts with Windows so the morning trigger actually fires.

## Decisions

| # | Decision | Why |
|---|---|---|
| R1 | The review **opens directly**, in front, on the first unlock after the day starts | The user's choice. This replaces D16's "notify, never steal focus". |
| R2 | Order: overdue (most overdue first) → due today (oldest first) → stale (longest untouched first) | Dates the user set come first, then what has rotted. This replaces D33's "stale on top". |
| R3 | The review shows only **actionable** notes: overdue, due today, stale | Keeps the morning short; the rest of the radar stays on the board. |
| R4 | **Keep** resets the stale clock (`modified_at = now`) | Otherwise a stale note comes back every morning. |
| R5 | "Start with Windows" is a checkable tray menu item until M6's Settings | On by default (D24). |
| R6 | The review counts as shown when it opens (or when the queue is empty); closing early doesn't reopen it that day | Once a day (D32). |

## Core (noite-core)

```cpp
// Settings (JSON; defaults)
int staleDays = 7;                      // validate(): 1-90, InvalidSetting otherwise

enum class ReviewReason { Overdue, DueToday, Stale };
struct ReviewItem {
    ScratchSummary note;
    ReviewReason reason;
    int days;                           // overdue: days late; stale: days untouched; due today: 0
};

QList<ReviewItem> Store::reviewQueue(QDate today, int staleDays) const;
void Store::touch(const ScratchId& id); // modified_at = now; NotFound; trashed notes rejected like other edits
std::optional<QDate> Store::lastReviewDay() const;
void Store::setLastReviewDay(QDate day);
```

- **Stale:** `status = 'active' AND due_date IS NULL AND modified_at <= now - staleDays * day`. `days` is the number of whole days since `modified_at`.
- **Review state:** `lastReviewDay` is stored in the `settings` table under its own key (`last_review_day`, ISO date). It is not part of `Settings`, so saving settings never overwrites it.
- **Other actions:** Snooze uses the existing `setDue(id, futureDate)` (D34); Done and Trash use the existing `markDone` and `trash`.

## Trigger (app)

`ReviewScheduler` makes the decision. Its inputs are a clock, the store, an `isUnlocked()` callback and `staleDays`/`dayStart`, so it is testable without Windows. Its rule:

```
check():
  if now < today's dayStart        → schedule the timer for dayStart, return
  if lastReviewDay == today        → return
  if !isUnlocked()                 → return
  setLastReviewDay(today)
  if reviewQueue(today) is empty   → return (no window)
  emit reviewDue()
```

`check()` runs:
- at app start;
- on session unlock (`WTSRegisterSessionNotification` on NativeHost's message window → `WM_WTSSESSION_CHANGE`/`WTS_SESSION_UNLOCK`);
- on resume (`WM_POWERBROADCAST`/`PBT_APMRESUMEAUTOMATIC`);
- from a single-shot timer set to the next `dayStart`.

`NativeHost` gains the signals `sessionUnlocked()` and `resumed()`. "Unlocked" is tracked from `WTS_SESSION_LOCK`/`UNLOCK` and starts as unlocked, since the app is running in the user's session.

## Start with Windows

- **When on:** every launch writes `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, value `noite` = `"<exe path>"`, plus ` --data "<dir>"` when the data folder isn't the default. Rewriting it each launch keeps it right if the portable folder moves (D4).
- **When off:** the value is deleted.
- **Only for the portable folder's own data** (final review; the author's call): a run with `--data` (a test folder, say) never writes or deletes the value, and the tray item is disabled. Relative or drive-root `--data` paths therefore never reach the registry.
- **Implementation:** behind `AutostartRegistry` (`bool set(QString command)`, `bool remove()`, `std::optional<QString> current()`). The app uses a `QSettings(NativeFormat)` implementation; tests use a fake.
- **Tray:** a checkable "Start with Windows" item. It saves `Settings::startWithWindows` and applies it at once.

## Review window

```
┌─ Morning review ─────────────────────── 3 / 9 ─┐
│  OVERDUE · 2 days                              │
│ ┌────────────────────────────────────────────┐ │
│ │ (the note, read-only, colour tint)         │ │
│ └────────────────────────────────────────────┘ │
│ [D Done] [S Snooze ▾] [K Keep] [Del Trash]     │
│ Enter: edit · ←: back · Esc: finish later      │
└────────────────────────────────────────────────┘
```

- **Window:** a normal top-level window centred on the active monitor, raised and activated (R1). It is created when needed and deleted on close, like the board.
- **Header:** the reason in caps (`OVERDUE · 2 days`, `DUE TODAY`, `STALE · 9 days untouched`) and the position `n / total`.
- **Body:** a read-only `NoteEdit` with the note's HTML, attachments loaded and the colour tint as background.
- **Keys and buttons:**
  - `D` Done, `K` Keep, `Del` Trash, then advance.
  - `S` opens the snooze menu: `1` Tomorrow, `2` Next Monday (today + 7 if today is Monday), `3` Pick… (calendar). Picking sets the due date and advances.
  - `Enter` edits in place: the read-only view becomes editable with the board's editor behaviour (autosave through a `CaptureController`); `Esc` or `Ctrl+Enter` returns to review mode on the same note.
  - `←` goes back one note and undoes the action taken on it.
  - `Ctrl+Z` undoes the last action and returns to that note.
- **End:** "Done 3 · Snoozed 2 · Kept 3 · Trashed 1" with "Open board" and "Close".
- **`Esc` mid-review:** closes; untouched notes stay as they are.
- **Tray menu:** gains "Morning review", which opens a fresh queue any time (it doesn't touch `lastReviewDay`).
- **Errors:** a store error shows in the status line and the note stays current. A note deleted elsewhere (`NotFound`) is skipped with a status line.

## Errors

| Case | Behaviour |
|---|---|
| Registry write fails | Logged; the tray item shows the real state (`current()`). |
| WTS registration fails | Logged; the review still runs on start, resume and the timer. |
| Store error in the review | Status line; the note stays; nothing is lost. |

## Testing (Qt Test, test-first)

Core:
- `reviewQueue`: order and reasons over a fixed set (overdue ×2, due today, stale ×2, fresh undated, upcoming, done, trashed). Days computed correctly. Stale boundary at exactly N days.
- `touch` resets the stale clock.
- `lastReviewDay` round-trips and survives `saveSettings`.
- `staleDays` validation.

App:
- Scheduler: before `dayStart` → no review and the timer set; after → `reviewDue` once; same day → none; locked → none, then unlock → review; empty queue → marked, no signal; the next day → review again.
- Autostart: on writes the quoted command (with `--data` only for a non-default folder); off removes it; the tray toggle saves the setting.
- Review window:
  - each key acts and advances;
  - snooze dates (Monday edge case);
  - `←` restores; `Ctrl+Z`;
  - summary counts;
  - `Esc` partway leaves the rest untouched;
  - edit in place returns to review;
  - a store error keeps the note;
  - `NotFound` skips.
- App: a scheduler `reviewDue` opens the review window in front; the tray "Morning review" opens one.

## Spec edits (spec.md)

- **D16:** the start-of-day review opens directly on the first unlock after `dayStart` (R1, R6).
- **D32:** order and scope as in R2 and R3; the open question is closed.
- **D33:** stale notes come after due today (R2); Keep resets the stale clock (R4).
- **D25/tray:** "Morning review" and "Start with Windows" items.
