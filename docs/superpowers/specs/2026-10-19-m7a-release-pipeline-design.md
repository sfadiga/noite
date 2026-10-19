# M7a: release pipeline — design

**Date:** 2026-10-19 · **Status:** approved in conversation · **Spec:** D23 (distribution), D26 (updates, migrations), D28 (CI)

M7 was split into four sub-projects, each with its own spec, plan and build:
- **M7a** (this one): the release pipeline.
- **M7b:** performance — the idle first summon and the perf gates.
- **M7c:** the end-to-end keystroke harness.
- **M7d:** a single-exe static build.

M7a comes first because M7b and M7d publish through it.

## Goal

A repeatable way to hand friends a portable zip:
- a versioned build, packaged and published by CI from the very binaries that passed the tests;
- PDBs kept, so a friend's crash dump can be read;
- the licence notices the Qt LGPL requires;
- a test proving a newer exe opens an older data folder;
- a README that tells a friend how to install, update and restore.

There is no code signing (D23).

## Decisions

| # | Decision | Why |
|---|---|---|
| P1 | A `v0.x.y` tag makes CI build, test and package, then create a **draft** GitHub Release that the author publishes by hand | The user's choice: a last look before friends see it. |
| P2 | The version source is `project(noite VERSION x.y.z)`; `git describe` adds `-dev+<sha>[.dirty]` off a tag; the release job fails when the tag ≠ the project version | One source, and a release can't ship a stale number. |
| P3 | A release job inside `ci.yml` (`needs: build`), shipping the build job's artifact | The user's choice (A): the tested binaries are the shipped ones. |
| P4 | Upgrade guarantee = committed **golden data folders**, one per schema version, opened by the current build in `tst_upgrade` | The user's choice: cheap, deterministic, runs on every push. |
| P5 | `deploy.ps1` and `tools/package.ps1 -DryRun` run on every push | Packaging and size-budget breaks show up before release day. |

## Version

- **CMake:** `project(noite VERSION 0.1.0 LANGUAGES C CXX)`. At configure time, `git describe --tags --match "v*" --dirty --always` produces `noite_version.h` with `NOITE_VERSION` (string) and `NOITE_VERSION_MAJOR/MINOR/PATCH`:
  - exactly on tag `v0.1.0` with a clean tree → `0.1.0`;
  - otherwise → `0.1.0-dev+<short sha>`, plus `.dirty` when modified;
  - git missing or failing → `0.1.0-dev`.

  Configure re-runs when HEAD moves (`CMAKE_CONFIGURE_DEPENDS` on `.git/HEAD` and the ref it points to, when present).
- **Windows version resource** (`app/src/noite.rc`, from a `.rc.in` template): FileVersion/ProductVersion `x.y.z.0`, ProductName `noite`, FileDescription `noite — notes that stay on your radar`, LegalCopyright `MIT licence`, OriginalFilename `noite.exe`.
- **In the app:**
  - `QCoreApplication::setApplicationVersion(NOITE_VERSION)`;
  - the startup log line `noite <version> started · data … · Qt … · Windows …`;
  - the tray tooltip `noite <version>`;
  - a Settings → System label `noite <version>`.

## Release assets (per tag)

| File | Contents |
|---|---|
| `noite-<v>-win64.zip` | Top folder `noite\`: <ul><li>what `deploy.ps1` produces (exe, Qt DLLs and plugins, app-local VC runtime)</li><li>`README.txt`, `LICENSE.txt` (MIT), `THIRD-PARTY.txt`</li><li>`licenses\LGPL-3.0.txt`, `licenses\GPL-3.0.txt`</li></ul> No `data\`, no `*.pdb`. |
| `noite-<v>-win64-pdb.zip` | `noite.pdb` |
| `SHA256SUMS.txt` | SHA-256 of both zips, `sha256sum` format |

- **Release notes:** `gh release create --generate-notes` (commits since the previous tag); edited by hand before publishing.
- **`THIRD-PARTY.txt`:**
  - Qt 6.10.3, © The Qt Company, used under the LGPLv3, dynamically linked, unmodified; source at https://download.qt.io/archive/qt/6.10/6.10.3/single/; licence texts in `licenses\`.
  - SQLite: public domain.
  - Microsoft Visual C++ runtime: redistributable under the Visual Studio licence.
- **Size:** the folder budget stays at 45 MB (`deploy.ps1 -MaxMB 45`). The release job prints the zip sizes.

### README.txt (in the zip)

```
noite <version> — notes that stay on your radar

Install
  Unzip anywhere you can write to (e.g. Documents\noite) — not Program Files.
  Run noite.exe. The first run opens a short setup.

Windows protected your PC?
  noite isn't code-signed. Click "More info", then "Run anyway".

Update
  Quit noite (tray icon → Quit), then replace the files with the new ones.
  Keep the data folder. A newer noite backs up the database before it changes its format.

Restore a backup
  Quit noite. Copy a snapshot from <your backup folder>\noite-backups\<id>\snapshots\
  over data\noite.db (keep a copy of the old file first). Start noite.

Where things are
  data\         your notes (one file: noite.db)
  data\logs\    logs and crash reports — send these if something breaks

Uninstall
  Untick "Start with Windows" in Settings, quit, delete the folder.
```

## CI (`.github/workflows/ci.yml`)

- **Job `build`** (every push and PR):
  1. checkout with `fetch-depth: 0` (tags visible to `git describe`);
  2. Qt, MSVC, configure, build Release, test (as today);
  3. `tools/deploy.ps1 -Config Release -QtDir …` (fails over budget);
  4. `tools/package.ps1 -DryRun` (assembles and checks the zip contents, no publishing);
  5. upload the test results (always) and the `dist` artifact (`dist/` plus `build/Release/noite.pdb`, retention 1 day).
- **Job `release`** (`needs: build`; `if: startsWith(github.ref, 'refs/tags/v')`; `permissions: contents: write`):
  1. download the artifact;
  2. `tools/package.ps1 -Version <tag without v>` (fails if ≠ the version in the built exe);
  3. `gh release create <tag> --draft --generate-notes --title "noite <v>"` with the 3 files;
  4. print the sizes.

### tools/package.ps1

- **Parameters:** `-Version <x.y.z>` (required unless `-DryRun`), `-DryRun`.
- **Steps:**
  - read the built version from `dist\noite\noite.exe`'s version resource; fail if a `-Version` was given and differs;
  - copy `packaging\README.txt` (with `<version>` substituted), `LICENSE` → `LICENSE.txt`, `packaging\THIRD-PARTY.txt` and `packaging\licenses\*` into a staging `noite\`;
  - check the required files exist: `noite.exe`, `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`, `platforms\qwindows.dll`, `vcruntime140.dll`, `README.txt`, `LICENSE.txt`, `THIRD-PARTY.txt`, `licenses\LGPL-3.0.txt`, `licenses\GPL-3.0.txt`;
  - check that no `data\` and no `*.pdb` are present;
  - write the two zips and `SHA256SUMS.txt` to `dist\`;
  - print the zip contents' top level and the sizes.
- **Locally:** `tools/package.ps1 -Version 0.1.0` after `tools/build.ps1 -Config Release` and `tools/deploy.ps1` gives the same files.

## Upgrade guarantee (golden data folders)

- **Fixtures:** `core/tests/golden/v1/noite.db` and `core/tests/golden/v2/noite.db`, each with a `manifest.json`. The manifest lists:
  - each note's title, status (active / done / trashed with `trashed_at`), colour, due date, tags and bundle;
  - each bundle's name and members;
  - the settings values (hotkeys, `trashRetentionDays`, `dayStart`, `staleDays`, `backupDir`, `backupFrequency`, `backupKeep`, `startWithWindows`, `setupCompleted`);
  - `last_review_day` and `job_state`;
  - for v2, one attachment's id, size and SHA-256, plus the note that references it.
- **Generator** (`core/tests/golden/make_golden.cpp`, a test-only executable `noite_golden <dir> <schema>`, not run by CI):
  - **v2:** writes through the real `Store` API with a fixed clock;
  - **v1:** applies the frozen `schema_v1.sql` and inserts the same data with SQL, then sets `user_version = 1`. v1 kept images as files, which the v2 migration doesn't import (nothing was ever released at v1), so the v1 fixture has no images; a comment says so.

  Fixtures are generated once and committed; they are never regenerated.
- **Rule for future schema bumps:** before bumping to vN+1, generate and commit `golden/vN` with the then-current build. This is written in `migrate.cpp` next to the migration list and in the README's developer section.
- **`tst_upgrade` (core):** data-driven over every folder in `core/tests/golden/`. For each: copy to a temp dir, then
  - `DataDir::acquire` and `Store::open` with a fixed clock;
  - `schemaVersion()` is the latest;
  - if the fixture's schema is older, a `migration-backups\noite-v<old>-*.db` exists;
  - every manifest item matches;
  - `listBoard` shows the expected active notes, and full-text search finds the word `golden` in exactly the expected note;
  - a `backup::run` to a temp dest passes `verify`.
- **`tst_app` (golden v2):** an `App` on a copy of `golden/v2` (whose `setupCompleted` is true) opens no wizard, registers both hotkeys on the fake backend, and its board shows the expected card count.

## Errors

| Case | Behaviour |
|---|---|
| Tag ≠ project version | The release job fails: `tag v0.1.1 ≠ project version 0.1.0`; no release. |
| Release for the tag already exists | `gh release create` fails; nothing overwritten. Delete the draft and re-tag. |
| Tests fail on a tag | `release` doesn't run (`needs: build`). |
| Folder over budget / required file missing / `data` or PDB inside the zip | `build` fails on every push (`deploy.ps1`, `package.ps1 -DryRun`). |
| No git | Version `0.1.0-dev`; the build works. |
| A golden folder no longer opens or differs from its manifest | `tst_upgrade` fails naming the item. |

## Testing

- **`tst_version` (app):**
  - `NOITE_VERSION` matches `^\d+\.\d+\.\d+(-dev(\+[0-9a-f]+(\.dirty)?)?)?$`;
  - `applicationVersion()` equals it;
  - the startup log line, the tray tooltip (`App::trayToolTip()`) and the Settings label contain it.
- **`tst_upgrade` (core)** and the golden-v2 `tst_app` case: as above.
- **`package.ps1 -DryRun`** in CI on every push, as the check of the zip contents.
- **Manual checks** (`docs/testing/manual-capture-checks.md`, new "Release" section):

| # | Check | Expected |
|---|---|---|
| R-1 | Push tag `v0.1.0` | A draft release "noite 0.1.0" with the 3 files; the notes list the commits |
| R-2 | Download the zip into a new folder, run noite.exe | SmartScreen → More info → Run anyway starts it; Properties → Details shows 0.1.0; the log says `noite 0.1.0 started`; the setup wizard opens |
| R-3 | Replace the exe in your real noite folder with the release one | Notes intact, no new migration snapshot (same schema), Settings shows 0.1.0 |

## Docs

- **`README.md`:**
  - replace "design and spikes, no app yet" with what v1 does;
  - add **Download** (latest release, the SmartScreen step), **Build** (`tools/build.ps1`, `tools/deploy.ps1`, `tools/package.ps1`) and **Releasing** (bump `project(VERSION)`, commit, tag `vX.Y.Z`, push the tag, check the draft, publish);
  - documents table: milestones M0–M7.
- **`docs/spec.md`:**
  - D23: the release assets and the draft flow;
  - D26: the restore procedure and golden folders as the upgrade guarantee;
  - D28: the CI row says CI builds and checks the portable zip on every push and tags create a draft release.
- **Roadmap:** split the M7 row into M7a–M7d; link the M7a plan.
