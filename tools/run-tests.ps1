# Runs every test suite offscreen. QTest output does not come through a pipe on this setup,
# so each test class writes build/<Config>/test-results/<suite>/<Class>.txt, printed afterwards.
param(
    [ValidateSet("Debug", "Release")][string]$Config = "Debug",
    [string]$QtDir = "$env:USERPROFILE\Qt\6.10.3\msvc2022_64"
)

$src = Split-Path $PSScriptRoot
$bin = Join-Path $src "build\$Config"
$results = Join-Path $bin "test-results"
Remove-Item $results -Recurse -Force -ErrorAction SilentlyContinue

$env:QT_QPA_PLATFORM = "offscreen"
$env:QT_PLUGIN_PATH = "$QtDir\plugins"
$env:PATH = "$QtDir\bin;$env:PATH"
if (-not $env:QT_QPA_FONTDIR) { $env:QT_QPA_FONTDIR = "$env:WINDIR\Fonts" }

$suites = @(
    @{ Name = "core"; Exe = Join-Path $bin "core-tests.exe" },
    @{ Name = "app"; Exe = Join-Path $bin "app-tests.exe" }
)

$total = 0
foreach ($suite in $suites) {
    $out = Join-Path $results $suite.Name
    New-Item -ItemType Directory -Force $out | Out-Null
    $env:NOITE_TEST_OUT = $out
    if (-not (Test-Path $suite.Exe)) { "MISSING test executable: $($suite.Exe)"; $total += 1; continue }

    $p = Start-Process $suite.Exe -PassThru -NoNewWindow
    $null = $p.Handle  # without this PowerShell 5.1 loses ExitCode after WaitForExit(timeout)
    if (-not $p.WaitForExit(300000)) { Stop-Process -Id $p.Id -Force; "TIMEOUT: $($suite.Name)"; $total += 1; continue }

    $files = @(Get-ChildItem $out -Filter *.txt | Sort-Object Name)
    $files | ForEach-Object { Get-Content $_.FullName }
    if ($files.Count -eq 0) { "NO RESULTS from suite $($suite.Name) (empty registry or crash)"; $total += 1 }
    "suite $($suite.Name): exit $($p.ExitCode), $($files.Count) test classes"
    $total += [math]::Abs($p.ExitCode)
}

# The version logic is a CMake script test (no Qt): a mismatch counts toward the total.
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath
    if ($vs) { $cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" }
}
if ($cmake -and (Test-Path $cmake)) {
    & $cmake -P (Join-Path $src "cmake\tests\version_test.cmake")
    $versionExit = $LASTEXITCODE
    "suite version: exit $versionExit"
    $total += [math]::Abs($versionExit)
} else {
    "MISSING cmake for the version test"
    $total += 1
}

# The release tools carry their own checks on temp files; a failure counts toward the total.
foreach ($tool in @("changelog-section.ps1", "bump-version.ps1")) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $tool) -SelfTest
    $toolExit = $LASTEXITCODE
    "suite $tool -SelfTest: exit $toolExit"
    $total += [math]::Abs($toolExit)
}

# The end-to-end run's database checker carries its own checks on a temporary database.
$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if ($python) {
    & $python -I (Join-Path $PSScriptRoot "e2e\noite_db.py") --self-test
    $dbExit = $LASTEXITCODE
    "suite noite_db --self-test: exit $dbExit"
    $total += [math]::Abs($dbExit)
} else {
    "MISSING python for the e2e checker self-test"
    $total += 1
}

"TOTAL failures: $total"
exit $total
