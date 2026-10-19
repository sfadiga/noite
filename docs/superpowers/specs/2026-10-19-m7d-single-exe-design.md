# M7d: single-exe release and versioning — design

**Date:** 2026-10-19 · **Status:** agreed in a grilling session · **Spec:** D23 (distribution), D28 (CI); builds on M7a (`docs/superpowers/specs/2026-10-19-m7a-release-pipeline-design.md`)

## Goal

- **Ship noite 1.0.0 on GitHub as one self-contained `noite.exe`:** static Qt, static C/C++ runtime, no DLLs.
- **Move the version to a `version.txt` file** with a user-facing `CHANGELOG.md`.
- **Show the version on the board.**
- **Publish the static Qt** as its own public repo, so others can use it too.

## Decisions

| # | Decision | Why |
|---|---|---|
| S1 | "Single exe" = static Qt + static runtime (`/MT`) + no ICU: one `noite.exe`, no DLLs or plugin folders; `data\` still created next to it on first run | The user's choice. |
| S2 | The static Qt comes from a **new public repo `sfadiga/qt-static-windows`**, built and published by **that repo's GitHub Actions** | The user's choice: reusable by others, reproducible, built on the same runner image as noite's CI. |
| S3 | Release asset: `noite-X.Y.Z-win64.zip` holding the static `noite.exe` + README/licence texts; the DLL zip stops shipping | LGPLv3 notices must travel with a statically linked binary; a zip carries them. |
| S4 | `version.txt` (one line, `X.Y.Z`) is the only place the number lives; CMake reads it; the tag must equal it | The user's request: one easy file to bump. |
| S5 | `CHANGELOG.md` (Keep a Changelog, user-facing); a tag needs its section, which becomes the release notes | The user's request; better notes than an auto PR list. |
| S6 | The first release is `v1.0.0`; no 0.x releases | `v0.1.0` was never tagged. |
| S7 | A muted version label on the board's top row | The user's request. |
| S8 | The dynamic build and its test suites stay the test gate; local builds stay dynamic; the static exe is built only in CI | No multi-hour Qt build on the author's PC; fast everyday tests. |

## Part A — `sfadiga/qt-static-windows` (new repo)

- **Visibility:** public. Description: "Static Qt 6 builds for MSVC x64 (qtbase, static runtime)".
- **Licence:** MIT for the scripts and workflow. The README states that the published Qt binaries are Qt's, under LGPLv3/GPLv3, with the matching source attached to each release.
- **Workflow `build.yml`:**
  - Trigger: `workflow_dispatch`, with inputs `qt_version` (default `6.10.3`) and `build` (a build number, default `1`).
  - Runs on `windows-latest`, the same image noite's CI uses.
- **Steps:**
  1. Download `qtbase-everywhere-src-<v>.tar.xz` from `download.qt.io` and check it against Qt's published `.sha256` (or `md5sums`).
  2. Run the MSVC developer environment.
  3. Configure:

     ```
     configure.bat -prefix <install> -static -static-runtime -release -optimize-size
                   -no-icu -no-opengl -nomake tests -nomake examples -no-ltcg
     ```

     LTCG stays off: LTCG objects only link with the exact compiler that built them.
  4. `cmake --build . --parallel`, then `cmake --install .`.
  5. Write `BUILD-INFO.txt`: the Qt version, MSVC version (`cl` banner), Windows SDK version, the exact configure command, the date, and the commit of the build repo.
  6. Zip the install as `qt-<v>-static-msvc2022-x64-mt.zip`.
  7. Write `SHA256SUMS.txt` covering the zip and the source tarball.
  8. Publish the release `qt-<v>-msvc2022-x64-mt-<build>` (not a draft). Assets:
     - the zip;
     - `BUILD-INFO.txt`;
     - Qt's licence texts (`LICENSES\*` from the source);
     - the source tarball;
     - `SHA256SUMS.txt`.
- **Variants:** one to start (release, `/MT`, x64, qtbase only, no ICU, no OpenGL). The asset name encodes the variant, so more can be added later.
- **README:**
  - what's inside, and the exact configure flags;
  - how to use it: set `CMAKE_PREFIX_PATH` to the unzipped folder; import the static plugins (on by default with Qt's CMake `qt_add_executable`);
  - the licence notes.
- **Outward steps:** creating the repo, pushing, and triggering the first build each need the author's explicit go-ahead at the time.

## Part B — noite

### Version source

- **`version.txt`** at the repo root: exactly one line, `1.0.0` (`^\d+\.\d+\.\d+$`, a trailing newline allowed).
- **CMake:** `CMakeLists.txt` reads `version.txt` before `project()`. A malformed file fails the configure with a clear message. The result goes into `project(noite VERSION <file> LANGUAGES C CXX)`. `cmake/NoiteVersion.cmake` keeps its rules unchanged, now with the file's version as the base. `CMAKE_CONFIGURE_DEPENDS` adds `version.txt`.
- **Tag check:** the release job checks the tag `vX.Y.Z` against `version.txt` itself. The exe-version check in `package.ps1` stays as the second guard.

### CHANGELOG.md

- **Location and format:** at the repo root, in [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) format, with an intro line and `## [Unreleased]` at the top.
- **Releases:** each release gets `## [X.Y.Z] - YYYY-MM-DD` with `### Added` / `### Changed` / `### Fixed` subsections, plus compare links at the bottom.
- **The 1.0.0 entry** is user-facing and lists:
  - instant capture (global hotkey, rich text, paste images and files, `@dates`, tags);
  - the board (tabs, search, filter chips, bundles, undo);
  - the morning review (overdue, due today, stale; Done / Snooze / Keep / Trash);
  - backups to a folder or Google Drive, trash purge, logs and crash reports;
  - first-run setup and Settings;
  - the portable single exe.
- **Tags:** the release job extracts the section for the version (from its heading to the next `## [`) and fails if it is missing or empty. That text is passed to `gh release create --notes-file` instead of `--generate-notes`.

### tools/bump-version.ps1

`tools/bump-version.ps1 <X.Y.Z>`:
- refuses a version that isn't x.y.z or isn't greater than the current one;
- writes `version.txt`;
- in `CHANGELOG.md`, renames `## [Unreleased]` to `## [X.Y.Z] - <today>` and inserts a fresh empty `## [Unreleased]` above it, then updates the compare links;
- refuses if `[Unreleased]` is empty;
- prints the git commands, and doesn't run them:

  ```
  git add version.txt CHANGELOG.md; git commit -m "release X.Y.Z"; (merge to main)
  git tag vX.Y.Z; git push origin vX.Y.Z
  ```

- has a `-SelfTest` mode on temp copies.

### Version on the board

- A `QLabel` at the right end of the board's top row (after the search box), in the theme's muted colour at the app font size minus one point.
- **Text:** `v<X.Y.Z>` for a release build; for any dev build, the base plus `-dev` (e.g. `v1.0.0-dev`).
- **Tooltip:** `noite <full NOITE_VERSION> · Qt <qVersion()>`.
- Not clickable, and not focusable.

### Static build in noite's CI

- **`tools/qt-static.json`:** `{ "tag": "qt-6.10.3-msvc2022-x64-mt-1", "url": "https://github.com/sfadiga/qt-static-windows/releases/download/<tag>/qt-6.10.3-static-msvc2022-x64-mt.zip", "sha256": "<hex>" }`.
- **Job `single-exe`** (every push and PR; independent of `build`):
  1. checkout with `fetch-depth: 0`, then MSVC dev env;
  2. restore the static Qt from `actions/cache` (key = the json's `sha256`), or download it and verify the SHA-256 (failing on a mismatch);
  3. configure noite Release with `CMAKE_PREFIX_PATH` set to the static Qt and `-DNOITE_STATIC=ON`, then build only the `noite` target;
  4. run `noite.exe --self-check`;
  5. `tools/package.ps1 -Static -DryRun`;
  6. upload the artifact `noite-single-exe` (the zip and the PDB, 7 days).
- **`NOITE_STATIC=ON`:**
  - links the static plugins: `qwindows`, `qmodernwindowsstyle`, `qjpeg`, `qgif`, `qico`; with Qt's CMake, via `qt_import_plugins` or the default static plugin import;
  - sets `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` (`/MT`) for noite and noite-core;
  - adds no other code-path differences.
- **The release job** uses the `single-exe` artifact (`needs: [build, single-exe]`) and packages with `-Static`.

### --self-check

- `noite.exe --self-check` is handled in `main()` before `App`.
- It creates only a `QGuiApplication`, with no windows and no tray.
- It checks that:
  - `NOITE_VERSION` starts with `version.txt`'s value (compiled in as `NOITE_VERSION_BASE`);
  - `QPluginLoader::staticInstances()` includes the Windows platform, the modern Windows style, and the JPEG, GIF and ICO image plugins (by class name);
  - `QImageReader` decodes a tiny embedded JPEG and GIF (from a Qt resource or a byte array).
- Output goes to `noite-self-check.txt` next to the exe: `self-check ok <version>`, or `self-check FAILED: <item>`.
- Exit code 0 on success, 1 on failure. It never touches `data\`.
- In a dynamic build the same flag works, with the plugins coming from the plugins folder. The dynamic CI build also runs it, via `-SelfTest` or a step.

### Packaging (`tools/package.ps1 -Static`)

- **Main zip** `noite-X.Y.Z-win64.zip`: `noite\noite.exe`, `README.txt`, `LICENSE.txt`, `THIRD-PARTY.txt`, `licenses\LGPL-3.0.txt`, `licenses\GPL-3.0.txt`.
  - Required: those files.
  - Forbidden: any `*.dll`, any plugin folder, `data\`, any `*.pdb`.
- **Before zipping:** run `noite.exe --self-check` and fail on non-zero.
- **PDB zip and `SHA256SUMS.txt`:** as in M7a.
- **The non-static mode** stays as it is, for local packaging and the dynamic dry run.
- **`THIRD-PARTY.txt`:** say Qt is **statically linked** (not "dynamically linked, unmodified"). It must also say:
  - noite's complete source is public under MIT (link), so the application can be relinked against a modified Qt, as LGPLv3 §4(d) requires;
  - where to get Qt's source: the exact tarball, attached to the qt-static-windows release.

### Docs

- **README.md:** Download (a single exe in a zip); Releasing (edit `CHANGELOG.md` → `tools/bump-version.ps1` → merge → tag); Build (local dynamic as today; static only in CI, via qt-static-windows).
- **spec.md D23:** the single-exe asset; the static Qt source.
- **Roadmap:** M7d row linked to the plan; M7d marked as the path to 1.0.0.
- **Manual checks:** a new "Single exe" section:
  - **SE-1:** download the 1.0.0 zip into a fresh folder → one `noite.exe`, no DLLs; it runs; Properties → Details shows 1.0.0; the board shows `v1.0.0`.
  - **SE-2:** pasting a JPEG and a GIF image works.
  - **SE-3:** replace the exe in your real noite folder → notes intact.

## Errors

| Case | Behaviour |
|---|---|
| `version.txt` missing or malformed | Configure fails: `version.txt must contain X.Y.Z`. |
| Tag ≠ `version.txt` | The release job fails before packaging. |
| No CHANGELOG section for the tag, or it's empty | The release job fails: `CHANGELOG.md has no entry for X.Y.Z`. |
| Static Qt download hash mismatch | `single-exe` fails; nothing is built. |
| A static plugin missing or an image decoder broken | `--self-check` fails, and so does `single-exe` / packaging. |
| A DLL or plugin folder in the static zip | `package.ps1 -Static` refuses. |
| The qt-static-windows release doesn't exist yet | `single-exe` fails at download (expected until Part A has published). |

## Testing

- **`cmake/tests/version_test.cmake`:** reading `version.txt`: a valid file; a trailing newline; a malformed file rejected.
- **`tst_version`:**
  - `NOITE_VERSION` starts with `NOITE_VERSION_BASE`, which equals the contents of `version.txt`;
  - the board label's text and tooltip, for a release-format and a dev-format version: a pure function `boardVersionText(QString full)` tested with `"1.0.0"` → `"v1.0.0"` and `"1.0.0-dev+abc1234.dirty"` → `"v1.0.0-dev"`, plus the label existing on `BoardWindow` with that text.
- **`tst_self_check` (app):** the self-check logic as a function `std::optional<QString> runSelfCheck()`, which returns the failing item or nullopt. In the dynamic test build, check it returns nullopt with the plugins available. A missing plugin is simulated through an injectable class-name list.
- **`bump-version.ps1 -SelfTest`:**
  - a valid bump rewrites both files exactly;
  - an empty `[Unreleased]` is refused;
  - a non-greater version and a malformed version are refused.
- **`package.ps1 -SelfTest`:** new `-Static` cases:
  - a complete static dist passes;
  - a DLL in it is refused;
  - a `platforms\` folder is refused;
  - a self-check that fails (stand-in exe) is refused.
- **CI:** `single-exe` on every push is the end-to-end check of the static build.

## Order of work

1. Part A: create the repo, add the workflow and README, run the first build, publish `qt-6.10.3-msvc2022-x64-mt-1`.
2. Part B tasks: `version.txt` + CHANGELOG + bump helper; board label; `--self-check`; static CMake option + packaging; CI `single-exe` + release changes; docs.
3. Merge, run `tools/bump-version.ps1 1.0.0` if needed (`version.txt` already holds 1.0.0 and the CHANGELOG already has the 1.0.0 section), and tag `v1.0.0`.
