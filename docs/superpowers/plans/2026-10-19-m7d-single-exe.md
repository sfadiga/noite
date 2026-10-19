# M7d: Single-Exe Release and Versioning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** noite 1.0.0 ships on GitHub as one self-contained `noite.exe`, built in CI against a static Qt published by a new public repo `sfadiga/qt-static-windows`. The version comes from `version.txt`, release notes come from `CHANGELOG.md`, and the board shows the version.

**Architecture:**
- **Part A** (Task 1): a separate repo whose `workflow_dispatch` workflow builds and publishes static qtbase.
- **Part B:**
  - `version.txt` feeds CMake, with a changelog toolchain (Task 2);
  - the board label (Task 3);
  - a `--self-check` mode (Task 4);
  - a `NOITE_STATIC` build plus `package.ps1 -Static` (Task 5);
  - a `single-exe` CI job and release changes (Task 6).

  The dynamic build and its tests remain the test gate.

**Tech Stack:** CMake 3.21+, Qt 6.10.3 (dynamic locally; a static qtbase from Part A in CI), MSVC on `windows-latest`, PowerShell 5.1, GitHub Actions, `gh`.

**Spec:** `docs/superpowers/specs/2026-10-19-m7d-single-exe-design.md` (S1–S8; builds on M7a)

## Global Constraints

- `version.txt`: one line `X.Y.Z` (`^\d+\.\d+\.\d+$`). A UTF-8 BOM, a trailing CR/LF or surrounding whitespace is tolerated and stripped; anything else fails the configure with `version.txt must contain X.Y.Z`. Initial content `1.0.0`.
- Qt static configure: `-static -static-runtime -release -optimize-size -no-icu -no-opengl -nomake tests -nomake examples -no-ltcg`, qtbase only, x64.
- Qt repo:
  - repo `sfadiga/qt-static-windows`, public, with the description "Static Qt 6 builds for MSVC x64 (qtbase, static runtime)";
  - release tag `qt-6.10.3-msvc2022-x64-mt-1`;
  - asset `qt-6.10.3-static-msvc2022-x64-mt.zip`;
  - also `BUILD-INFO.txt`, `SHA256SUMS.txt`, Qt's `LICENSES\*` and `qtbase-everywhere-src-6.10.3.tar.xz`.
- Static plugins linked into noite: `QWindowsIntegrationPlugin`, `QModernWindowsStylePlugin`, `QJpegPlugin`, `QGifPlugin`, `QICOPlugin`.
- Board label: `v<X.Y.Z>` for a release; `v<X.Y.Z>-dev` for any dev build. Tooltip `noite <full NOITE_VERSION> · Qt <qVersion()>`.
- Self-check:
  - `noite.exe --self-check`;
  - writes `noite-self-check.txt` next to the exe: `self-check ok <version>` or `self-check FAILED: <item>`;
  - exits 0 or 1;
  - creates no window, tray or `data\`.
- Static zip:
  - `noite-X.Y.Z-win64.zip` containing only `noite\noite.exe`, `README.txt`, `LICENSE.txt`, `THIRD-PARTY.txt`, `licenses\LGPL-3.0.txt`, `licenses\GPL-3.0.txt`;
  - forbidden: any `*.dll`, any subfolder other than `licenses\`, `data\`, any `*.pdb`, `noite-self-check.txt`;
  - `dumpbin /dependents noite.exe` must list no `Qt6*`, `vcruntime*`, `msvcp*`, `concrt*`, `icu*` or `ucrtbase*` DLL.
- Changelog: Keep a Changelog 1.1.0; `## [Unreleased]` on top; releases `## [X.Y.Z] - YYYY-MM-DD`; a tag fails without a non-empty section; that section becomes the release notes.
- Error texts:
  - `version.txt must contain X.Y.Z`
  - `CHANGELOG.md has no entry for X.Y.Z`
  - `tag vA.B.C ≠ version.txt X.Y.Z`
- Outward steps (creating a repo, pushing, triggering workflows, tagging, releasing) happen only after the user's explicit go-ahead. Implementers never do them.
- Test command: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -Config Debug -Test` → `TOTAL failures: N`.
- Edit files only with the Edit/Write tools. Every edited text file passes `grep -nP '[\x00-\x08\x0b-\x0c\x0e-\x1f]'` with no output.

## Review Focus

1. **`version.txt` saved by Notepad** (BOM, CRLF, a trailing space) → still reads `1.0.0`. Test: Task 2 `version_test.cmake` cases for BOM+CRLF and trailing whitespace.
2. **Changelog prefixes** (`[1.0.1]` vs `[1.0.10]`) and the compare-link block → section extraction takes exactly the right section, never the next one or the links. Test: Task 2 `changelog-section.ps1 -SelfTest` case "1.0.1 vs 1.0.10".
3. **The "single" exe still needs a DLL** on a friend's PC (`VCRUNTIME140.dll`, a Qt DLL) → the package must refuse it. Test: Task 5 `package.ps1 -Static -SelfTest` case "dependent DLL refused" (stand-in dependents list), plus the real `dumpbin` check in `-Static` mode.
4. **The self-check's output file ends up in the zip.** Test: Task 5 `-SelfTest` case "noite-self-check.txt never zipped".
5. **The board at its minimum width** → the version label must not shrink the search box below 260 px or hide the tabs. Test: Task 3 `boardLabelAtMinimumWidth`.

---

### Task 1: Part A — the `qt-static-windows` repo contents (local only)

**Files** (in a new folder **outside** noite: `D:\0_GitHub\qt-static-windows\`, initialised as its own git repo):
- Create:
  - `README.md`, `LICENSE` (MIT, © Sandro Fadiga)
  - `.github/workflows/build.yml`
  - `build.ps1` (the build logic, callable locally or from CI)
  - `.gitignore` (`/work/`, `/out/`)

**Interfaces:**
- Produces:
  - `build.ps1 -QtVersion <6.10.3> -Build <1> -WorkDir <path> -OutDir <path> [-DryRun]`:
    - download the source and verify it;
    - configure with the Global Constraints flags plus `-prefix <WorkDir>\install`;
    - build, install, and write `BUILD-INFO.txt`;
    - zip `<OutDir>\qt-<v>-static-msvc2022-x64-mt.zip`, copy `LICENSES\*` and the tarball into `<OutDir>`, and write `<OutDir>\SHA256SUMS.txt`;
    - `-DryRun` prints every command and path without downloading or building.
  - `build.yml`:
    - `workflow_dispatch` with inputs `qt_version` (default `6.10.3`) and `build` (default `1`);
    - `runs-on: windows-latest`, `timeout-minutes: 360`, `permissions: contents: write`;
    - steps: `actions/checkout@v4` → `ilammy/msvc-dev-cmd@v1` → `powershell -File build.ps1 …` → `gh release create "qt-${qt_version}-msvc2022-x64-mt-${build}" <OutDir>/* --title "Qt ${qt_version} static (MSVC x64, /MT) build ${build}" --notes-file <OutDir>/BUILD-INFO.txt`, with `GH_TOKEN: ${{ github.token }}`.
  - Source download: `https://download.qt.io/archive/qt/<major.minor>/<v>/submodules/qtbase-everywhere-src-<v>.tar.xz`, verified against the adjacent `.sha256` file (if absent, the `md5sums.txt` in that folder). Fail on a mismatch.

- [ ] **Step 1: Write `build.ps1`** (PowerShell 5.1; `$ErrorActionPreference = 'Stop'`).
  - Native calls go through a helper that checks `$LASTEXITCODE`.
  - Extract the `.tar.xz` with `tar -xf`, which ships with Windows 10+.
  - Configure from a separate build dir with `<src>\configure.bat`, then `cmake --build . --parallel`, then `cmake --install .`.
  - `BUILD-INFO.txt` lines: `Qt <v>`, `MSVC <cl version>` (from `cl 2>&1`), `Windows SDK <WindowsSDKVersion env>`, `configure <exact command>`, `date <UTC ISO>`, `build-repo <git rev-parse HEAD>`.
- [ ] **Step 2: Write `build.yml`, `README.md` and `LICENSE`.**
  - README sections: What this is; What's inside (the flags); Using it with CMake (`-DCMAKE_PREFIX_PATH=<unzipped>`, `qt_import_plugins`); Licences (the binaries are Qt's, LGPLv3/GPLv3; source attached to every release; the scripts are MIT); Building a new version (Actions → build → Run workflow).
- [ ] **Step 3: Verify locally without building Qt.**
  - `powershell -File build.ps1 -QtVersion 6.10.3 -Build 1 -WorkDir $env:TEMP\qsw -OutDir $env:TEMP\qsw-out -DryRun` prints the URL, the configure command (with every flag from Global Constraints) and the asset names.
  - The YAML parses (`python -c "import yaml; yaml.safe_load(open('.github/workflows/build.yml'))"`).
  - Also download only the `.sha256` file and show that the URL scheme resolves (`Invoke-WebRequest -Method Head`).
- [ ] **Step 4: Commit in the new repo** (`git init -b main`, `git add .`, `git commit -m "Static Qt build workflow (qtbase, MSVC x64, /MT)"`). Do **not** create the GitHub repo or push; the controller does that after the user's go-ahead.

---

### Task 2: `version.txt`, CHANGELOG and the changelog tools

**Files:**
- Create: `version.txt`, `CHANGELOG.md`, `tools/bump-version.ps1`, `tools/changelog-section.ps1`
- Modify:
  - `CMakeLists.txt` (read `version.txt` before `project()`)
  - `cmake/NoiteVersion.cmake` (a `noite_read_version_file(<path> <out_var>)` helper; `version.txt` in `CMAKE_CONFIGURE_DEPENDS`; `NOITE_VERSION_BASE` in the generated header)
  - `cmake/tests/version_test.cmake`
  - `app/src/version.h.in`
  - `app/tests/tst_version.cpp`
  - `tools/run-tests.ps1` (also run both scripts' `-SelfTest`, counted toward the total)

**Interfaces:**
- Produces:
  - `noite_version.h` gains `#define NOITE_VERSION_BASE "X.Y.Z"`
  - `tools/changelog-section.ps1 -Version X.Y.Z [-Path CHANGELOG.md] [-Out notes.md] [-SelfTest]`: writes the section's body (the lines after `## [X.Y.Z] - …` up to the next `## [` or the link-reference block), trimmed. Throws `CHANGELOG.md has no entry for X.Y.Z` if missing or empty.
  - `tools/bump-version.ps1 -Version X.Y.Z [-SelfTest]`: as in the spec (refuses non-x.y.z, a non-greater version, or an empty `[Unreleased]`; prints the git commands without running them).

- [ ] **Step 1: Write the failing tests**
  - **`version_test.cmake`**, via `noite_read_version_file`, with files written to a temp dir:
    - `"1.0.0"` → `1.0.0`
    - `"1.0.0\n"` → `1.0.0`
    - `"\xEF\xBB\xBF1.0.0\r\n"` → `1.0.0`
    - `" 1.0.0 \n"` → `1.0.0`
    - `"1.0"`, `"v1.0.0"`, `""` → fatal with `version.txt must contain X.Y.Z` (test with `execute_process(COMMAND ${CMAKE_COMMAND} -P <helper script>)` and check the output).
  - **`tst_version`:** `QCOMPARE(QString(NOITE_VERSION_BASE), <contents of version.txt read at test time via a NOITE_SOURCE_DIR define>)`; `QVERIFY(QString(NOITE_VERSION).startsWith(NOITE_VERSION_BASE))`.
  - **`changelog-section.ps1 -SelfTest`** (temp files):
    - extracts `[1.0.0]`'s body exactly;
    - `[1.0.1]` and `[1.0.10]` both present → each extraction returns its own body;
    - the last section before the link block doesn't include the links;
    - a missing version throws the exact message;
    - an empty section throws.
  - **`bump-version.ps1 -SelfTest`** (temp copies):
    - 1.0.0 → 1.0.1 writes `version.txt` `1.0.1`, a dated `## [1.0.1] - <today>` holding the former Unreleased lines, a fresh empty `## [Unreleased]` and updated compare links;
    - an empty Unreleased is refused;
    - `1.0.0` (not greater) is refused;
    - `1.1` is refused.
- [ ] **Step 2: Run to verify they fail** (the cmake script and the new self-tests fail; `tst_version` fails to compile without `NOITE_VERSION_BASE`).
- [ ] **Step 3: Implement**
  - `CMakeLists.txt` includes the version module, then calls `noite_read_version_file(${CMAKE_CURRENT_SOURCE_DIR}/version.txt NOITE_BASE)`, then `project(noite VERSION ${NOITE_BASE} LANGUAGES C CXX)`. The rest of the version logic is unchanged.
  - `version.txt` = `1.0.0`.
  - `CHANGELOG.md`: the intro, an empty `## [Unreleased]`, `## [1.0.0] - 2026-10-19` with the user-facing Added list from the spec, and the link references (`[Unreleased]: https://github.com/sfadiga/noite/compare/v1.0.0...HEAD`, `[1.0.0]: https://github.com/sfadiga/noite/releases/tag/v1.0.0`).
- [ ] **Step 4: Verify** — `cmake -P cmake/tests/version_test.cmake`, both `-SelfTest`s and the test command → `TOTAL failures: 0`. A Release build's exe ProductVersion starts with `1.0.0`.
- [ ] **Step 5: Commit** — `feat(release): version.txt is the version source; CHANGELOG.md with section extraction and a bump helper`.

---

### Task 3: Version label on the board

**Files:**
- Create: `app/src/Version.h/.cpp`
- Modify: `app/src/board/BoardWindow.h/.cpp`, `app/tests/tst_version.cpp`, `app/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `QString boardVersionText(const QString& fullVersion)`: `"1.0.0"` → `"v1.0.0"`; `"1.0.0-dev+abc1234.dirty"` → `"v1.0.0-dev"`; `"1.0.0-dev"` → `"v1.0.0-dev"`
  - `QString versionToolTip()`: `noite <NOITE_VERSION> · Qt <qVersion()>`
  - `QLabel* BoardWindow::versionLabel() const`

- [ ] **Step 1: Write the failing tests** (`tst_version.cpp`)

```cpp
void boardVersionTextForms()   // the three cases above
void boardShowsVersionLabel()  // BoardWindow on a store fixture: versionLabel() non-null, text == boardVersionText(NOITE_VERSION),
                               // toolTip == versionToolTip(), focusPolicy == Qt::NoFocus, it sits in the top row right of the search box
                               // (its x() > search box x())
void boardLabelAtMinimumWidth() // resize the board to its minimumSizeHint().width(): search box width >= 260, label visible
```

- [ ] **Step 2: Run to verify they fail** (compile errors).
- [ ] **Step 3: Implement.**
  - The label goes at the right end of the top row's layout (after `search_`).
  - It uses the theme's muted colour (see `Theme.h` for the existing muted/secondary text colour) and the app font size minus 1 pt.
  - `Qt::NoFocus`, and no interaction.
- [ ] **Step 4: Run the tests to verify they pass** → `TOTAL failures: 0`.
- [ ] **Step 5: Commit** — `feat(board): version label in the top row`.

---

### Task 4: `--self-check`

**Files:**
- Create: `app/src/SelfCheck.h/.cpp`, `app/tests/tst_self_check.cpp`, `app/src/selfcheck.qrc` (or byte arrays in `SelfCheck.cpp`)
- Modify: `app/src/main.cpp`, `app/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `QStringList requiredStaticPlugins()`: the five class names from Global Constraints when `NOITE_STATIC` is defined, else empty
  - `QStringList presentStaticPluginClasses()`: the `className()` from each `QPluginLoader::staticInstances()` entry's metaObject
  - `std::optional<QString> runSelfCheck(const QStringList& required, const QStringList& present)`, which returns the first failing item text or nullopt. The checks:
    - `NOITE_VERSION` starts with `NOITE_VERSION_BASE`;
    - every required class is present (`missing plugin <name>`);
    - a `QImageReader` decodes the embedded GIF (`gif decode`) and JPEG (`jpeg decode`).
  - `int selfCheckMain(int argc, char** argv)`: builds a `QCoreApplication`, runs the check with the real lists, writes `noite-self-check.txt` (UTF-8) next to the exe with `self-check ok <NOITE_VERSION>` or `self-check FAILED: <item>`, and returns 0 or 1.
  - `main()`: if any argument equals `--self-check`, `return selfCheckMain(argc, argv);` before `applog::startBuffering()` and `QApplication`.
- Embedded test images:
  - **GIF** (1×1, hex): `47494638396101000100800000000000ffffff21f90401000000002c00000000010001000002024401003b`.
  - **JPEG:** generate a valid 8×8 baseline JPEG once (for example with `QImage(8,8,…).save(buf,"JPG")` in a throwaway program, or Python/PIL) and embed its bytes as a hex array. Note in a comment how it was made.

- [ ] **Step 1: Write the failing tests** (`tst_self_check.cpp`)

```cpp
void passesWithNothingRequired()  // runSelfCheck({}, {}) == nullopt (the dynamic test build has the image plugins in its plugin path)
void reportsMissingPlugin()       // runSelfCheck({"QJpegPlugin","QGifPlugin"}, {"QGifPlugin"}) == "missing plugin QJpegPlugin"
void requiredListMatchesBuild()   // in this (dynamic) test build, requiredStaticPlugins() is empty
void selfCheckMainWritesFile()    // QProcess runs a copy of noite.exe (in a QTemporaryDir) with --self-check: exit code 0,
                                  // <temp>/noite-self-check.txt starts with "self-check ok ", and no <temp>/data folder was created
                                  // (selfCheckMain can't run in-process: it creates its own QCoreApplication)
```

For `selfCheckMainWritesFile`:
- Copy `noite.exe` into a `QTemporaryDir`, so the file and the absence of `data\` are checked there. The dynamic exe needs its Qt DLLs, so set `PATH` to include the Qt bin dir, which the test runner already provides, and `QT_PLUGIN_PATH`.
- Locate the exe via a compile definition `NOITE_EXE_PATH` (`$<TARGET_FILE:noite>`), with `add_dependencies(app-tests noite)`.
- It must not open any window. If the test can't run hermetically, make it the controller's manual check and say so in the report.

- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run the tests to verify they pass** → `TOTAL failures: 0`. Then run `build\Release\noite.exe --self-check` once by hand (no window appears) and paste `noite-self-check.txt`.
- [ ] **Step 5: Commit** — `feat(app): noite.exe --self-check (version, static plugins, image decoders)`.

---

### Task 5: Static build option and `package.ps1 -Static`

**Prerequisite (controller):** Part A's release `qt-6.10.3-msvc2022-x64-mt-1` exists. The controller passes its asset URL and SHA-256 in the dispatch. The implementer downloads the zip into `%USERPROFILE%\Qt\static-6.10.3` (outside the repo) after verifying the hash.

**Files:**
- Modify:
  - `CMakeLists.txt` and/or `app/CMakeLists.txt` (`option(NOITE_STATIC …)`)
  - `tools/package.ps1` (`-Static`)
  - `packaging/THIRD-PARTY.txt`
  - the spec's THIRD-PARTY wording, if it differs
- Create: `tools/qt-static.json`

**Interfaces:**
- Consumes: Task 4's `NOITE_STATIC` define (`requiredStaticPlugins`), Task 1's asset name.
- Produces:
  - **CMake `-DNOITE_STATIC=ON`:**
    - fails if `Qt6::Core` isn't a `STATIC_LIBRARY` (`NOITE_STATIC needs a static Qt`);
    - sets `CMAKE_MSVC_RUNTIME_LIBRARY` to `MultiThreaded$<$<CONFIG:Debug>:Debug>` for all noite targets;
    - defines `NOITE_STATIC` for noite-app;
    - calls `qt_import_plugins(noite INCLUDE Qt6::QWindowsIntegrationPlugin Qt6::QModernWindowsStylePlugin Qt6::QJpegPlugin Qt6::QGifPlugin Qt6::QICOPlugin)`. Verify the exact target names in `<static Qt>\lib\cmake\Qt6Gui\` and `Qt6Widgets\`; if a name differs, use the real one and update Global Constraints in the report.
  - **`tools/qt-static.json`:** `{ "tag": "qt-6.10.3-msvc2022-x64-mt-1", "url": "<asset url>", "sha256": "<hex>" }`
  - **`package.ps1 -Static [-DryRun|-Version] [-Exe <path>]`:**
    - stages only `noite.exe`, from `-Exe` or by default `build\Static\noite.exe`, plus the packaging texts;
    - runs the exe with `--self-check` (`Start-Process -Wait -PassThru`, with the exe in the stage; exit ≠ 0 → throw with the file's text), then deletes `noite-self-check.txt` from the stage;
    - runs `dumpbin /dependents` (available after the VS dev shell; the script uses `Launch-VsDevShell` like `deploy.ps1`, or finds `dumpbin` via `vswhere`) and refuses any dependent matching `^(Qt6|vcruntime|msvcp|concrt|icu|ucrtbase)` (case-insensitive);
    - applies the Global Constraints required/forbidden lists;
    - PDB from `build\Static\noite.pdb`;
    - outputs and checksums as in M7a.
  - **New `-SelfTest` cases** (stand-ins via injectable `-DependentsOverride` and `-SelfCheckOverride` parameters):
    - a complete static stage passes;
    - a `Qt6Core.dll` file in the stage is refused;
    - a `platforms\` folder is refused;
    - a dependents list containing `VCRUNTIME140.dll` is refused;
    - a failing self-check (exit 1) is refused, with its text in the message;
    - `noite-self-check.txt` is never in the zip.
  - **THIRD-PARTY.txt:** Qt is statically linked. noite's complete source is public under MIT at https://github.com/sfadiga/noite, so the application can be relinked against a modified Qt (LGPLv3 §4(d)). Qt's source is the `qtbase-everywhere-src-6.10.3.tar.xz` attached to https://github.com/sfadiga/qt-static-windows/releases/tag/qt-6.10.3-msvc2022-x64-mt-1. Keep the bundled-component notices from M7a.

- [ ] **Step 1: Write the self-test cases first; run `package.ps1 -SelfTest`** → the new cases fail.
- [ ] **Step 2: Implement the CMake option and `-Static`.**
- [ ] **Step 3: Build static locally once:**

  ```
  cmake -S . -B build\Static -G Ninja -DCMAKE_BUILD_TYPE=Release -DNOITE_STATIC=ON -DCMAKE_PREFIX_PATH=%USERPROFILE%\Qt\static-6.10.3
  cmake --build build\Static --target noite
  ```

  Do this inside the VS dev shell. Then run `tools\package.ps1 -Static -DryRun`. Paste:
  - the `dumpbin /dependents` list;
  - the self-check line;
  - the zip contents;
  - the exe and zip sizes.

  Don't start the exe normally (no GUI). Only `--self-check`.
- [ ] **Step 4: The full dynamic suite** (the test command) → `TOTAL failures: 0`; `package.ps1 -SelfTest` passes.
- [ ] **Step 5: Commit** — `feat(release): NOITE_STATIC build against the qt-static-windows release; package.ps1 -Static (single exe, no DLL dependents)`.

---

### Task 6: CI `single-exe` job, release changes, docs

**Files:**
- Modify:
  - `.github/workflows/ci.yml`
  - `README.md`
  - `docs/spec.md` (D23)
  - `docs/superpowers/plans/2026-10-15-noite-roadmap.md`
  - `docs/testing/manual-capture-checks.md` (a new "Single exe" section, SE-1 to SE-3, verbatim from the spec)

**Interfaces:**
- Consumes: `tools/qt-static.json`, `package.ps1 -Static`, `tools/changelog-section.ps1`, `version.txt`.

- [ ] **Step 1: The `single-exe` job** (every push and PR; `runs-on: windows-latest`):
  1. `actions/checkout@v4` with `fetch-depth: 0`;
  2. `ilammy/msvc-dev-cmd@v1`;
  3. read `tools/qt-static.json`;
  4. `actions/cache@v4` for `${{ runner.temp }}\qt-static`, keyed `qt-static-<sha256>`;
  5. on a miss, download the URL, compare `Get-FileHash` with the json's sha256 (throw on a mismatch), and expand it;
  6. configure `build\Static` (Ninja, Release, `-DNOITE_STATIC=ON`, `CMAKE_PREFIX_PATH` = the expanded folder) and build `--target noite`;
  7. `tools\package.ps1 -Static -DryRun`;
  8. upload the artifact `noite-single-exe` (`dist/dry-run/*.zip`, `dist/dry-run/SHA256SUMS.txt`, and `build/Static/noite.exe` + `noite.pdb`, `retention-days: 7`, `if-no-files-found: error`).
- [ ] **Step 2: The `release` job:**
  1. `needs: [build, single-exe]`;
  2. check the tag `v<version.txt>`: read `version.txt` (trimmed) and compare it with `github.ref_name` minus the `v`; on a difference, throw `tag vA.B.C ≠ version.txt X.Y.Z`;
  3. run `tools/changelog-section.ps1 -Version $v -Out notes.md`;
  4. download the `noite-single-exe` artifact, so the exe and PDB land at `build/Static/`;
  5. run `tools/package.ps1 -Static -Version $v`;
  6. keep the M7a `gh release view` guard;
  7. `gh release create … --draft --notes-file notes.md --title "noite $v"` with the 3 files.

  The DLL-zip packaging leaves the release job. The `build` job keeps its `deploy.ps1` + `package.ps1 -DryRun`, so the dynamic packaging stays checked.
- [ ] **Step 3: Docs.**
  - README:
    - Download is now "a zip with one `noite.exe`";
    - Releasing: edit CHANGELOG `[Unreleased]` → `tools\bump-version.ps1 X.Y.Z` → PR, merge → tag `vX.Y.Z` on `main` → check the draft → publish;
    - Build: local builds are dynamic; the static exe is CI-only, against `sfadiga/qt-static-windows`.
  - spec D23: the single exe and its source.
  - Roadmap: M7d row linked to this plan.
  - Manual checks: SE-1 to SE-3.
- [ ] **Step 4: Validate locally.**
  - The YAML parses.
  - Run every new PowerShell step's command line locally, where possible, against the Task 5 local static build.
  - Run `tools\changelog-section.ps1 -Version 1.0.0` and paste the output.
  - The control-character grep is clean.
  - The full suite → `TOTAL failures: 0`.
- [ ] **Step 5: Commit** — `ci: single-exe job against qt-static-windows; releases ship the single exe with CHANGELOG notes and a version.txt tag check; docs`.

The real CI runs are the controller's job, after the user OKs the push.
