# noite

A note-taking app for busy people who get things done. Press a hotkey, write, and it's there tomorrow.

> **Status: v1 features are built (M1–M6); the release pipeline (M7a) is new.** Performance work and a single-exe build come next. *noite* is a working name.

## What it does (v1)

- **Instant capture:** a global hotkey (default `Ctrl+Alt+J`) opens a blank scratch note immediately. About 7 ms warm and up to 100 ms on the first press after a long idle; M7b targets the idle case. It saves as you type; `Esc` hides it.
- **Rich notes:**
  - markdown typing shortcuts (`#`, `-`, `[]`, `**bold**`, `[link](url)`),
  - rich paste from browsers and Word, images, tables.
- **A board:** Today / All / Completed / Trash tabs, bundles, filter chips by tag, colour or due date, and full-text search.
- **A start-of-day review:** overdue, due today, then the oldest notes first.
- **Done and trash:** finished notes go to Completed; trash is recoverable for a window you set (default 30 days).
- **Portable and offline:** unzip and run, with everything in a `data\` folder next to the exe and no network code. Backups are snapshots to a folder you choose, such as your Google Drive folder.

Planned for v2: audio notes with offline speech-to-text, merging and splitting notes, Linux and macOS.

## Download

Get the latest `noite-<version>-win64.zip` from the [Releases page](https://github.com/sfadiga/noite/releases/latest).

1. Unzip it anywhere you can write to (for example `Documents\noite`), not Program Files.
2. Run `noite.exe`. The first run opens a short setup.
3. noite isn't code-signed, so Windows may say "Windows protected your PC". Click **More info**, then **Run anyway**.

`SHA256SUMS.txt` has the checksums of the zips. To update, quit noite (tray icon, Quit) and replace the files with the new ones, keeping the `data\` folder. The zip's `README.txt` also explains how to restore a backup.

## Stack

**Qt 6 Widgets + C++20**, one native process with no WebView, built with CMake and MSVC. The editor is `QTextEdit` with hand-written markdown shortcuts. Notes are stored as Qt rich-text HTML in SQLite, with images in a content-addressed attachments folder.

Why Qt and not Tauri: [Tauri spike](docs/spikes/2026-10-hotkey-latency.md) vs [Qt spike](docs/spikes/2026-10-qt-capture.md).

| | Tauri 2 + WebView2 | Qt 6 Widgets |
|---|---|---|
| Hotkey → typing (warm p95) | 28 ms | 7.6 ms |
| Idle memory | 176 MB | 18.9 MB |

## Documents

| | |
|---|---|
| [Requirements](docs/requirements.md) | the original 24 requirements |
| [Spec](docs/spec.md) | v1 design decisions (D1–D30) and requirements traceability |
| [Roadmap](docs/superpowers/plans/2026-10-15-noite-roadmap.md) | milestones M0–M7 (M7 is split into M7a–M7d) |
| [Spike findings](docs/spikes/) | measurements and lessons from the prototypes |

## Repository layout

```
core/                    noite-core library and its tests (golden data folders in core/tests/golden)
app/                     the Qt Widgets app
tools/                   build, test, deploy and package scripts
packaging/               files that go into the release zip (README, licences)
docs/                    requirements, spec, plans, spike findings
spikes/hotkey-latency/   Tauri 2 prototype + keystroke latency harness (PowerShell)
spikes/qt-capture/       Qt 6 Widgets prototype + editor capability tests (Qt Test)
```

## Build

Requirements:
- Visual Studio Build Tools with the C++ workload. It includes CMake and Ninja.
- Qt 6.10.3 for `msvc2022_64`, for example via [aqtinstall](https://github.com/miurahr/aqtinstall):

```powershell
python -m pip install --user aqtinstall
python -m aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -O "$env:USERPROFILE\Qt"
```

```powershell
powershell -File tools\build.ps1 -Config Release -Test    # configure, build, run the suites offscreen
powershell -File tools\deploy.ps1 -Config Release         # dist\noite: exe + Qt DLLs + app-local VC runtime (45 MB budget)
powershell -File tools\package.ps1 -Version 0.1.0         # dist\noite-0.1.0-win64.zip, the PDB zip, SHA256SUMS.txt
```

`tools\package.ps1 -DryRun` runs the same checks without a version and writes to `dist\dry-run\`; `-SelfTest` tests the script itself. CI runs the build, the tests, `deploy.ps1` and `package.ps1 -DryRun` on every push.

The keystroke latency harness (`spikes/hotkey-latency/run-trials.ps1`) **sends real keystrokes and takes focus**. Only run it when you're away from the keyboard.

### Releasing

1. Set the new version in `project(noite VERSION x.y.z ...)` in the top-level `CMakeLists.txt` and commit.
2. Tag it and push the tag: `git tag vX.Y.Z` then `git push origin vX.Y.Z`.
3. CI builds, tests and packages, then creates a **draft** release. It fails if the tag differs from the project version.
4. Open the draft on GitHub, edit the generated notes, and publish.

### Schema changes

Before bumping the database schema to vN+1, run `noite_golden` for vN with the current build and commit the result as `core/tests/golden/vN`. `tst_upgrade` opens every golden folder with the current build; that is the guarantee that a newer exe opens an older data folder. Never regenerate a committed folder.

## License

[MIT](LICENSE). Qt is used under the LGPL v3, linked dynamically.
