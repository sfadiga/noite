# Throwaway spike: runs the editor capability tests offscreen. QTest output does not
# come through a pipe here, so results go to a file that is printed afterwards.
param(
    [ValidateSet("Debug", "Release")][string]$Config = "Release",
    [string]$QtDir = "$env:USERPROFILE\Qt\6.10.3\msvc2022_64"
)

$env:QT_QPA_PLATFORM = "offscreen"
$env:QT_PLUGIN_PATH = "$QtDir\plugins"
$env:PATH = "$QtDir\bin;$env:PATH"

$exe = Join-Path $PSScriptRoot "build\$Config\qt-capture-tests.exe"
$out = Join-Path $PSScriptRoot "build\$Config\tests.txt"
Remove-Item $out -ErrorAction SilentlyContinue
$p = Start-Process $exe -ArgumentList "-o", "$out,txt" -PassThru -NoNewWindow
if (-not $p.WaitForExit(120000)) { Stop-Process -Id $p.Id -Force; Get-Content $out -ErrorAction SilentlyContinue; throw "tests timed out" }
Get-Content $out
exit $p.ExitCode
