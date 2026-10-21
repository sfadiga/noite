# M8c: the Deferred tab — design

**Date:** 2026-10-21 · **Status:** agreed in a grilling session (Q14–Q19) · **Spec:** amends D11 (lifecycle), D17 (tabs), D30 (keys), D31 (radar rule), D32 (review)

## Goal

A place for notes you want to keep but not be reminded of: deferred notes raise no alarm (no Today, no review, no stale resurfacing) until you bring them back.

## Decisions

| # | Decision | Why |
|---|---|---|
| F1 | A new status **`deferred`**: `active → deferred → active`. Not a flag on active notes. | One place decides what alarms: `status = 'active'`. Every radar query already filters on it. |
| F2 | Deferred notes are out of Today, All, the morning review (including the undated fallback), stale resurfacing and the review counts. Their due date is kept but ignored, and applies again once the note is brought back. | "Raise no alarms." |
| F3 | Tabs: Today · All · Completed · **Deferred** · Trash. `Ctrl+4` Deferred, `Ctrl+5` Trash; the e2e run's restore step moves to `Ctrl+5`. | Trash stays last. |
| F4 | `L` ("later") on the board defers the selected active note; on the Deferred tab it brings the note back. The card menu has **Defer** / **Bring back**. Both are undoable (`Ctrl+Z`). | One key, the D/Del pattern. |
| F5 | The morning review gets `L  Defer` next to Done / Snooze / Keep / Trash. Left undoes it. The summary counts it: `Done n · Snoozed n · Kept n · Deferred n · Trashed n`. | Deferring is a morning decision. |
| F6 | From Deferred: `D` marks done (undo returns it to Deferred); `Del` trashes it, and restoring returns it to Deferred (`trashed_from = 'deferred'`). Editing doesn't change the status. The purge never touches deferred notes. | The usual exits stay open. |
| F7 | Only active notes can be deferred: Defer is hidden on Completed and Trash, and `L` does nothing there. | Done notes raise nothing already. |
| F8 | Deferred tab order: oldest created first. | Same as an undated All. |

## Schema: migration v3

SQLite can't change a `CHECK` in place, so `scratches` is rebuilt:
- `status IN ('active','done','trashed','deferred')`;
- `trashed_from IN ('active','done','deferred')`.

Migrations run in one transaction with `foreign_keys = ON`, and the `DROP TABLE` would cascade-delete `scratch_tags`. So:
1. The migration copies the tags to a temporary table.
2. It rebuilds `scratches` and recreates its index.
3. It puts the tags back.

`tst_upgrade`'s golden v1 and v2 folders check that the tags survive.

## Core

- `Status::Deferred` (`"deferred"`), `Tab::Deferred` (before `Trash`); `TabCounts` holds 5 counts.
- `Store::defer(id)` (active → deferred) and `Store::bringBack(id)` (deferred → active). Both set `modified_at` and throw `InvalidTransition` from any other status.
- `markDone`: from active or deferred. `trash`: from active, done or deferred. `restore` → `trashed_from` (unchanged).

## App

- `TabStrip`: five tabs.
- `BoardWindow`: `Ctrl+1`…`Ctrl+5`; `L` and the menu item, using `deferOrBringBack()`; status lines "Deferred · Ctrl+Z to undo" / "Brought back".
- `ReviewWindow`: `L` and a `L  Defer` button; the outcome kind and summary.
- Docs: spec D11/D17/D30/D31/D32 and D36 (this design), README, CHANGELOG; `tools/e2e/run-e2e.ps1` step 4 `^5`.

## Testing

- **Core:**
  - **Lifecycle:** defer and bring back; defer only from active; done and trash from deferred; restore back to deferred; `bringBack` only from deferred.
  - **Board:** the Deferred tab lists only deferred notes; they are not in Today or All; counts at index 3, Trash at 4.
  - **Review:** an overdue deferred note is in neither the queue nor the fallback.
  - **Upgrade:** the goldens (tags survive).
- **App:**
  - `L` defers, and `Ctrl+Z` undoes it;
  - `L` on Deferred brings the note back;
  - `L` does nothing on Completed;
  - `D` on a deferred note, then undo, returns it to Deferred;
  - `Ctrl+4` / `Ctrl+5`;
  - the review's `L` with its summary and undo.
