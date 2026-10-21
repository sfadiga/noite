# Changelog

All notable changes to noite are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- On the board, `Ctrl+1` to `Ctrl+4` switch to Today, All, Completed and Trash.
- An icon: an 8-bit crescent moon with blue stars, on the exe, every window and the tray.
- Export a note, with its images, to a single HTML file or a PDF: `Ctrl+E` on the board or in an open note, or **Export…** in the card menu.

### Changed

- The morning review now opens every morning: when nothing is overdue, due today or stale, it goes over your undated notes, oldest first.
- Each morning-review check writes a line to the log (what triggered it and what it decided), so a review that didn't open can be explained.
- CI now runs an end-to-end test on every push: a note is captured with the hotkey, then marked done, trashed and restored on the board, with real keystrokes.

- CI now checks the performance budget on every push: opening the board with 2,000 notes and opening a capture.
- The latency log (`NOITE_LATENCY_CSV`) records each step of opening a capture (shown, activated, painted) and its page faults.

## [1.0.1] - 2026-10-20

### Added

- The capture window can be resized from any edge or corner; it keeps its size until noite quits.

### Changed

- The capture window closes only on `Esc` (or `Ctrl+Enter`): clicking another window leaves it open on top. Opening or acting on the same note from the board or the review saves and closes it first.

### Fixed

- Pasting plain text that looks like markdown no longer cuts off at a `<placeholder>` such as `<my-feature>`, which left empty numbered lines in place of the rest of the paste.
- Pasted markdown is spaced like typed text: no gap between lines, and its blank lines stay.

## [1.0.0] - 2026-10-19

### Added

- Instant capture: a global hotkey opens a quick note with rich text, pasted images and files, `@dates` and tags.
- The board: tabs, search, filter chips, bundles and undo.
- The morning review: overdue, due today and stale notes, each with Done, Snooze, Keep or Trash.
- Backups to a folder or to Google Drive, trash purge, logs and crash reports.
- First-run setup and a Settings dialog.
- A portable single exe: no installer, and your data stays next to it.

[Unreleased]: https://github.com/sfadiga/noite/compare/v1.0.1...HEAD
[1.0.1]: https://github.com/sfadiga/noite/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/sfadiga/noite/releases/tag/v1.0.0
