# noite v1 — Build Roadmap

**Spec:** [`docs/spec.md`](../../spec.md) (decisions D1–D30, revised 2026-10-16 for Qt)

v1 is split into milestones. Each one produces software that works and can be tested on its own, and each gets its own detailed plan in this folder.

**Stack (revised 2026-10-16):** Qt 6 Widgets + C++20. Tauri 2 was measured first and kept as a baseline ([Tauri spike](../../spikes/2026-10-hotkey-latency.md) vs [Qt spike](../../spikes/2026-10-qt-capture.md)): both well within the latency budget (warm p95 28 ms vs 7.6 ms; the timers stop at different points, so it's not a like-for-like ratio), and idle memory 176 MB vs 18.9 MB, which is the deciding number. The user writes C++, didn't want a browser engine, and wanted the fully rich editor.

| # | Milestone | Delivers | Spec | Depends on | Plan |
|---|---|---|---|---|---|
| M0 | Toolchain + Tauri latency spike | Tauri baseline numbers | D5 | — | [m0](2026-10-15-m0-toolchain-and-hotkey-spike.md) (done; superseded by M0b) |
| M0b | Qt Widgets spike | Qt numbers, editor capability tests, stack decision | D3, D5, D7, D14, D15 | — | [m0b](2026-10-16-m0b-qt-spike.md) (done) |
| M1 | `noite-core` C++ library | UI-free C++ library: data dir + lock, schema + migrations, scratches, lifecycle, clone, bundles, board queries + FTS search, attachments + purge, settings, backup snapshots; Windows CI running Qt Test | D4, D8–D13, D19, D20, D22, D26, D27 (lock half) | M0b | [m1](2026-10-16-m1-noite-core-cpp.md) (done; the [Rust version](2026-10-15-m1-noite-core-storage.md) is superseded) |
| M2 | Capture app | Qt app around `noite-core`: tray, single instance per data folder, capture hotkey (`Ctrl+Alt+J`), hidden pre-warmed capture window on the foreground window's monitor, the spike's editor (shortcuts, paste, checkboxes, links) hardened with the [review's deferred list](../../spikes/2026-10-qt-capture.md#review-fixes-whole-branch-review-2026-10-16) (rich-paste cleanup and image capture, message-window hotkey, IME input, single instance before hotkey registration), autosave, discard-if-empty, `@date` parsing, **idle first-summon mitigation**, latency instrumentation | D5, D6, D7, D9 (parsing), D21, D27, D30 (capture keys) | M1 | [m2](2026-10-17-m2-capture-app.md) |
| M3 | **Radar** (added 2026-10-17, the core promise) | Morning review (first unlock after day-start: overdue → due today → stale → undated oldest-first; Done / Snooze / Keep / Trash per note), stale resurfacing (undated + untouched N days, default 7), snooze = due date with quick picks, "Start with Windows" so the morning trigger actually fires, core query for the review order | D16, D31–D34, D4 (autostart) | M2 | later |
| M4 | Board | Board window + hotkey, tabs, card grid, bundle drag/expand, filter chips, search, keyboard nav, undo, theme. **Starts with a measured choice: Widgets vs one embedded QML view.** | D17, D18, D19 (UI), D30 | M3 | later |
| M5 | Background jobs | Purge scheduler, backup scheduler + "Back up now", rotating logs | D13, D22, D29 | M2 | later |
| M6 | Setup wizard + Settings | 8-step wizard with hotkey conflict check (now also asks for the stale-after-N-days setting), Settings window, re-run wizard, full tray menu, sync-folder warning shown to user | D24, D25, D33 | M4, M5 | later |
| M7 | Release hardening | End-to-end runs with the keystroke harness; 2,000-note and idle-summon performance checks gating CI; trimmed portable zip (~35 MB) built in GitHub Actions; upgrade-path test (old data + new exe) | D5, D23, D26, D28 | M6 | later |

## Decisions made while planning (not in the spec, confirm on review)

1. **Future-dated scratches** (spec D9 leaves them out): kept out of **Today**. In **All** they sort after the undated ones, earliest due date first.
2. **Bundle lifetime:** a bundle is created by dropping one card on another, and deleted automatically once no scratch of any status belongs to it.
3. **Title derivation** lives in `noite-core` (from the document's plain text), tested with Qt Test.
4. **Attachment grace period:** the purge never deletes an attachment file younger than 24 h, even if nothing references it. This protects images pasted while the scratch's autosave is still pending.
5. **Sync-folder guard:** `noite-core` detects when `data\` sits inside a Google Drive, OneDrive or Dropbox folder. M6 (setup wizard + settings) shows the warning. This protects the D13 rule that the live database is never synced.
6. **Restore from backup** has no UI in v1 (the spec has none). M1 adds a backup verify step; restoring is a manual copy, documented in M7.
