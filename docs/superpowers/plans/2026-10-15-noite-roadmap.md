# noite v1 — Build Roadmap

**Spec:** [`docs/spec.md`](../../spec.md) (decisions D1–D30)

v1 is split into milestones. Each one produces software that works and can be tested on its own, and each gets its own detailed plan in this folder. Only M0 and M1 are detailed now: M0 is a go/no-go gate on the stack, and the details of M2 onwards (the Tauri command layer, window wiring) depend on its result.

| # | Milestone | Delivers | Spec | Depends on | Plan |
|---|---|---|---|---|---|
| M0 | Toolchain + hotkey latency spike | Installed toolchain; measured hotkey→editor-ready latency, keystroke loss, idle memory; go/no-go on Tauri 2 | D3, D5, D6, D14 | — | [m0](2026-10-15-m0-toolchain-and-hotkey-spike.md) |
| M1 | `noite-core` storage library | Rust crate, no UI: data dir + lock, schema + migrations, scratches, lifecycle, clone, bundles, board queries + FTS search, attachments + purge, settings, backup snapshots; Windows CI running `cargo test` | D4, D8–D13, D19, D20, D22, D26, D27 (lock half) | M0 Task 1 only (toolchain) | [m1](2026-10-15-m1-noite-core-storage.md) |
| M2 | Capture app | Tauri shell around `noite-core`: tray, single-instance focus, capture hotkey, hidden pre-warmed capture window, TipTap editor with autosave, discard-if-empty, paste handling (D21), `@date` parsing, capture-window keys, latency instrumentation | D5, D6, D7, D9 (parsing), D21, D27, D30 (capture keys) | M0 pass, M1 | later |
| M3 | Board | Board window + hotkey, tabs, card grid, bundle drag/expand, filter chips (Vitest), search box, keyboard nav, undo, theme | D17, D18, D19 (UI), D30 | M2 | later |
| M4 | Background jobs | Purge scheduler, backup scheduler + "Back up now", start-of-day notification, "Start with Windows" registry entry rewritten per launch, rotating logs | D4, D13, D16, D22, D29 | M2 | later |
| M5 | Setup wizard + Settings | 8-step wizard with hotkey conflict check, Settings window, re-run wizard, full tray menu, sync-folder warning shown to user | D24, D25 | M3, M4 | later |
| M6 | Release hardening | `tauri-driver` E2E (capture → board → done → trash → restore), 2,000-note perf benchmark gating CI, portable zip build in GitHub Actions, upgrade-path test (old data + new exe) | D5, D23, D26, D28 | M5 | later |

## Gates

- **After M0:** if the spike misses its latency or keystroke-loss gates, stop and choose a direction with the user before writing M2. M1 is not affected: `noite-core` has no Tauri dependency.
- **After M1:** the M2 plan defines the Tauri command API as a thin wrapper over the M1 `Store` methods.

## Decisions made while planning (not in the spec, confirm on review)

1. **Future-dated scratches** (spec D9 leaves them out): kept out of **Today**. In **All** they sort after the undated ones, earliest due date first.
2. **Bundle lifetime:** a bundle is created by dropping one card on another, and deleted automatically once no scratch of any status belongs to it.
3. **Title derivation** lives in Rust (`noite-core`), not Vitest as spec §9 lists, because the core already extracts plain text for search. One implementation, tested with `cargo test`.
4. **Attachment grace period:** the purge never deletes an attachment file younger than 24 h, even if nothing references it. This protects images pasted while the scratch's autosave is still pending.
5. **Sync-folder guard:** `noite-core` detects when `data\` sits inside a Google Drive, OneDrive or Dropbox folder. M5 shows the warning. This protects the D13 rule that the live database is never synced.
6. **Restore from backup** has no UI in v1 (the spec has none). M1 adds `backup::verify`; restoring is a manual copy, documented in M6.
