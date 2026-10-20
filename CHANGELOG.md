# Changelog

All notable changes to noite are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/sfadiga/noite/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/sfadiga/noite/releases/tag/v1.0.0
