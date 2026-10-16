# noite

A note-taking app for busy people who get things done. Press a hotkey, write, and it's there tomorrow.

> **Status: design and spikes, no app yet.** The spec is written, and two prototype spikes have settled the stack. The product code starts with milestone M1. *noite* is a working name.

## What it will do (v1)

- **Instant capture:** a global hotkey (default `Ctrl+Alt+J`) opens a blank scratch note immediately. The prototype took about 7 ms warm and up to 100 ms on the first press after a long idle; M2 has a fix planned for the idle case. It saves as you type; `Esc` hides it.
- **Rich notes:**
  - markdown typing shortcuts (`#`, `-`, `[]`, `**bold**`, `[link](url)`),
  - rich paste from browsers and Word, images, tables.
- **A board:** Today / All / Completed / Trash tabs, bundles, filter chips by tag, colour or due date, and full-text search.
- **A start-of-day review:** overdue, due today, then the oldest notes first.
- **Done and trash:** finished notes go to Completed; trash is recoverable for a window you set (default 30 days).
- **Portable and offline:** unzip and run, with everything in a `data\` folder next to the exe and no network code. Backups are snapshots to a folder you choose, such as your Google Drive folder.

Planned for v2: audio notes with offline speech-to-text, merging and splitting notes, Linux and macOS.

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
| [Roadmap](docs/superpowers/plans/2026-10-15-noite-roadmap.md) | milestones M0–M6 |
| [Spike findings](docs/spikes/) | measurements and lessons from the prototypes |

## Repository layout

```
docs/                    requirements, spec, plans, spike findings
spikes/hotkey-latency/   Tauri 2 prototype + keystroke latency harness (PowerShell)
spikes/qt-capture/       Qt 6 Widgets prototype + editor capability tests (Qt Test)
```

## Building the Qt spike (Windows)

Requirements:
- Visual Studio Build Tools with the C++ workload. It includes CMake and Ninja.
- Qt 6.10.3 for `msvc2022_64`, for example via [aqtinstall](https://github.com/miurahr/aqtinstall):

```powershell
python -m pip install --user aqtinstall
python -m aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -O "$env:USERPROFILE\Qt"
```

Build and run the tests:

```powershell
powershell -File spikes\qt-capture\build.ps1 -Config Release -Test   # build, deploy, run the editor tests offscreen
spikes\qt-capture\build\Release\qt-capture.exe                       # then press Ctrl+Alt+J
```

The latency harness (`spikes/hotkey-latency/run-trials.ps1`) **sends real keystrokes and takes focus**. Only run it when you're away from the keyboard.

## License

[MIT](LICENSE). Qt is used under the LGPL v3, linked dynamically.
