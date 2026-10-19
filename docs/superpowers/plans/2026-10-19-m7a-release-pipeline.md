# M7a: Release Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `v0.x.y` tag makes CI publish a draft GitHub Release with the portable zip (licence notices included), the PDB zip and checksums, all built from the tested binaries. Every build carries a version, and committed golden data folders prove a newer exe opens older data.

**Architecture:**
- **Versioning:** a CMake helper turns `git describe` into the version, with a Windows version resource.
- **Packaging:** `tools/package.ps1` assembles and checks the zips (it has a dry-run mode for every push).
- **CI:** `ci.yml` gains packaging steps and a tag-only `release` job.
- **Upgrade guarantee:** a test-only generator writes golden data folders once, and `tst_upgrade` opens them on every run.

**Tech Stack:** CMake 3.21+ (script mode for unit-testable logic), C++20/Qt 6.10, PowerShell 5.1 (the tools), GitHub Actions (`gh release`), SQLite.

**Spec:** `docs/superpowers/specs/2026-10-19-m7a-release-pipeline-design.md` (P1–P5; project spec D23, D26, D28)

## Global Constraints

- Project version: `project(noite VERSION 0.1.0 LANGUAGES C CXX)`.
- Version rules, from `git describe --tags --match "v*" --long --dirty --always`:
  - `v0.1.0-0-g<sha>` (clean, exactly on the tag) → `0.1.0`;
  - `v0.1.0-<n>-g<sha>` (n > 0) → `0.1.0-dev+<sha>`;
  - any `-dirty` suffix → `<…>-dev+<sha>.dirty`;
  - a describe whose tag isn't `v\d+.\d+.\d+` (e.g. `v0.2.0-rc1-0-g…`), or a bare sha (no tags) → `0.1.0-dev+<sha>`;
  - git unavailable → `0.1.0-dev`.

  The base is always the project version.
- Version regex (tests): `^\d+\.\d+\.\d+(-dev(\+[0-9a-f]+(\.dirty)?)?)?$`.
- Version resource:
  - FileVersion/ProductVersion `x.y.z.0`; the string ProductVersion is the full `NOITE_VERSION`;
  - ProductName `noite`;
  - FileDescription `noite — notes that stay on your radar`;
  - LegalCopyright `MIT licence`;
  - OriginalFilename `noite.exe`.
- App texts: the log line `noite <version> started · data … · Qt … · Windows …`; tray tooltip `noite <version>`; Settings → System label `noite <version>`.
- Asset names: `noite-<v>-win64.zip` (top folder `noite\`), `noite-<v>-win64-pdb.zip` (`noite.pdb`), `SHA256SUMS.txt` (`<sha256>  <file>` lines).
- Required files in the main zip:
  - `noite.exe`, `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`;
  - `platforms\qwindows.dll`, `styles\qmodernwindowsstyle.dll`, `imageformats\qjpeg.dll`, `imageformats\qgif.dll`;
  - `vcruntime140.dll`;
  - `README.txt`, `LICENSE.txt`, `THIRD-PARTY.txt`, `licenses\LGPL-3.0.txt`, `licenses\GPL-3.0.txt`.

  Forbidden: any `data\` entry, any `*.pdb`.
- `README.txt` and `THIRD-PARTY.txt` texts: verbatim from the spec (`<version>` substituted in README).
- Folder budget 45 MB (existing `deploy.ps1 -MaxMB 45`).
- Release job: `needs: build`, `if: startsWith(github.ref, 'refs/tags/v')`, `permissions: contents: write`, `gh release create <tag> --draft --generate-notes --title "noite <v>"`.
- Test command: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -Config Debug -Test` → `TOTAL failures: N`.
- Edit files only with the Edit/Write tools.

## Review Focus

1. **CI checks out a tag as a detached HEAD**, and a tag may point at a merge commit → the version must still read exactly `0.1.0`. Test: Task 1 `cmake/tests/version_test.cmake` case `v0.1.0-0-gabc1234` → `0.1.0`.
2. **A pre-release or malformed tag** (`v0.2.0-rc1`, `v1.0`) → must not produce a release claiming `0.2.0`; the build is a dev version and `package.ps1` refuses it. Test: Task 1 cases `v0.2.0-rc1-0-gabc1234` and `v1.0-0-gabc1234` → `0.1.0-dev+abc1234`; Task 3 `-SelfTest` case "version mismatch refused".
3. **The author's `dist\noite\data` is a live data folder** → it must never end up in a zip. Test: Task 3 `-SelfTest` case "data folder excluded" (staging from a dist with a `data\noite.db` gives a zip without `data\`).
4. **A golden fixture silently regenerated or edited** → the upgrade guarantee would quietly weaken. Test: Task 2 `fixtureUnchanged`: the fixture's SHA-256 equals `manifest.json`'s `dbSha256`.
5. **A friend pastes a JPEG on a fresh PC** → it needs `imageformats\qjpeg.dll` in the zip. Test: Task 3 required-file list, and the `-SelfTest` case "missing plugin refused".

---

### Task 1: Version from git, version resource, version in the app

**Files:**
- Create:
  - `cmake/NoiteVersion.cmake` (`noite_version_from_describe(<describe> <base> <out_var>)` plus the configure-time driver)
  - `cmake/tests/version_test.cmake`
  - `app/src/version.h.in`, `app/src/noite.rc.in`
  - `app/tests/tst_version.cpp`
- Modify:
  - `CMakeLists.txt` (`project(... VERSION 0.1.0 ...)`, include the module, the ctest entry for the script test)
  - `app/CMakeLists.txt` (generated header, `.rc` on the `noite` target)
  - `app/src/main.cpp` (`setApplicationVersion`)
  - `app/src/App.cpp` (log line, tray tooltip, `trayToolTip()`)
  - `app/src/App.h`
  - `app/src/setup/SettingsDialog.cpp` (System label)
  - `tools/run-tests.ps1` (also run `ctest -R version` or `cmake -P` the script test)

**Interfaces:**
- Produces:
  - `noite_version.h` (generated into the build tree, on noite-app's include path): `#define NOITE_VERSION "<string>"`, `NOITE_VERSION_MAJOR`, `NOITE_VERSION_MINOR`, `NOITE_VERSION_PATCH`
  - `QString App::trayToolTip() const` (the text set on the tray, available without a tray)
  - The exe's `ProductVersion` string resource == `NOITE_VERSION` (Task 3 reads it)

- [ ] **Step 1: Write the failing tests**

`cmake/tests/version_test.cmake` (run with `cmake -P`; `message(FATAL_ERROR)` on any mismatch):
```cmake
# noite_version_from_describe(<describe> "0.1.0" out) for each case:
# "v0.1.0-0-gabc1234"            -> "0.1.0"
# "v0.1.0-3-gabc1234"            -> "0.1.0-dev+abc1234"
# "v0.1.0-0-gabc1234-dirty"      -> "0.1.0-dev+abc1234.dirty"
# "v0.0.9-12-gdef5678"           -> "0.1.0-dev+def5678"   (base is always the project version)
# "abc1234"                      -> "0.1.0-dev+abc1234"   (no tags: --always)
# "abc1234-dirty"                -> "0.1.0-dev+abc1234.dirty"
# "v0.2.0-rc1-0-gabc1234"        -> "0.1.0-dev+abc1234"
# "v1.0-0-gabc1234"              -> "0.1.0-dev+abc1234"
# ""                             -> "0.1.0-dev"           (git unavailable)
```
`app/tests/tst_version.cpp`:
```cpp
void versionHasTheExpectedForm() // QRegularExpression("^\\d+\\.\\d+\\.\\d+(-dev(\\+[0-9a-f]+(\\.dirty)?)?)?$") matches NOITE_VERSION
void versionStartsWithProjectVersion() // QString(NOITE_VERSION).startsWith(QString("%1.%2.%3").arg(MAJOR,MINOR,PATCH))
void appShowsVersion()           // App on a temp data folder (setupCompleted true): log file has "noite " NOITE_VERSION " started · data ";
                                 // trayToolTip() == "noite " NOITE_VERSION; the Settings dialog has a QLabel with that text
```
In `tst_version`, set `QCoreApplication::setApplicationVersion` the same way `main.cpp` does, through a shared one-liner, or assert that main's call uses `NOITE_VERSION`. The simplest form: assert `QCoreApplication::applicationVersion()` after calling a new `void applyAppIdentity()` from `app/src/AppOptions.h`, which main calls.

- [ ] **Step 2: Run to verify they fail**

Run: `cmake -P cmake/tests/version_test.cmake` → fails (the function doesn't exist); the test command → compile errors (`NOITE_VERSION`).

- [ ] **Step 3: Implement**

- **`noite_version_from_describe`:** a regex on the describe string. Tag part `^v([0-9]+)\.([0-9]+)\.([0-9]+)-([0-9]+)-g([0-9a-f]+)(-dirty)?$`. Only "count 0, clean, and tag == base" gives the bare base. Everything else is `<base>-dev+<sha>[.dirty]` (sha from `-g<sha>`, or the bare-sha form `^([0-9a-f]+)(-dirty)?$`). An empty string gives `<base>-dev`.
- **Driver:** `find_package(Git QUIET)`; `execute_process(git describe --tags --match "v*" --long --dirty --always)` in the source dir, ignoring errors; `configure_file` the header and the `.rc`.
- **Re-configure on new commits:** add `.git/HEAD` and the resolved ref file (when it exists) to `CMAKE_CONFIGURE_DEPENDS`.
- **Resource:** the `.rc` uses `VS_VERSION_INFO` with the constraint's strings, encoded as UTF-8 with `#pragma code_page(65001)` because the description has an em dash.
- **`applyAppIdentity()`:** `setApplicationName("noite")` and `setApplicationVersion(NOITE_VERSION)`; called in `main` before `App`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake -P cmake/tests/version_test.cmake` (no output, exit 0) and the test command → `TOTAL failures: 0`. Then `tools/build.ps1 -Config Release` and check `(Get-Item build\Release\noite.exe).VersionInfo.ProductVersion` matches the regex (paste it in the report).

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt cmake app tools
git commit -m "feat(build): version from git describe, exe version resource, version in the log, tray and Settings"
```

---

### Task 2: Golden data folders and the upgrade test

**Files:**
- Create:
  - `core/tests/golden/make_golden.cpp` (executable `noite_golden`, built but never run by CI)
  - `core/tests/golden/v1/noite.db`, `core/tests/golden/v1/manifest.json`
  - `core/tests/golden/v2/noite.db`, `core/tests/golden/v2/manifest.json`
  - `core/tests/tst_upgrade.cpp`
- Modify:
  - `core/CMakeLists.txt` (the `noite_golden` target; `tst_upgrade`; pass the golden dir to tests via a compile definition `NOITE_GOLDEN_DIR`)
  - `core/src/migrate.cpp` (comment next to the migration list)
  - `app/tests/tst_app.cpp` (golden v2 case)
  - `app/CMakeLists.txt` (`NOITE_GOLDEN_DIR` for app-tests)

**Interfaces:**
- Produces:
  - The fixtures and their `manifest.json` schema:
    - `{ "schema": int, "dbSha256": hex, "notes": [{ "title", "status": "active"|"done"|"trashed", "trashedAtMs"?, "color"?, "dueDate"? (ISO), "tags": [..], "bundle"? (name) }], "bundles": [{ "name", "members": [titles] }], "settings": { …Settings JSON as stored… }, "lastReviewDay"? (ISO), "jobState"? {…}, "attachment"? { "id", "size", "sha256", "noteTitle" } }`
    - `"searchWord": "golden"` and `"searchHit": <title>`
    - `"activeCount": int` (cards in the All tab)
  - `noite_golden <dir> <1|2>`: writes `<dir>/noite.db` and `<dir>/manifest.json`, with a fixed clock (2026-10-10 09:00 UTC) for every timestamp

- [ ] **Step 1: Write the generator and produce the fixtures**

- **Fixture contents (both versions; v1 has no attachment):**
  - 6 notes: 3 active (one with tags `#work #home`, one with a due date 2026-10-12, one in bundle "Groceries" with colour 2); 1 done; 1 trashed 2 days before the fixed clock; 1 more in "Groceries";
  - exactly one note contains the word `golden`;
  - settings with non-default values: `captureHotkey "Ctrl+Alt+K"`, `trashRetentionDays 40`, `dayStart 07:30`, `staleDays 9`, `backupDir "C:/noite-golden-backups"`, `backupFrequency Weekly`, `backupKeep 5`, `startWithWindows false`, `setupCompleted true`;
  - `last_review_day` 2026-10-09; for v2, a `job_state` with `lastPurgeDay` 2026-10-09.
- **v2:** through `Store` with a fixed `Clock`, plus one `putAttachment` of a deterministic 64×64 PNG made with `QImage`, referenced from the tagged note.
- **v1:** apply the embedded `schema_v1.sql` (`noite_schema.h`) and insert the same rows with SQL (`settings` JSON as v1 stored it; `user_version = 1`).
- **Run once:**
  - Run `build\Debug\noite_golden.exe core\tests\golden\v1 1` and the same for `v2 2`.
  - Commit the outputs.
  - Write `dbSha256` into each manifest after generating.
  - Add a header comment in `make_golden.cpp` saying: never re-run for an existing version; run only to add the folder for the current schema before a bump.

- [ ] **Step 2: Write the failing tests** (`tst_upgrade.cpp`, data-driven over every subfolder of `NOITE_GOLDEN_DIR`)

```cpp
void fixtureUnchanged_data(); void fixtureUnchanged()     // SHA-256 of golden/<v>/noite.db == manifest dbSha256
void opensAtLatestSchema_data(); void opensAtLatestSchema() // copy to a QTemporaryDir; DataDir::acquire + Store::open (fixed clock) →
                                                          // schemaVersion() == latest; manifest.schema < latest ⇒ migration-backups has
                                                          // exactly one "noite-v<schema>-*.db"; == latest ⇒ none
void contentMatchesManifest_data(); void contentMatchesManifest() // every manifest note found by title with its status, trashedAt, colour,
                                                          // due date, tags, bundle; bundles with members; settings() equal field by field;
                                                          // lastReviewDay; jobState (v2); attachmentBytes(id) size + sha256 (v2)
void boardAndSearch_data(); void boardAndSearch()         // All tab count == activeCount; search "golden" → exactly searchHit
void backupOfUpgradedFolderVerifies_data(); void backupOfUpgradedFolderVerifies() // backup::run to a temp dest → no throw (verify passed)
```
`tst_app.cpp`:
```cpp
void goldenV2StartsWithoutWizard() // copy golden/v2 to a temp data folder; App with the fake hotkey backend → start(): wizard() null;
                                   // backend has kCaptureId "Ctrl+Alt+K" and kBoardId registered; showBoard() → All tab card count == activeCount
```

- [ ] **Step 3: Run to verify they fail**

Run: the test command. Expected: `tst_upgrade` failures, e.g. before the fixtures exist, or with a deliberately wrong expected value while writing the test. Paste the output.

- [ ] **Step 4: Make them pass**

Fix the test or the generator until green. Do not change production code to make a fixture pass. If the current build can't open a fixture faithfully, that's a real bug: stop and report it.

Add the comment in `migrate.cpp` above the migration list:

```cpp
// Before adding migration N+1: run noite_golden for the current schema N (core/tests/golden/vN)
// and commit it, so tst_upgrade keeps proving that vN data opens.
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: the test command. Expected: `TOTAL failures: 0`.

- [ ] **Step 6: Commit**

```bash
git add core app
git commit -m "test(core): golden data folders v1/v2 and an upgrade test that opens them with the current build"
```

---

### Task 3: Packaging script and licence files

**Files:**
- Create:
  - `tools/package.ps1`
  - `packaging/README.txt` (verbatim from the spec, with a `<version>` placeholder)
  - `packaging/THIRD-PARTY.txt` (verbatim from the spec's bullet list, as plain text)
  - `packaging/licenses/LGPL-3.0.txt`, `packaging/licenses/GPL-3.0.txt` (the official FSF texts; from https://www.gnu.org/licenses/lgpl-3.0.txt and https://www.gnu.org/licenses/gpl-3.0.txt, unmodified)
- Modify: `.gitignore` if needed (dist outputs already ignored; check)

**Interfaces:**
- Consumes: Task 1's version resource (`(Get-Item noite.exe).VersionInfo.ProductVersion`).
- Produces:
  - `tools/package.ps1 [-Version <x.y.z>] [-DryRun] [-SelfTest] [-Dist <path>]`:
    - **Default** (`-Dist` = `dist\noite`, `-Version` required): checks the built exe's ProductVersion == `-Version` (else throws `exe version <a> ≠ release version <b>`). It also requires `-Version` to match `^\d+\.\d+\.\d+$`. Stages `noite\` (dist contents minus `data\`, plus the packaging files) and checks the required and forbidden lists. Writes `dist\noite-<v>-win64.zip`, `dist\noite-<v>-win64-pdb.zip` (from `build\Release\noite.pdb`) and `dist\SHA256SUMS.txt`, and prints the top-level entries and the sizes.
    - **`-DryRun`:** the same staging and checks, with the version read from the exe and no version-match check. Writes the zips under `dist\dry-run\`.
    - **`-SelfTest`:** runs the script's own checks against a synthetic dist in `$env:TEMP`, then exits 0 or throws. The cases:
      1. a complete dist passes and its zip has `noite\README.txt` containing the version;
      2. a dist with `data\noite.db` gives a zip without `data\`;
      3. a dist missing `imageformats\qjpeg.dll` is refused, naming the file;
      4. a `.pdb` inside the dist is refused;
      5. `-Version 0.2.0` against an exe reporting `0.1.0-dev+abc` is refused;
      6. `-Version 1.0` is refused (not x.y.z).

      The synthetic exe can be any small file whose version the script reads through an injectable `-ExeVersion` parameter (used only by the self-test).

- [ ] **Step 1: Write the self-test cases first** (inside the script, behind `-SelfTest`) and run `powershell -NoProfile -ExecutionPolicy Bypass -File tools/package.ps1 -SelfTest`. Expected: fails (the functions aren't written yet).

- [ ] **Step 2: Implement** the staging, checks, zips and checksums.
  - Zip with `System.IO.Compression.ZipFile`, entries under the `noite/` prefix.
  - SHA-256 with `Get-FileHash -Algorithm SHA256`, lower-case hex, two spaces, file name.
  - Write `README.txt` as UTF-8 with CRLF line endings, so Notepad shows it properly.

- [ ] **Step 3: Verify**
  - `tools/package.ps1 -SelfTest` passes.
  - Then run `tools/build.ps1 -Config Release`, `tools/deploy.ps1` and `tools/package.ps1 -DryRun`: the zip is listed, with no `data\`, and `SHA256SUMS.txt` has 2 lines.
  - Paste the outputs, including the zip size, in the report.

- [ ] **Step 4: Commit**

```bash
git add tools/package.ps1 packaging .gitignore
git commit -m "feat(release): package script (zip, PDB zip, checksums, licence notices) with dry-run and self-test"
```

---

### Task 4: CI release job and docs

**Files:**
- Modify:
  - `.github/workflows/ci.yml`
  - `README.md`
  - `docs/spec.md` (D23, D26, D28)
  - `docs/superpowers/plans/2026-10-15-noite-roadmap.md` (M7 row → M7a–M7d, the M7a plan link)
  - `docs/testing/manual-capture-checks.md` (a new "Release" section, R-1 to R-3, verbatim from the spec)

**Interfaces:**
- Consumes: Task 3's `tools/package.ps1` (`-SelfTest`, `-DryRun`, `-Version`); `tools/deploy.ps1`.

- [ ] **Step 1: Edit `ci.yml`**

- **Job `build`:**
  1. `actions/checkout@v4` with `fetch-depth: 0`;
  2. after Test: `tools/deploy.ps1 -Config Release -QtDir $env:QT_ROOT_DIR`;
  3. `tools/package.ps1 -SelfTest`, then `tools/package.ps1 -DryRun`;
  4. upload the artifact `noite-dist`: `dist/noite/**` and `build/Release/noite.pdb`, `retention-days: 1`.
- **Job `release`:** as in Global Constraints.
  1. `actions/checkout@v4` (for `tools/` and `packaging/`), then `actions/download-artifact@v4` into the same paths;
  2. `$v = "${{ github.ref_name }}".Substring(1)`; `tools/package.ps1 -Version $v`;
  3. `gh release create ${{ github.ref_name }} --draft --generate-notes --title "noite $v" dist/noite-$v-win64.zip dist/noite-$v-win64-pdb.zip dist/SHA256SUMS.txt` with `GH_TOKEN: ${{ github.token }}`.

  `deploy.ps1` launches the VS dev shell itself. In CI the `msvc-dev-cmd` step already did that, so make `deploy.ps1` skip `Launch-VsDevShell` when `$env:VCToolsRedistDir` is already set. Its CRT copy needs that variable.

- [ ] **Step 2: Validate the workflow locally where possible**
  - Parse the YAML: `python -c "import yaml,sys; yaml.safe_load(open('.github/workflows/ci.yml'))"`, if Python is available, or `ConvertFrom-Yaml` if the module exists.
  - Run every new PowerShell step's command line locally against the Release build: `deploy.ps1`, `package.ps1 -SelfTest`, `package.ps1 -DryRun`. Paste the outputs.
  - The real CI run is the controller's job, after pushing the branch.

- [ ] **Step 3: Docs** as listed in the spec's "Docs" section.

- [ ] **Step 4: Run the full suite** (the test command) → `TOTAL failures: 0`.

- [ ] **Step 5: Commit**

```bash
git add .github tools docs README.md
git commit -m "ci: package and check the portable zip on every push; tags create a draft release; docs for downloading, building and releasing"
```
