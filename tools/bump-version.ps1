# Prepares a release: bumps version.txt and turns CHANGELOG.md's [Unreleased] into the release.
#
#   bump-version.ps1 -Version 1.0.1     writes version.txt and CHANGELOG.md, prints the git commands
#   bump-version.ps1 -SelfTest          runs the script's own checks on temp copies
#
# Refuses a version that isn't X.Y.Z, isn't greater than the current one, or an empty
# [Unreleased]. It never runs git; commit, merge and tag yourself with the printed commands.
param(
    [string]$Version,
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"
$utf8 = New-Object System.Text.UTF8Encoding($false)
$root = Split-Path $PSScriptRoot

function Get-Eol([string]$Text) {
    if ($Text.Contains("`r`n")) { return "`r`n" } else { return "`n" }
}

# Returns the new CHANGELOG text for releasing $New (from $Current) on $Today.
function Get-BumpedChangelog([string]$Text, [string]$Current, [string]$New, [string]$Today) {
    $eol = Get-Eol $Text
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.AddRange([string[]]($Text -split "`r?`n"))

    $u = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^## \[Unreleased\]\s*$') { $u = $i; break }
    }
    if ($u -lt 0) { throw "CHANGELOG.md has no [Unreleased] section" }

    $end = $lines.Count
    for ($i = $u + 1; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^## \[' -or $lines[$i] -match '^\[[^\]]+\]:\s') { $end = $i; break }
    }
    $body = New-Object System.Collections.Generic.List[string]
    for ($i = $u + 1; $i -lt $end; $i++) { $body.Add($lines[$i]) }
    while ($body.Count -gt 0 -and $body[0].Trim().Length -eq 0) { $body.RemoveAt(0) }
    while ($body.Count -gt 0 -and $body[$body.Count - 1].Trim().Length -eq 0) { $body.RemoveAt($body.Count - 1) }
    if ($body.Count -eq 0) { throw "CHANGELOG.md [Unreleased] is empty; nothing to release" }

    $out = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $u; $i++) { $out.Add($lines[$i]) }
    $out.Add("## [Unreleased]")
    $out.Add("")
    $out.Add("## [$New] - $Today")
    $out.Add("")
    $out.AddRange($body)
    $out.Add("")

    $linkDone = $false
    for ($i = $end; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line -match '^\[Unreleased\]:\s*(.*)/compare/v[0-9]+\.[0-9]+\.[0-9]+\.\.\.HEAD\s*$') {
            $base = $Matches[1]
            $out.Add("[Unreleased]: $base/compare/v$New...HEAD")
            $out.Add("[$New]: $base/compare/v$Current...v$New")
            $linkDone = $true
        } else {
            $out.Add($line)
        }
    }
    if (-not $linkDone) { throw "CHANGELOG.md has no [Unreleased]: .../compare/vX.Y.Z...HEAD link" }
    return ($out -join $eol)
}

# Validates and applies the bump to <Dir>\version.txt and <Dir>\CHANGELOG.md.
function Invoke-Bump([string]$Dir, [string]$New, [string]$Today) {
    if ($New -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { throw "version must be X.Y.Z (got '$New')" }
    $versionFile = Join-Path $Dir "version.txt"
    $changelog = Join-Path $Dir "CHANGELOG.md"
    $raw = [IO.File]::ReadAllText($versionFile, $utf8)
    $current = $raw.Trim()
    if ($current -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { throw "version.txt must contain X.Y.Z" }
    if ([version]$New -le [version]$current) { throw "version $New is not greater than the current $current" }

    $newChangelog = Get-BumpedChangelog ([IO.File]::ReadAllText($changelog, $utf8)) $current $New $Today
    $eol = Get-Eol $raw
    [IO.File]::WriteAllText($versionFile, $New + $eol, $utf8)
    [IO.File]::WriteAllText($changelog, $newChangelog, $utf8)
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
    $changelog = @"
# Changelog

## [Unreleased]

### Fixed

- a fix

## [1.0.0] - 2026-10-19

### Added

- first

[Unreleased]: https://example.invalid/compare/v1.0.0...HEAD
[1.0.0]: https://example.invalid/releases/tag/v1.0.0
"@
    $empty = $changelog -replace "(?s)### Fixed.*?(?=## \[1\.0\.0\])", ""

    $base = Join-Path ([IO.Path]::GetTempPath()) ("bump-version-" + [guid]::NewGuid().ToString("N"))
    try {
        foreach ($eol in @("`n", "`r`n")) {
            $tag = if ($eol -eq "`n") { "LF" } else { "CRLF" }
            $dir = Join-Path $base $tag
            New-Item -ItemType Directory $dir -Force | Out-Null
            [IO.File]::WriteAllText((Join-Path $dir "version.txt"), "1.0.0$eol", $utf8)
            [IO.File]::WriteAllText((Join-Path $dir "CHANGELOG.md"), ($changelog -replace "`r?`n", $eol), $utf8)

            Invoke-Bump $dir "1.0.1" "2026-11-02"
            $v = [IO.File]::ReadAllText((Join-Path $dir "version.txt"), $utf8)
            $c = [IO.File]::ReadAllText((Join-Path $dir "CHANGELOG.md"), $utf8)
            Check "version.txt written ($tag)" ($v -ceq "1.0.1$eol")
            $expected = @"
# Changelog

## [Unreleased]

## [1.0.1] - 2026-11-02

### Fixed

- a fix

## [1.0.0] - 2026-10-19

### Added

- first

[Unreleased]: https://example.invalid/compare/v1.0.1...HEAD
[1.0.1]: https://example.invalid/compare/v1.0.0...v1.0.1
[1.0.0]: https://example.invalid/releases/tag/v1.0.0
"@
            Check "changelog rewritten ($tag)" ($c -ceq ($expected -replace "`r?`n", $eol))
            Check "line endings preserved ($tag)" ($(if ($eol -eq "`n") { -not $c.Contains("`r") } else { ($c -replace "`r`n", "").IndexOfAny([char[]]"`r`n") -lt 0 }))
            Check "no BOM ($tag)" (-not ([IO.File]::ReadAllBytes((Join-Path $dir "CHANGELOG.md"))[0] -eq 0xEF))

            # Refusals leave the files untouched.
            $before = $c
            Check "same version refused ($tag)" ($null -ne (Throws { Invoke-Bump $dir "1.0.1" "2026-11-03" }))
            Check "lower version refused ($tag)" ($null -ne (Throws { Invoke-Bump $dir "1.0.0" "2026-11-03" }))
            Check "1.1 refused ($tag)" ($null -ne (Throws { Invoke-Bump $dir "1.1" "2026-11-03" }))
            Check "v-prefix refused ($tag)" ($null -ne (Throws { Invoke-Bump $dir "v1.2.0" "2026-11-03" }))
            # Now [Unreleased] is empty (it was just released), so a greater version is refused too.
            $msg = Throws { Invoke-Bump $dir "1.0.2" "2026-11-03" }
            Check "empty Unreleased refused ($tag)" ($msg -like "*empty*")
            Check "refusals changed nothing ($tag)" ([IO.File]::ReadAllText((Join-Path $dir "CHANGELOG.md"), $utf8) -ceq $before -and
                [IO.File]::ReadAllText((Join-Path $dir "version.txt"), $utf8) -ceq "1.0.1$eol")
        }

        $dir = Join-Path $base "empty"
        New-Item -ItemType Directory $dir -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $dir "version.txt"), "1.0.0`n", $utf8)
        [IO.File]::WriteAllText((Join-Path $dir "CHANGELOG.md"), $empty, $utf8)
        $msg = Throws { Invoke-Bump $dir "1.0.1" "2026-11-02" }
        Check "empty Unreleased refused" ($msg -like "*empty*")
        Check "version.txt unchanged on empty" ([IO.File]::ReadAllText((Join-Path $dir "version.txt"), $utf8) -ceq "1.0.0`n")
    } finally {
        Remove-Item $base -Recurse -Force -ErrorAction SilentlyContinue
    }

    if ($script:failures -gt 0) { Write-Host "bump-version self-test: $script:failures failure(s)"; exit 1 }
    Write-Host "bump-version self-test: ok"
}

if ($SelfTest) { Invoke-SelfTest; return }

if (-not $Version) { throw "usage: bump-version.ps1 -Version X.Y.Z" }
Invoke-Bump $root $Version (Get-Date -Format "yyyy-MM-dd")
Write-Host "version.txt and CHANGELOG.md now say $Version. Next (not run for you):"
Write-Host ""
Write-Host "  git add version.txt CHANGELOG.md; git commit -m `"release $Version`"; (merge to main)"
Write-Host "  git tag v$Version; git push origin v$Version"
