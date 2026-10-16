# Throwaway spike diagnostic: logs the PROCESS NAME of every foreground-window change
# (10 ms polling) until "$Out.stop" appears. Never records window titles: other apps'
# titles can contain private information.
param([Parameter(Mandatory)][string]$Out)

Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Fg {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
}
'@
"time,pid,process" | Set-Content $Out
$last = [IntPtr]::Zero
while (-not (Test-Path "$Out.stop")) {
    $h = [Fg]::GetForegroundWindow()
    if ($h -ne $last) {
        $last = $h
        $procId = 0; [void][Fg]::GetWindowThreadProcessId($h, [ref]$procId)
        $name = (Get-Process -Id $procId -ErrorAction SilentlyContinue).Name
        "{0},{1},{2}" -f (Get-Date).ToString("HH:mm:ss.fff"), $procId, $name | Add-Content $Out
    }
    Start-Sleep -Milliseconds 10
}
Remove-Item "$Out.stop"
