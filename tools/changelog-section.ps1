# Extracts one release's section from CHANGELOG.md (Keep a Changelog format).
#
#   changelog-section.ps1 -Version 1.0.0                    prints the body of "## [1.0.0] - ..."
#   changelog-section.ps1 -Version 1.0.0 -Out notes.md      writes it to a file (UTF-8, no BOM)
#   changelog-section.ps1 -SelfTest                         runs the script's own checks in %TEMP%
#
# The body is the lines after the heading up to the next "## [" or the link-reference block at
# the bottom, trimmed. A missing or empty section is an error: CI uses this for release notes.
param(
    [string]$Version,
    [string]$Path = (Join-Path (Split-Path $PSScriptRoot) "CHANGELOG.md"),
    [string]$Out,
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Get-ChangelogSection([string]$Text, [string]$Version) {
    $lines = $Text -split "`r?`n"
    $heading = '^## \[' + [regex]::Escape($Version) + '\]'
    $start = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match $heading) { $start = $i; break }
    }
    if ($start -lt 0) { throw "CHANGELOG.md has no entry for $Version" }

    $body = New-Object System.Collections.Generic.List[string]
    for ($i = $start + 1; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^## \[' -or $lines[$i] -match '^\[[^\]]+\]:\s') { break }
        $body.Add($lines[$i])
    }
    $result = ($body -join "`n").Trim()
    if ($result.Length -eq 0) { throw "CHANGELOG.md has no entry for $Version" }
    return $result
}

function Invoke-SelfTest {
    $script:failures = 0
    function Check([string]$Name, [bool]$Ok) {
        if (-not $Ok) { Write-Host "FAIL: $Name"; $script:failures++ }
    }
    function Throws([scriptblock]$Block) {
        try { & $Block } catch { return $_.Exception.Message }
        return $null
    }

    $text = @"
# Changelog

## [Unreleased]

## [1.0.10] - 2026-11-01

### Fixed

- ten

## [1.0.1] - 2026-10-25

### Added

- one

## [1.0.0] - 2026-10-19

### Added

- first
- second

## [0.9.0] - 2026-01-01

## [Unreleased]: not a link line

[Unreleased]: https://example.invalid/compare/v1.0.10...HEAD
[1.0.0]: https://example.invalid/releases/tag/v1.0.0
"@
    foreach ($eol in @("`n", "`r`n")) {
        $t = $text -replace "`r?`n", $eol
        $tag = if ($eol -eq "`n") { "LF" } else { "CRLF" }
        Check "1.0.0 body ($tag)" ((Get-ChangelogSection $t "1.0.0") -ceq "### Added`n`n- first`n- second")
        Check "1.0.1 body ($tag)" ((Get-ChangelogSection $t "1.0.1") -ceq "### Added`n`n- one")
        Check "1.0.10 body ($tag)" ((Get-ChangelogSection $t "1.0.10") -ceq "### Fixed`n`n- ten")
        $msg = Throws { Get-ChangelogSection $t "2.0.0" }
        Check "missing version ($tag)" ($msg -ceq "CHANGELOG.md has no entry for 2.0.0")
        $msg = Throws { Get-ChangelogSection $t "0.9.0" }
        Check "empty section ($tag)" ($msg -ceq "CHANGELOG.md has no entry for 0.9.0")
    }

    # The last section before the link block must not swallow the links.
    $last = "# Changelog`n`n## [1.0.0] - 2026-10-19`n`n- only`n`n[Unreleased]: https://x/compare/v1.0.0...HEAD`n[1.0.0]: https://x/tag/v1.0.0`n"
    Check "last section stops at links" ((Get-ChangelogSection $last "1.0.0") -ceq "- only")

    # File round trip through -Path / -Out.
    $dir = Join-Path ([IO.Path]::GetTempPath()) ("changelog-section-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory $dir | Out-Null
    try {
        $cl = Join-Path $dir "CHANGELOG.md"
        $notes = Join-Path $dir "notes.md"
        [IO.File]::WriteAllText($cl, $text, $utf8)
        & $PSCommandPath -Version 1.0.1 -Path $cl -Out $notes
        Check "-Out writes the body" (([IO.File]::ReadAllText($notes, $utf8)).Trim() -ceq "### Added`n`n- one")
        $bytes = [IO.File]::ReadAllBytes($notes)
        Check "-Out has no BOM" (-not ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB))
    } finally {
        Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
    }

    if ($script:failures -gt 0) { Write-Host "changelog-section self-test: $script:failures failure(s)"; exit 1 }
    Write-Host "changelog-section self-test: ok"
}

if ($SelfTest) { Invoke-SelfTest; return }

if (-not $Version) { throw "usage: changelog-section.ps1 -Version X.Y.Z [-Path CHANGELOG.md] [-Out notes.md]" }
$body = Get-ChangelogSection ([IO.File]::ReadAllText($Path, $utf8)) $Version
if ($Out) { [IO.File]::WriteAllText($Out, $body + "`n", $utf8) } else { $body }
