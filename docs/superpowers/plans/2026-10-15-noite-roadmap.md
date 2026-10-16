# noite v1 — Build Roadmap

**Spec:** [`docs/spec.md`](../../spec.md) (decisions D1–D30, revised 2026-10-16 for Qt)

v1 is split into milestones. Each one produces software that works and can be tested on its own, and each gets its own detailed plan in this folder.

**Stack (revised 2026-10-16):** Qt 6 Widgets + C++20. Tauri 2 was measured first and kept as a baseline ([Tauri spike](../../spikes/2026-10-hotkey-latency.md) vs [Qt spike](../../spikes/2026-10-qt-capture.md)): warm p95 28 ms vs 7.6 ms, idle memory 176 MB vs 18.9 MB. The user writes C++, didn't want a browser engine, and wanted the fully rich editor.

| # | Milestone | Delivers | Spec | Depends on | Plan |
|---|---|---|---|---|---|
| M0 | Toolchain + Tauri latency spike | Tauri baseline numbers | D5 | — | [m0](2026-10-15-m0-toolchain-and-hotkey-spike.md) (done; superseded by M0b) |
| M0b | Qt Widgets spike | Qt numbers, editor capability tests, stack decision | D3, D5, D7, D14, D15 | — | [m0b](2026-10-16-m0b-qt-spike.md) (done) |
| M1 | `noite-core` C++ library | UI-free C++ library: data dir + lock, schema + migrations, scratches, lifecycle, clone, bundles, board queries + FTS search, attachments + purge, settings, backup snapshots; Windows CI running Qt Test | D4, D8–D13, D19, D20, D22, D26, D27 (lock half) | M0b | **to be rewritten in C++.** The [Rust version](2026-10-15-m1-noite-core-storage.md) is superseded, but its schema, rules and test cases carry over. |
| M2 | Capture app | Qt app around `noite-core`: tray, single instance per data folder, capture hotkey (`Ctrl+Alt+J`), hidden pre-warmed capture window on the foreground window's monitor, the spike's editor (shortcuts, paste, checkboxes, links) hardened, autosave, discard-if-empty, `@date` parsing, **idle first-summon mitigation**, latency instrumentation | D5, D6, D7, D9 (parsing), D21, D27, D30 (capture keys) | M1 | later |
| M3 | Board | Board window + hotkey, tabs, card grid, bundle drag/expand, filter chips, search, keyboard nav, undo, theme. **Starts with a measured choice: Widgets vs one embedded QML view.** | D17, D18, D19 (UI), D30 | M2 | later |
| M4 | Background jobs | Purge scheduler, backup scheduler + "Back up now", start-of-day notification, "Start with Windows" registry entry rewritten per launch, rotating logs | D4, D13, D16, D22, D29 | M2 | later |
| M5 | Setup wizard + Settings | 8-step wizard with hotkey conflict check, Settings window, re-run wizard, full tray menu, sync-folder warning shown to user | D24, D25 | M3, M4 | later |
| M6 | Release hardening | End-to-end runs with the keystroke harness; 2,000-note and idle-summon performance checks gating CI; trimmed portable zip (~35 MB) built in GitHub Actions; upgrade-path test (old data + new exe) | D5, D23, D26, D28 | M5 | later |

## Decisions made while planning (not in the spec, confirm on review)

1. **Future-dated scratches** (spec D9 leaves them out): kept out of **Today**. In **All** they sort after the undated ones, earliest due date first.
2. **Bundle lifetime:** a bundle is created by dropping one card on another, and deleted automatically once no scratch of any status belongs to it.
3. **Title derivation** lives in `noite-core` (from the document's plain text), tested with Qt Test.
4. **Attachment grace period:** the purge never deletes an attachment file younger than 24 h, even if nothing references it. This protects images pasted while the scratch's autosave is still pending.
5. **Sync-folder guard:** `noite-core` detects when `data\` sits inside a Google Drive, OneDrive or Dropbox folder. M5 shows the warning. This protects the D13 rule that the live database is never synced.
6. **Restore from backup** has no UI in v1 (the spec has none). M1 adds a backup verify step; restoring is a manual copy, documented in M6.
