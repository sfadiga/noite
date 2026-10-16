# Scoring for run-trials.ps1. A trial fails if it never became ready (no ready_ms: the
# editor never painted with focus) or if the editor text is not exactly "hello"
# (case-sensitive, so a stuck Shift/Caps Lock is caught).
function Measure-Trials($rows) {
    $rows = @($rows)
    $failed = @($rows | Where-Object { $_.ready_ms -eq "" -or $_.text -cne "hello" })
    $ms = @($rows | Where-Object { $_.ready_ms -ne "" } | ForEach-Object { [double]$_.ready_ms } | Sort-Object)
    function Rank([double]$p) { if ($ms.Count -eq 0) { return $null }; $ms[[math]::Max(0, [math]::Ceiling($p * $ms.Count) - 1)] }
    [pscustomobject]@{
        Failed = $failed
        Ready  = $ms.Count
        First  = if ($rows.Count -gt 0 -and $rows[0].ready_ms -ne "") { [double]$rows[0].ready_ms } else { $null }
        P50    = Rank 0.50
        P95    = Rank 0.95
        Max    = if ($ms.Count -gt 0) { $ms[-1] } else { $null }
    }
}
