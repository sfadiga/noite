# Throwaway spike harness: fires the capture hotkey from a dedicated foreground
# window (harness-target.ps1), types immediately, and checks every keystroke
# landed in the capture editor. Never targets the user's own apps.
param(
    [int]$Trials = 50,
    [string]$Label = "run",
    [string]$Hotkey = "^%j",  # Ctrl+Alt+J (Ctrl+Alt+N is taken on the dev machine)
    [string]$Process = "hotkey-latency",
    [string]$Csv = (Join-Path $PSScriptRoot "results\trials.csv")
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms

$csv = [IO.Path]::GetFullPath($Csv)
$results = Split-Path $csv
if (-not (Get-Process $Process -ErrorAction SilentlyContinue)) { throw "$Process.exe is not running" }

$before = if (Test-Path $csv) { @(Import-Csv $csv).Count } else { 0 }

New-Item -ItemType Directory -Force $results | Out-Null
$leakFile = Join-Path $results "$Label-leaked.txt"
Remove-Item $leakFile, "$leakFile.stop", "$leakFile.ready" -ErrorAction SilentlyContinue
$targetTitle = "noite-harness-target"
$target = Start-Process powershell -PassThru -WindowStyle Minimized -ArgumentList @(
    "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$(Join-Path $PSScriptRoot 'harness-target.ps1')`"", "-Out", "`"$leakFile`"")
$shell = New-Object -ComObject WScript.Shell
$deadline = (Get-Date).AddSeconds(10)
while (-not (Test-Path "$leakFile.ready")) {
    if ((Get-Date) -gt $deadline) { throw "harness target window did not become ready" }
    Start-Sleep -Milliseconds 200
}

for ($i = 1; $i -le $Trials; $i++) {
    if (-not $shell.AppActivate($targetTitle)) { throw "could not activate harness target" }
    Start-Sleep -Milliseconds 300
    [System.Windows.Forms.SendKeys]::SendWait($Hotkey)
    [System.Windows.Forms.SendKeys]::SendWait("hello")
    Start-Sleep -Milliseconds 400
    [System.Windows.Forms.SendKeys]::SendWait("{ESC}")
    Start-Sleep -Milliseconds 700
}

New-Item -ItemType File -Force "$leakFile.stop" | Out-Null
if (-not $target.WaitForExit(5000)) { $target.Kill(); throw "harness target did not close; leak check unavailable" }
Remove-Item "$leakFile.stop", "$leakFile.ready"
$leaked = if (Test-Path $leakFile) { [IO.File]::ReadAllText($leakFile) } else { "" }

$rows = @(Import-Csv $csv | Select-Object -Skip $before)
$rows | Export-Csv -NoTypeInformation (Join-Path $results "$Label.csv")

$failed = @($rows | Where-Object { $_.text -ne "hello" })
$ms = @($rows | Where-Object { $_.ready_ms -ne "" } | ForEach-Object { [double]$_.ready_ms } | Sort-Object)
function Rank([double]$p) { if ($ms.Count -eq 0) { return $null }; $ms[[math]::Max(0, [math]::Ceiling($p * $ms.Count) - 1)] }

"label=$Label trials_sent=$Trials rows=$($rows.Count) failed=$($failed.Count) missing_rows=$($Trials - $rows.Count) leaked_chars=$($leaked.Length)"
if ($ms.Count -gt 0) {
    "first={0:N2} p50={1:N2} p95={2:N2} max={3:N2} (ms)" -f [double]$rows[0].ready_ms, (Rank 0.50), (Rank 0.95), $ms[-1]
}
$failed | ForEach-Object { "FAILED trial $($_.trial): text='$($_.text)'" }
if ($leaked.Length -gt 0) { "LEAKED into foreground app: '$leaked'" }
