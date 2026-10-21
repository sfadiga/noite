# End-to-end run (M7c): drives a real noite with real keystrokes and checks each step in its
# database: capture a note with the hotkey, open the board, mark it done, trash it from
# Completed, restore it from Trash.
#
# IT SENDS REAL KEYSTROKES AND TAKES FOCUS. CI runs it on every push; on a PC, run it only
# while you are away from the keyboard.
#
#   run-e2e.ps1 [-Exe <noite.exe>] [-Out <folder>]
#
# Prints "setup|step <n> PASS|FAIL <detail>", "leaked_chars=<n>", then "E2E PASS" or
# "E2E FAIL"; exits 0 only on a pass. <Out> keeps run.txt, noite.log and leaked.txt.
param(
    [string]$Exe = (Join-Path (Split-Path (Split-Path $PSScriptRoot)) "dist\noite\noite.exe"),
    [string]$Out = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class E2eForeground {
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    // The process owning the foreground window; nothing about the window itself is read.
    public static uint Pid() {
        uint pid;
        GetWindowThreadProcessId(GetForegroundWindow(), out pid);
        return pid;
    }
}
"@

if (-not $Out) { $Out = Join-Path $env:TEMP ("noite-e2e-" + (Get-Date -Format "yyyyMMdd-HHmmss")) }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$data = Join-Path $Out "data"
$db = Join-Path $data "noite.db"
$noiteLog = Join-Path $data "logs\noite.log"
$leak = Join-Path $Out "leaked.txt"
$runLog = Join-Path $Out "run.txt"
$checker = Join-Path $PSScriptRoot "noite_db.py"
$text = "e2e buy milk"
Set-Content -Path $runLog -Value "" -Encoding UTF8

function Say([string]$line) {
    Write-Output $line
    Add-Content -Path $runLog -Value $line -Encoding UTF8
}

# Polls $condition every 100 ms; $false after $seconds.
function Wait-Until([scriptblock]$condition, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while (-not (& $condition)) {
        if ((Get-Date) -gt $deadline) { return $false }
        Start-Sleep -Milliseconds 100
    }
    return $true
}

function Started-Lines {
    if (-not (Test-Path $noiteLog)) { return 0 }
    return @(Get-Content $noiteLog -Encoding UTF8 | Where-Object { $_ -match " started " }).Count
}

# The "hotkeys: capture <combo> ok|unavailable . board <combo> ok|unavailable" line noite logs
# once it answers its hotkeys; $null until then.
function Hotkeys-Line {
    if (-not (Test-Path $noiteLog)) { return $null }
    return Get-Content $noiteLog -Encoding UTF8 | Where-Object { $_ -match "hotkeys: capture " } | Select-Object -Last 1
}

function Start-Noite {
    return Start-Process $Exe -ArgumentList "--data", "`"$data`"" -PassThru
}

$python = $null
$first = $null
$noite = $null
$target = $null
$failed = $false
try {
    $python = (Get-Command python -ErrorAction SilentlyContinue).Source
    if (-not $python) { throw "setup FAIL: python not found" }
    if (-not (Test-Path $Exe)) { throw "setup FAIL: no noite at $Exe" }
    # Another noite holds the hotkeys: the run's keystrokes would land in it (and its data).
    if (Get-Process noite -ErrorAction SilentlyContinue) { throw "setup FAIL: quit the running noite first" }

    # A fresh data folder: the first start creates the database (the schema is committed once
    # prepare can read the settings table), then setup is marked done so the run doesn't
    # meet the first-run wizard.
    $first = Start-Noite
    $ready = Wait-Until {
        if (-not (Test-Path $db)) { return $false }
        $ErrorActionPreference = "Continue" # this scope only: "no such table" on stderr means "not yet"
        & $python -I $checker prepare $db 2>$null | Out-Null
        return $LASTEXITCODE -eq 0
    } 10
    if (-not $ready) { throw "setup FAIL: noite did not create its database" }
    Stop-Process -Id $first.Id -Force
    $first.WaitForExit(5000) | Out-Null

    $target = Start-Process powershell -PassThru -WindowStyle Minimized -ArgumentList @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$(Join-Path $PSScriptRoot 'harness-target.ps1')`"", "-Out", "`"$leak`"")
    if (-not (Wait-Until { Test-Path "$leak.ready" } 10)) { throw "setup FAIL: the harness target window did not open" }

    $noite = Start-Noite
    if (-not (Wait-Until { (Started-Lines) -ge 2 -and (Hotkeys-Line) } 10)) { throw "setup FAIL: noite did not start" }
    if ((Hotkeys-Line) -match "unavailable") { throw "setup FAIL: a hotkey is taken by another program" }
    Say "setup PASS"

    # Every keystroke goes only to the process meant to have the foreground: the target
    # (hotkeys) or noite (everything else). Checked by process id before each send.
    $shell = New-Object -ComObject WScript.Shell
    function Activate-Target([int]$step) {
        $shell.AppActivate("noite-e2e-target") | Out-Null
        if (-not (Wait-Until { [E2eForeground]::Pid() -eq $target.Id } 5)) { throw "step $step FAIL the harness target did not take the foreground" }
    }
    function Send-Hotkey([int]$step, [string]$keys) {
        if ([E2eForeground]::Pid() -ne $target.Id) { throw "step $step FAIL the harness target lost the foreground" }
        [System.Windows.Forms.SendKeys]::SendWait($keys)
    }
    function Send([int]$step, [string]$keys) {
        if (-not (Wait-Until { [E2eForeground]::Pid() -eq $noite.Id } 5)) { throw "step $step FAIL noite did not take the foreground" }
        [System.Windows.Forms.SendKeys]::SendWait($keys)
        Start-Sleep -Milliseconds 300 # paces the keys; never decides pass or fail
    }
    function Expect([int]$step, [string]$status, [string]$from = "") {
        $arguments = @("-I", $checker, "expect", $db, "--text", $text, "--status", $status)
        if ($from) { $arguments += @("--trashed-from", $from) }
        $message = & $python @arguments
        if ($LASTEXITCODE -ne 0) { throw "step $step FAIL $message" }
        Say "step $step PASS $message"
    }

    # 1. Capture a note with the global hotkey; Esc saves and closes it.
    Activate-Target 1
    Send-Hotkey 1 "^%j"
    Send 1 $text
    Send 1 "{ESC}"
    Expect 1 "active"

    # 2. Open the board with its hotkey; Today first, then mark the selected note done.
    Activate-Target 2
    Send-Hotkey 2 "^%b"
    Send 2 "^1"
    Send 2 "d"
    Expect 2 "done"

    # 3. Completed tab: trash it.
    Send 3 "^3"
    Send 3 "{DEL}"
    Expect 3 "trashed" "done"

    # 4. Trash tab (Ctrl+5 since the Deferred tab, M8c): restore it (back to Completed).
    Send 4 "^5"
    Send 4 "{DEL}"
    Expect 4 "done"
} catch {
    $failed = $true
    Say $_.Exception.Message
} finally {
    if ($target) {
        New-Item -ItemType File -Force "$leak.stop" | Out-Null
        if (-not $target.WaitForExit(5000)) { $target.Kill() }
        Remove-Item "$leak.stop", "$leak.ready" -ErrorAction SilentlyContinue
    }
    foreach ($p in @($noite, $first)) {
        if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    }
    if (Test-Path $noiteLog) { Copy-Item $noiteLog (Join-Path $Out "noite.log") }
}

$leaked = if (Test-Path $leak) { [IO.File]::ReadAllText($leak) } else { "" }
Say "leaked_chars=$($leaked.Length)"
if ($leaked.Length -gt 0) { $failed = $true }
if ($failed) { Say "E2E FAIL"; exit 1 }
Say "E2E PASS"
exit 0
