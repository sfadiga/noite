# One entry point for the everyday loop: runs the given steps in order and stops at the
# first failure.
#
#   tools\dev.ps1 build test             build, then run the suites
#   tools\dev.ps1 ci                     clean + build + test, like CI's build job
#   tools\dev.ps1 build run              build, then start it on a scratch data folder
#   tools\dev.ps1 deploy run-dist        refresh dist\noite and start it (your real notes)
#
# Steps:
#   clean      delete build\<Config> (never dist\: dist\noite\data holds your notes)
#   build      tools\build.ps1: configure + build
#   test       tools\run-tests.ps1: every suite, offscreen
#   deploy     tools\deploy.ps1: dist\noite (refuses while that noite is running)
#   run        start build\<Config>\noite.exe on build\<Config>\dev-data (or -Data), with the
#              Qt it was built against. If your own noite is running, the hotkeys stay
#              with it and this one reports them as unavailable.
#   run-dist   start dist\noite\noite.exe on its own data folder, unless it is already running
#   ci         clean, build, test
#
# The end-to-end test is not a step: it sends real keystrokes (tools\e2e\run-e2e.ps1, only
# while you are away from the keyboard).
param(
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)][string[]]$Steps,
    [ValidateSet("Debug", "Release")][string]$Config = "Release",
    [string]$QtDir = "$env:USERPROFILE\Qt\6.10.3\msvc2022_64",
    [string]$Data
)

$ErrorActionPreference = "Stop"
$src = Split-Path $PSScriptRoot
$bin = Join-Path $src "build\$Config"
$dist = Join-Path $src "dist\noite"
$known = @("clean", "build", "test", "deploy", "run", "run-dist", "ci")

function Usage {
    "usage: tools\dev.ps1 <step> [<step> ...] [-Config Debug|Release] [-Data <dir>]"
    "steps: $($known -join ', ')"
}

# Child scripts run in their own PowerShell, so their 'throw' or 'exit' ends only them.
function Invoke-Tool([string]$name, [string[]]$toolArgs) {
    "== $name"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $name) @toolArgs
    if ($LASTEXITCODE) { throw "$name failed (exit $LASTEXITCODE)" }
}

function Get-NoiteFrom([string]$folder) {
    @(Get-Process noite -ErrorAction SilentlyContinue | Where-Object {
            $_.Path -and $_.Path.StartsWith($folder + "\", [StringComparison]::OrdinalIgnoreCase) })
}

function Step-Clean {
    "== clean $bin"
    $running = Get-NoiteFrom $bin
    if ($running.Count) { throw "noite is running from $bin (PID $($running.Id -join ', ')); quit it first" }
    if (Test-Path $bin) { Remove-Item $bin -Recurse -Force }
}

function Step-Run {
    $exe = Join-Path $bin "noite.exe"
    if (-not (Test-Path $exe)) { throw "build first: $exe not found" }
    $dataDir = if ($Data) { $Data } else { Join-Path $bin "dev-data" }
    "== run $exe --data $dataDir"
    # Inherited by the child: the Qt DLLs and plugins of the build, as run-tests.ps1 does.
    $env:PATH = "$QtDir\bin;$env:PATH"
    $env:QT_PLUGIN_PATH = "$QtDir\plugins"
    Start-Process -FilePath $exe -ArgumentList "--data", "`"$dataDir`"" -WorkingDirectory $bin | Out-Null
}

function Step-RunDist {
    $exe = Join-Path $dist "noite.exe"
    if (-not (Test-Path $exe)) { throw "deploy first: $exe not found" }
    $running = Get-NoiteFrom $dist
    if ($running.Count) { "== run-dist: already running (PID $($running.Id -join ', '))"; return }
    "== run-dist $exe"
    Start-Process -FilePath $exe -WorkingDirectory $dist | Out-Null
}

if (-not $Steps) { Usage; exit 2 }
$expanded = foreach ($s in $Steps) { if ($s -eq "ci") { "clean"; "build"; "test" } else { $s } }
foreach ($s in $expanded) {
    if ($known -notcontains $s) { "unknown step: $s"; Usage; exit 2 }
}

try {
    foreach ($s in $expanded) {
        switch ($s) {
            "clean" { Step-Clean }
            "build" { Invoke-Tool "build.ps1" @("-Config", $Config, "-QtDir", $QtDir) }
            "test" { Invoke-Tool "run-tests.ps1" @("-Config", $Config, "-QtDir", $QtDir) }
            "deploy" { Invoke-Tool "deploy.ps1" @("-Config", $Config, "-QtDir", $QtDir) }
            "run" { Step-Run }
            "run-dist" { Step-RunDist }
        }
    }
} catch {
    "FAILED: $($_.Exception.Message)"
    exit 1
}
"done: $($expanded -join ' ')"
