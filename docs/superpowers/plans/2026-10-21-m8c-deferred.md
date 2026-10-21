# M8c Deferred Tab Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Deferred status and tab whose notes raise no alarms until brought back.

**Spec:** `docs/superpowers/specs/2026-10-21-m8c-deferred-design.md` (F1–F8)

## Global Constraints

- Migration v3 must keep `scratch_tags` (cascade on `DROP TABLE`); `tst_upgrade` proves it.
- Scripts under `tools/` stay ASCII-only. Edit sources with the Edit tool.
- Build `tools\build.ps1 -Config Release`; tests `tools\run-tests.ps1 -Config Release` → `TOTAL failures: 0`.

## Review Focus

1. **Tags through v3:** `tst_upgrade.contentMatchesManifest` compares the tags of every golden note.
2. **Undo of Done on a deferred note** must return it to Deferred, not Active.
3. **`Tab` index users:** `TabStrip`, `TabCounts`, `Ctrl+digit` and the e2e restore key all shift with the new tab.

### Task 1: Core (schema v3, status, lifecycle, board, review)

- [ ] Failing tests:
  - `tst_lifecycle`: `deferAndBringBack`, `deferOnlyFromActive`, `doneAndTrashFromDeferred`.
  - `tst_board`: `deferredTabListsOnlyDeferred`, `tabCountsIncludeDeferred`.
  - `tst_review`: `deferredNotesRaiseNothing`.
- [ ] `schema_v3.sql`, `kSchemaV3` (CMake + `schema.h.in` + `kMigrations`); `Status::Deferred`, `Tab::Deferred`, `TabCounts[5]`; `defer`/`bringBack`; the `markDone`/`trash` sources; `tabWhere`/`tabOrder`/`tabCounts`.
- [ ] Green, including `tst_upgrade`. Commit `feat(core): deferred status (schema v3)`.

### Task 2: Board and review

- [ ] Failing tests:
  - `tst_board_window`: `lDefersAndUndo`, `lOnDeferredBringsBack`, `lIgnoredOnCompleted`, `doneOnDeferredUndoReturnsToDeferred`, `ctrlDigitSwitchesTab` (5 tabs).
  - `tst_review_window`: `lDefersAndCounts`, `backUndoesDefer`.
- [ ] Implement; green. Commit `feat(board): Deferred tab, L to defer or bring back; review L Defer`.

### Task 3: e2e, docs

- [ ] `run-e2e.ps1` step 4 `^5` (and its header comment); spec, README, CHANGELOG. Commit; push; PR; CI `E2E PASS`.
