# Runs core-tests offscreen. QTest output does not come through a pipe on this setup, so
# each test class writes build/<Config>/test-results/<Class>.txt, printed afterwards.
param(
    [ValidateSet("Debug", "Release")][string]$Config = "Debug",
    [string]$QtDir = "$env:USERPROFILE\Qt\6.10.3\msvc2022_64"
)

$src = Split-Path $PSScriptRoot
$bin = Join-Path $src "build\$Config"
$out = Join-Path $bin "test-results"
Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory $out | Out-Null

$env:QT_QPA_PLATFORM = "offscreen"
$env:QT_PLUGIN_PATH = "$QtDir\plugins"
$env:PATH = "$QtDir\bin;$env:PATH"
$env:NOITE_TEST_OUT = $out
if (-not $env:QT_QPA_FONTDIR) { $env:QT_QPA_FONTDIR = "$env:WINDIR\Fonts" }

$p = Start-Process (Join-Path $bin "core-tests.exe") -PassThru -NoNewWindow
$null = $p.Handle  # without this PowerShell 5.1 loses ExitCode after WaitForExit(timeout)
if (-not $p.WaitForExit(300000)) { Stop-Process -Id $p.Id -Force; throw "tests timed out" }

Get-ChildItem $out -Filter *.txt | Sort-Object Name | ForEach-Object { Get-Content $_.FullName }
"TOTAL failures: $($p.ExitCode)"
exit $p.ExitCode
