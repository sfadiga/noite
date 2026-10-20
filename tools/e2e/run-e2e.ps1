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
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    public static bool TitleIs(string title) {
        var s = new StringBuilder(256);
        GetWindowText(GetForegroundWindow(), s, 256);
        return s.ToString() == title;
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

    # A fresh data folder: the first start creates the database, then setup is marked done
    # so the run doesn't meet the first-run wizard.
    $first = Start-Noite
    if (-not (Wait-Until { (Test-Path $db) -and (Started-Lines) -ge 1 } 10)) { throw "setup FAIL: noite did not create its data folder" }
    Stop-Process -Id $first.Id -Force
    $first.WaitForExit(5000) | Out-Null
    $prepared = & $python -I $checker prepare $db # stdout only: under "Stop", 2>&1 would make stderr fatal
    if ($LASTEXITCODE -ne 0) { throw "setup FAIL: prepare: $prepared" }

    $target = Start-Process powershell -PassThru -WindowStyle Minimized -ArgumentList @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$(Join-Path $PSScriptRoot 'harness-target.ps1')`"", "-Out", "`"$leak`"")
    if (-not (Wait-Until { Test-Path "$leak.ready" } 10)) { throw "setup FAIL: the harness target window did not open" }

    $noite = Start-Noite
    if (-not (Wait-Until { (Started-Lines) -ge 2 } 10)) { throw "setup FAIL: noite did not start" }
    Say "setup PASS"

    $shell = New-Object -ComObject WScript.Shell
    function Activate-Target {
        if (-not $shell.AppActivate("noite-e2e-target")) { throw "setup FAIL: could not activate the harness target" }
        Start-Sleep -Milliseconds 300
    }
    function Send([string]$keys) {
        [System.Windows.Forms.SendKeys]::SendWait($keys)
        Start-Sleep -Milliseconds 300
    }
    function Expect([int]$step, [string]$status, [string]$from = "") {
        $arguments = @("-I", $checker, "expect", $db, "--text", $text, "--status", $status)
        if ($from) { $arguments += @("--trashed-from", $from) }
        $message = & $python @arguments
        if ($LASTEXITCODE -ne 0) { throw "step $step FAIL $message" }
        Say "step $step PASS $message"
    }

    # 1. Capture a note with the global hotkey; Esc saves and closes it.
    Activate-Target
    Send "^%j"
    Start-Sleep -Milliseconds 100
    Send $text
    Send "{ESC}"
    Expect 1 "active"

    # 2. Open the board with its hotkey; Today first, then mark the selected note done.
    Activate-Target
    Send "^%b"
    if (-not (Wait-Until { [E2eForeground]::TitleIs("noite") } 10)) { throw "step 2 FAIL the board did not come to the foreground" }
    Send "^1"
    Send "d"
    Expect 2 "done"

    # 3. Completed tab: trash it.
    Send "^3"
    Send "{DEL}"
    Expect 3 "trashed" "done"

    # 4. Trash tab: restore it (back to Completed).
    Send "^4"
    Send "{DEL}"
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
