# Throwaway spike probe: memory of hotkey-latency.exe plus its whole WebView2 process tree.
$ErrorActionPreference = "Stop"

$root = Get-Process hotkey-latency | Select-Object -First 1
$all = Get-CimInstance Win32_Process
$ids = New-Object System.Collections.Generic.List[int]
$ids.Add($root.Id)
for ($i = 0; $i -lt $ids.Count; $i++) {
    $all | Where-Object { $_.ParentProcessId -eq $ids[$i] } | ForEach-Object { $ids.Add([int]$_.ProcessId) }
}

$procs = $ids | ForEach-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue }
$procs | Select-Object Name, Id,
    @{ n = "WorkingSetMB"; e = { [math]::Round($_.WorkingSet64 / 1MB, 1) } },
    @{ n = "PrivateMB"; e = { [math]::Round($_.PrivateMemorySize64 / 1MB, 1) } } |
    Format-Table -AutoSize | Out-String -Width 120

"TOTAL processes={0} working_set={1:N1} MB private={2:N1} MB" -f $procs.Count,
    (($procs | Measure-Object WorkingSet64 -Sum).Sum / 1MB),
    (($procs | Measure-Object PrivateMemorySize64 -Sum).Sum / 1MB)
