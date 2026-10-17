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

"TOTAL failures: $total"
exit $total
