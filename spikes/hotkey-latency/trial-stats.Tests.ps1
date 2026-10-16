# Pester 3 tests for trial scoring (run: Invoke-Pester spikes\hotkey-latency\trial-stats.Tests.ps1).
. (Join-Path $PSScriptRoot "trial-stats.ps1")

function Row($trial, $ready, $text) { [pscustomobject]@{ trial = "$trial"; ready_ms = $ready; text = $text } }

Describe "Measure-Trials" {
    It "counts a row without ready_ms as failed" {
        $s = Measure-Trials @((Row 1 "7.10" "hello"), (Row 2 "" "hello"))
        $s.Failed.Count | Should Be 1
        $s.Failed[0].trial | Should Be "2"
    }
    It "is case-sensitive about the typed text" {
        $s = Measure-Trials @((Row 1 "7.10" "HELLO"))
        $s.Failed.Count | Should Be 1
    }
    It "computes nearest-rank percentiles over rows with a ready time" {
        $rows = 1..20 | ForEach-Object { Row $_ ("{0}.00" -f $_) "hello" }
        $s = Measure-Trials $rows
        $s.P50 | Should Be 10
        $s.P95 | Should Be 19
        $s.Max | Should Be 20
        $s.First | Should Be 1
    }
    It "reports no first value when the first row never became ready" {
        $s = Measure-Trials @((Row 1 "" "hello"), (Row 2 "5.00" "hello"))
        $s.First | Should Be $null
    }
}
