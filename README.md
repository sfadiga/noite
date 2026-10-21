# noite

<img src="packaging/icon/noite-128.png" width="128" height="128" alt="noite's icon: an 8-bit crescent moon and blue stars on a night sky" />

A note-taking app for busy people who get things done. Press a hotkey, write, and it's there tomorrow.

> **Status: v1 features are built (M1–M6); 1.0.0 is the first release, shipped as a single `noite.exe` (M7).** *noite* is a working name.

<img width="1002" height="732" alt="image" src="https://github.com/user-attachments/assets/8369d8ed-901c-44e8-a341-837aec25f573" />

<img width="1002" height="732" alt="image" src="https://github.com/user-attachments/assets/eadfb360-36ce-4958-9bca-cccd3480384c" />

## What it does (v1)

- **Instant capture:** a global hotkey (default `Ctrl+Alt+J`) opens a blank scratch note immediately. About 8 ms warm; the first press after a long idle takes about 80–120 ms, most of it Windows waking up the display path. It saves as you type; `Esc` hides it.
- **Rich notes:**
  - markdown typing shortcuts (`#`, `-`, `[]`, `**bold**`, `[link](url)`),
  - rich paste from browsers and Word, images, tables.
- **A board:** Today / All / Completed / Trash tabs (`Ctrl+1`–`Ctrl+4`), bundles, filter chips by tag, colour or due date, and full-text search.
- **Export:** a note to one HTML file or a PDF, images included (`Ctrl+E` or the card menu's **Export…**).
- **A start-of-day review:** overdue, due today, then the oldest notes first.
- **Done and trash:** finished notes go to Completed; trash is recoverable for a window you set (default 30 days).
- **Portable and offline:** unzip and run, with everything in a `data\` folder next to the exe and no network code. Backups are snapshots to a folder you choose, such as your Google Drive folder.

Planned for v2: audio notes with offline speech-to-text, merging and splitting notes, Linux and macOS.

## Download

Get the latest `noite-<version>-win64.zip` from the [Releases page](https://github.com/sfadiga/noite/releases/latest). It is a zip with one `noite.exe`: no DLLs and no installer.

1. Unzip it anywhere you can write to (for example `Documents\noite`), not Program Files.
2. Run `noite.exe`. The first run opens a short setup.
3. noite isn't code-signed, so Windows may say "Windows protected your PC". Click **More info**, then **Run anyway**.

`SHA256SUMS.txt` has the checksums of the zips. To update, quit noite (tray icon, Quit) and replace `noite.exe` with the new one, keeping the `data\` folder. To restore a backup, quit noite and move `data\noite.db`, and `data\noite.db-wal` and `data\noite.db-shm` if present, into a folder of your own (your copy of the old data). Then copy a snapshot from `<your backup folder>\noite-backups\<id>\snapshots\` into `data\`, rename it `noite.db`, and start noite. The zip's `README.txt` has the same steps.

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

Everyday loop: `tools\dev.ps1` runs steps in order and stops at the first failure (`-Config Debug` for a debug build).

```powershell
powershell -File tools\dev.ps1 build run          # build, then start it on build\Release\dev-data (not your notes)
powershell -File tools\dev.ps1 ci                 # clean + build + test
powershell -File tools\dev.ps1 deploy run-dist    # refresh dist\noite and start it on its own data (your notes)
```

Steps: `clean` (build folder only, never `dist\`), `build`, `test`, `deploy`, `run` (`-Data <dir>` for another folder), `run-dist`, `ci`. The scripts behind them:

```powershell
powershell -File tools\build.ps1 -Config Release -Test    # configure, build, run the suites offscreen
powershell -File tools\deploy.ps1 -Config Release         # dist\noite: exe + Qt DLLs + app-local VC runtime (45 MB budget)
powershell -File tools\package.ps1 -Version 0.1.0         # dist\noite-0.1.0-win64.zip, the PDB zip, SHA256SUMS.txt
```

`tools\package.ps1 -DryRun` runs the same checks without a version and writes to `dist\dry-run\`; `-SelfTest` tests the script itself. Local builds are dynamic (Qt DLLs next to the exe), which is what you want for development.

The released single `noite.exe` is built only in CI (the `single-exe` job): `-DNOITE_STATIC=ON` against a static Qt from [sfadiga/qt-static-windows](https://github.com/sfadiga/qt-static-windows), pinned by tag and SHA-256 in `tools\qt-static.json`. `tools\package.ps1 -Static` packages it, running `noite.exe --self-check` and refusing a Qt, VC runtime or bundled-ICU dependency. CI runs both flavours on every push: the dynamic build, tests, `deploy.ps1` and `package.ps1 -DryRun`, and the static build with `package.ps1 -Static -DryRun`.

The end-to-end test (`tools\e2e\run-e2e.ps1`, after `tools\deploy.ps1`) and the keystroke latency harness (`spikes/hotkey-latency/run-trials.ps1`) **send real keystrokes and take focus**. Only run them when you're away from the keyboard. CI runs the end-to-end test on every push; it needs Python 3 on the PATH.

### Releasing

1.0.0 is already prepared: `version.txt` is 1.0.0 and `CHANGELOG.md` has the 1.0.0 section. For 1.0.0 skip steps 1 and 2; check the CHANGELOG date (step 3), merge, wait for a green `single-exe` job on `main`, then tag.

1. Write the release's changes under `[Unreleased]` in `CHANGELOG.md`.
2. Run `tools\bump-version.ps1 X.Y.Z`. It updates `version.txt` and turns `[Unreleased]` into the `X.Y.Z` section.
3. Check that the date on the `X.Y.Z` CHANGELOG heading is the day you are releasing; fix it before tagging.
4. Open a PR with that, wait for CI to be green, and merge it to `main`.
5. Pull, then tag `main`'s HEAD and push the tag: `git tag vX.Y.Z` then `git push origin vX.Y.Z`.
6. CI builds the single exe and creates a **draft** release with the zip, the PDB zip and `SHA256SUMS.txt`, and the notes from the CHANGELOG section. It fails if the tag differs from `version.txt`, if the CHANGELOG has no entry for the version, or if a release for that tag already exists. In that case delete the draft and re-tag, or re-run the release job within 7 days (the `noite-single-exe` build artifact expires after that).
7. Check the draft on GitHub and publish.

### Schema changes

Before bumping the database schema to vN+1, run `noite_golden` for vN with the current build and commit the result as `core/tests/golden/vN`. `tst_upgrade` opens every golden folder with the current build; that is the guarantee that a newer exe opens an older data folder. Never regenerate a committed folder.

## License

[MIT](LICENSE). Qt is used under the LGPL v3, unmodified and linked statically into the released `noite.exe`; noite's source is public, so you can relink it against your own Qt build. The Qt source for each release is attached to the [qt-static-windows](https://github.com/sfadiga/qt-static-windows) release named in `tools\qt-static.json`.
