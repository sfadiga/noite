# Packages dist\noite into the release assets: dist\noite-<v>-win64.zip, dist\noite-<v>-win64-pdb.zip and
# dist\SHA256SUMS.txt (checksums of the two zips).
#   package.ps1 -Version 0.1.0   release: the exe's ProductVersion must equal -Version
#   package.ps1 -DryRun          same staging and checks, version read from the exe, output in dist\dry-run\
#   package.ps1 -SelfTest        runs the script's own checks on a synthetic dist in %TEMP%
#   package.ps1 -Static [-Version x.y.z | -DryRun] [-Exe <path>]
#                                the single-exe zip: only noite.exe (default build\Static\noite.exe, built
#                                with -DNOITE_STATIC=ON) and the notices; its --self-check must pass and
#                                dumpbin must list no Qt, VC runtime or ICU DLL (Windows' own ICU in System32,
#                                icuuc.dll/icuin.dll/icu.dll, is allowed; no ICU DLL is shipped). The PDB is
#                                noite.pdb next to the exe.
# Non-static packages are for local testing only; releases ship -Static (THIRD-PARTY.txt describes static linking).
# -ExeVersion overrides the version read from the exe (used by the self-test only).
# dist\noite\data is a live data folder: it is never copied, modified or zipped.
[CmdletBinding()]
param(
    [string]$Version,
    [switch]$DryRun,
    [switch]$SelfTest,
    [switch]$Static,
    [string]$Dist,
    [string]$Exe,
    [string]$ExeVersion
)
$ErrorActionPreference = "Stop"
$src = Split-Path -Parent $PSScriptRoot
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$Required = @(
    "noite.exe", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "platforms\qwindows.dll",
    "imageformats\qjpeg.dll", "imageformats\qgif.dll", "styles\qmodernwindowsstyle.dll", "vcruntime140.dll",
    "README.txt", "LICENSE.txt", "THIRD-PARTY.txt", "licenses\LGPL-3.0.txt", "licenses\GPL-3.0.txt"
)
# -Static: the whole package, and the DLL dependents a single exe must not have.
$StaticRequired = @("noite.exe", "README.txt", "LICENSE.txt", "THIRD-PARTY.txt", "licenses\LGPL-3.0.txt", "licenses\GPL-3.0.txt")
$ForbiddenDependent = '^(Qt6|vcruntime|msvcp|concrt|icu|ucrtbase)'
# Except Windows' own ICU in System32 (part of Windows 10 since 1703; Qt 6.10 needs 1809+): -no-icu still
# leaves Qt's winsdkicu feature on, which imports icuuc.dll. Nothing ships next to the exe for it. Any other
# icu* name (Qt's versioned icuuc74.dll, icudt74.dll, ...) would have to be shipped, so it stays refused.
$SystemIcuDependent = '^(icuuc|icuin|icu)\.dll$'
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Write-ZipFromFolder([string]$Folder, [string]$ZipPath, [string]$Prefix) {
    if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
    # Entry names are built from each item's own Name while walking down, never by slicing a FullName with the
    # root's length: $Folder may be an 8.3 short path (GitHub runner %TEMP%) while FullName comes back long.
    $rootDir = New-Object System.IO.DirectoryInfo((Resolve-Path -LiteralPath $Folder).ProviderPath)
    $zip = [System.IO.Compression.ZipFile]::Open($ZipPath, [System.IO.Compression.ZipArchiveMode]::Create)
    try { Add-ZipFolderEntries $zip $rootDir $Prefix } finally { $zip.Dispose() }
}

function Add-ZipFolderEntries($Zip, [System.IO.DirectoryInfo]$Dir, [string]$EntryPrefix) {
    foreach ($i in $Dir.GetFileSystemInfos()) {
        if ($i -is [System.IO.DirectoryInfo]) {
            Add-ZipFolderEntries $Zip $i "$EntryPrefix/$($i.Name)"
        } else {
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $Zip, $i.FullName, "$EntryPrefix/$($i.Name)", [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    }
}

function Get-TempBase {
    if ($env:TEMP -and (Test-Path -LiteralPath $env:TEMP)) { return $env:TEMP }
    return [System.IO.Path]::GetTempPath().TrimEnd('\')
}

function Get-ZipEntryNames([string]$ZipPath) {
    $zip = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
    try { return @($zip.Entries | ForEach-Object { $_.FullName }) } finally { $zip.Dispose() }
}

# The package version: -Version (x.y.z, must equal the exe's ProductVersion), or the exe's own for a dry run.
function Resolve-PackageVersion([string]$ExePath, [string]$Version, [bool]$DryRun, [string]$ExeVersion) {
    if (-not $DryRun) {
        if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "-Version '$Version' is not x.y.z" }
    }
    $built = $ExeVersion
    if (-not $built) { $built = (Get-Item -LiteralPath $ExePath).VersionInfo.ProductVersion }
    if (-not $built) { throw "noite.exe has no ProductVersion" }
    if ($DryRun) { return $built }
    if ($built -ne $Version) {
        throw ("exe version {0} {1} release version {2} {3} did you bump project(VERSION) in CMakeLists.txt before tagging?" -f $built, [char]0x2260, $Version, [char]0x2014)
    }
    return $Version
}

# README.txt (with the version, CRLF), LICENSE.txt, THIRD-PARTY.txt and licenses\ into the stage.
function Add-PackageNotices([string]$Stage, [string]$Version, [string]$PackagingDir, [string]$LicenseFile) {
    $readme = [System.IO.File]::ReadAllText((Join-Path $PackagingDir "README.txt"), [System.Text.Encoding]::UTF8)
    $readme = $readme.Replace("<version>", $Version) -replace "`r?`n", "`r`n"
    [System.IO.File]::WriteAllText((Join-Path $Stage "README.txt"), $readme, $Utf8NoBom)
    Copy-Item -LiteralPath $LicenseFile (Join-Path $Stage "LICENSE.txt") -Force
    Copy-Item -LiteralPath (Join-Path $PackagingDir "THIRD-PARTY.txt") $Stage -Force
    New-Item -ItemType Directory -Path (Join-Path $Stage "licenses") -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $PackagingDir "licenses") -File |
        Copy-Item -Destination (Join-Path $Stage "licenses") -Force
}

# Zips the stage and the PDB and writes SHA256SUMS.txt in $Tmp\out, then moves all three into $OutDir
# (only once all three exist). Returns @{ Zip; PdbZip; Sums }.
function Write-PackageOutputs([string]$Tmp, [string]$Stage, [string]$Version, [string]$PdbPath, [string]$OutDir) {
    if (-not (Test-Path -LiteralPath $PdbPath)) { throw "PDB not found: $PdbPath (build Release first)" }
    $work = Join-Path $Tmp "out"
    New-Item -ItemType Directory -Path $work -Force | Out-Null
    $zipName = "noite-$Version-win64.zip"
    $pdbZipName = "noite-$Version-win64-pdb.zip"
    Write-ZipFromFolder $Stage (Join-Path $work $zipName) "noite"

    $pdbDir = Join-Path $Tmp "pdb"
    New-Item -ItemType Directory -Path $pdbDir -Force | Out-Null
    Copy-Item -LiteralPath $PdbPath (Join-Path $pdbDir "noite.pdb")
    Write-ZipFromFolder $pdbDir (Join-Path $work $pdbZipName) "noite"

    $lines = foreach ($n in @($zipName, $pdbZipName)) {
        "{0}  {1}" -f (Get-FileHash -LiteralPath (Join-Path $work $n) -Algorithm SHA256).Hash.ToLowerInvariant(), $n
    }
    [System.IO.File]::WriteAllText((Join-Path $work "SHA256SUMS.txt"), (($lines -join "`n") + "`n"), $Utf8NoBom)

    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
    foreach ($n in @($zipName, $pdbZipName, "SHA256SUMS.txt")) {
        Move-Item -LiteralPath (Join-Path $work $n) -Destination (Join-Path $OutDir $n) -Force
    }
    return @{ Zip = (Join-Path $OutDir $zipName); PdbZip = (Join-Path $OutDir $pdbZipName); Sums = (Join-Path $OutDir "SHA256SUMS.txt") }
}

# Stages, checks and zips. Returns @{ Zip; PdbZip; Sums; Version }. Throws on any failed check.
function New-PackageZips {
    param(
        [string]$DistDir, [string]$Version, [bool]$DryRun, [string]$ExeVersion,
        [string]$OutDir, [string]$PdbPath, [string]$PackagingDir, [string]$LicenseFile
    )
    $exe = Join-Path $DistDir "noite.exe"
    if (-not (Test-Path -LiteralPath $exe)) { throw "noite.exe not found in $DistDir (run tools/deploy.ps1 first)" }
    $Version = Resolve-PackageVersion $exe $Version $DryRun $ExeVersion

    $tmp = Join-Path (Get-TempBase) ("noite-pkg-" + [guid]::NewGuid().ToString("N"))
    $stage = Join-Path $tmp "noite"
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    try {
        # dist contents minus the live data folder
        Get-ChildItem -LiteralPath $DistDir -Force | Where-Object { $_.Name -ne "data" } |
            Copy-Item -Destination $stage -Recurse -Force
        if (Get-ChildItem -LiteralPath $stage -Recurse -File -Force -Filter *.pdb) {
            $names = (Get-ChildItem -LiteralPath $stage -Recurse -File -Force -Filter *.pdb | ForEach-Object Name) -join ", "
            throw "forbidden file(s) in dist: $names"
        }

        Add-PackageNotices $stage $Version $PackagingDir $LicenseFile

        foreach ($r in $Required) {
            if (-not (Test-Path -LiteralPath (Join-Path $stage $r))) { throw "required file missing from the package: $r" }
        }
        if (Test-Path -LiteralPath (Join-Path $stage "data")) { throw "forbidden folder in the package: data\" }

        $out = Write-PackageOutputs $tmp $stage $Version $PdbPath $OutDir
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
    return @{ Zip = $out.Zip; PdbZip = $out.PdbZip; Sums = $out.Sums; Version = $Version }
}

# Every file and folder under $Dir, as paths relative to it ("licenses\GPL-3.0.txt"). Built from each
# item's own Name while walking down, never by slicing FullName (see Write-ZipFromFolder).
function Get-StageEntries([System.IO.DirectoryInfo]$Dir, [string]$Prefix = "") {
    foreach ($i in $Dir.GetFileSystemInfos()) {
        $rel = if ($Prefix) { "$Prefix\$($i.Name)" } else { $i.Name }
        if ($i -is [System.IO.DirectoryInfo]) {
            [pscustomobject]@{ Path = $rel; IsDir = $true }
            Get-StageEntries $i $rel
        } else {
            [pscustomobject]@{ Path = $rel; IsDir = $false }
        }
    }
}

# The DLLs `dumpbin /dependents` lists for $ExePath (delay-loaded ones included). Enters the VS dev
# shell first when dumpbin isn't on PATH, like deploy.ps1.
function Get-ExeDependents([string]$ExePath) {
    if (-not (Get-Command dumpbin -ErrorAction SilentlyContinue)) {
        $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath
        if (-not $vs) { throw "dumpbin not found and no Visual Studio Build Tools to provide it" }
        & "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    }
    $text = @(& dumpbin /nologo /dependents $ExePath)
    if ($LASTEXITCODE) { throw "dumpbin /dependents failed ($LASTEXITCODE): $($text -join ' ')" }
    $deps = @($text | Where-Object { $_ -match '^\s+(\S+\.dll)\s*$' } | ForEach-Object { $_.Trim() })
    if ($deps.Count -eq 0) { throw "dumpbin /dependents listed no DLLs for ${ExePath}: $($text -join ' ')" }
    return $deps
}

# -Static: stages only noite.exe plus the notices, runs its --self-check in the stage, refuses DLL
# dependents and anything but the six files, then zips as New-PackageZips does.
# Returns @{ Zip; PdbZip; Sums; Version; SelfCheck; Dependents }. Throws on any failed check.
# -DependentsOverride / -SelfCheckOverride replace dumpbin and the exe run (self-test only); the
# override gets the stage folder, must write noite-self-check.txt there and returns the exit code.
function New-StaticPackageZips {
    param(
        [string]$ExePath, [string]$Version, [bool]$DryRun, [string]$ExeVersion,
        [string]$OutDir, [string]$PdbPath, [string]$PackagingDir, [string]$LicenseFile,
        [string[]]$DependentsOverride, [scriptblock]$SelfCheckOverride
    )
    if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
        throw "noite.exe not found: $ExePath (build it with -DNOITE_STATIC=ON against the static Qt first)"
    }
    $Version = Resolve-PackageVersion $ExePath $Version $DryRun $ExeVersion

    $tmp = Join-Path (Get-TempBase) ("noite-pkg-" + [guid]::NewGuid().ToString("N"))
    $stage = Join-Path $tmp "noite"
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    try {
        $exe = Join-Path $stage "noite.exe"
        Copy-Item -LiteralPath $ExePath $exe

        # --self-check writes its result next to the exe, i.e. into the stage. Only a file written by
        # this run counts, and it never reaches the zip.
        $report = Join-Path $stage "noite-self-check.txt"
        if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Force }
        if ($SelfCheckOverride) {
            $code = & $SelfCheckOverride $stage | Select-Object -Last 1
        } else {
            $p = Start-Process -FilePath $exe -ArgumentList "--self-check" -WorkingDirectory $stage -Wait -PassThru
            $code = $p.ExitCode
        }
        $line = ""
        if (Test-Path -LiteralPath $report) {
            $line = [System.IO.File]::ReadAllText($report, [System.Text.Encoding]::UTF8).Trim()
            Remove-Item -LiteralPath $report -Force
        }
        if ($null -eq $code) { throw "noite.exe --self-check: no exit code" }
        if ($code -ne 0) { throw "noite.exe --self-check failed (exit $code): $line" }
        if (-not $line) { throw "noite.exe --self-check exited 0 but wrote no noite-self-check.txt" }
        if ($line -ne "self-check ok $Version") {
            throw "noite.exe --self-check wrote '$line', expected 'self-check ok $Version'"
        }

        if ($null -ne $DependentsOverride) { $deps = @($DependentsOverride) } else { $deps = Get-ExeDependents $exe }
        $bad = @($deps | Where-Object { $_ -match $ForbiddenDependent -and $_ -notmatch $SystemIcuDependent })
        if ($bad.Count) { throw "noite.exe depends on DLL(s) a single exe must not need: $($bad -join ', ')" }

        Add-PackageNotices $stage $Version $PackagingDir $LicenseFile

        # after the self-check, so anything the exe left behind is caught too
        $entries = @(Get-StageEntries (New-Object System.IO.DirectoryInfo((Resolve-Path -LiteralPath $stage).ProviderPath)))
        $dirs = @($entries | Where-Object { $_.IsDir -and $_.Path -ne "licenses" } | ForEach-Object { "$($_.Path)\" })
        if ($dirs.Count) { throw "forbidden folder(s) in the static package: $($dirs -join ', ')" }
        $files = @($entries | Where-Object { -not $_.IsDir } | ForEach-Object Path)
        $forbidden = @($files | Where-Object { $_ -like "*.dll" -or $_ -like "*.pdb" -or $_ -eq "noite-self-check.txt" })
        if ($forbidden.Count) { throw "forbidden file(s) in the static package: $($forbidden -join ', ')" }
        foreach ($r in $StaticRequired) {
            if ($files -notcontains $r) { throw "required file missing from the package: $r" }
        }
        $extra = @($files | Where-Object { $StaticRequired -notcontains $_ })
        if ($extra.Count) { throw "unexpected file(s) in the static package: $($extra -join ', ')" }

        $out = Write-PackageOutputs $tmp $stage $Version $PdbPath $OutDir
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
    return @{ Zip = $out.Zip; PdbZip = $out.PdbZip; Sums = $out.Sums; Version = $Version; SelfCheck = $line; Dependents = $deps }
}

function Invoke-SelfTest {
    function Expect-Throw([scriptblock]$Block, [string]$Pattern, [string]$Name) {
        $msg = $null
        try { & $Block | Out-Null } catch { $msg = $_.Exception.Message }
        if ($null -eq $msg) { throw "self-test '$Name': expected a failure, got success" }
        if ($msg -notmatch $Pattern) { throw "self-test '$Name': message '$msg' does not match '$Pattern'" }
        Write-Host "  ok  $Name (refused: $msg)"
    }
    $root = Join-Path (Get-TempBase) ("noite-pkg-selftest-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $root | Out-Null
    try {
        $pkgDir = Join-Path $src "packaging"
        $lic = Join-Path $src "LICENSE"
        $pdb = Join-Path $root "noite.pdb"
        Set-Content $pdb "pdb" -Encoding ASCII
        $script:n = 0
        function New-Dist([string[]]$Skip = @(), [string[]]$Extra = @()) {
            $script:n++
            $d = Join-Path $root "dist$($script:n)\noite"
            New-Item -ItemType Directory -Path $d -Force | Out-Null
            foreach ($f in @("noite.exe", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "platforms\qwindows.dll",
                             "imageformats\qjpeg.dll", "imageformats\qgif.dll", "styles\qmodernwindowsstyle.dll", "vcruntime140.dll")) {
                if ($Skip -contains $f) { continue }
                $p = Join-Path $d $f
                New-Item -ItemType Directory -Path (Split-Path $p) -Force | Out-Null
                Set-Content $p "x" -Encoding ASCII
            }
            foreach ($f in $Extra) {
                $p = Join-Path $d $f
                New-Item -ItemType Directory -Path (Split-Path $p) -Force | Out-Null
                Set-Content $p "x" -Encoding ASCII
            }
            return $d
        }
        function Pack([string]$D, [string]$Ver, [bool]$Dry, [string]$ExeVer) {
            $out = Join-Path $root "out$($script:n)"
            return New-PackageZips -DistDir $D -Version $Ver -DryRun $Dry -ExeVersion $ExeVer -OutDir $out `
                -PdbPath $pdb -PackagingDir $pkgDir -LicenseFile $lic
        }

        # 1. complete dist passes; zip has noite/README.txt with the version, CRLF, and checksums match
        $r = Pack (New-Dist) "0.1.0" $false "0.1.0"
        $names = Get-ZipEntryNames $r.Zip
        if ($names -notcontains "noite/README.txt") { throw "self-test 1: noite/README.txt missing from the zip" }
        $zip = [System.IO.Compression.ZipFile]::OpenRead($r.Zip)
        try {
            $e = $zip.GetEntry("noite/README.txt")
            $sr = New-Object System.IO.StreamReader($e.Open(), [System.Text.Encoding]::UTF8)
            $text = $sr.ReadToEnd(); $sr.Dispose()
        } finally { $zip.Dispose() }
        if ($text -notmatch "^noite 0\.1\.0 ") { throw "self-test 1: README.txt does not carry the version" }
        if ($text -match "<version>") { throw "self-test 1: <version> placeholder left in README.txt" }
        if ($text -notmatch "`r`n") { throw "self-test 1: README.txt is not CRLF" }
        $sums = Get-Content $r.Sums
        if (@($sums).Count -ne 2) { throw "self-test 1: SHA256SUMS.txt should have 2 lines" }
        foreach ($l in $sums) {
            if ($l -notmatch '^([0-9a-f]{64})  (.+)$') { throw "self-test 1: bad checksum line '$l'" }
            $f = Join-Path (Split-Path $r.Zip) $Matches[2]
            if ((Get-FileHash $f -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Matches[1]) { throw "self-test 1: checksum mismatch for $($Matches[2])" }
        }
        Write-Host "  ok  complete dist packages (README.txt carries 0.1.0, CRLF, 2 checksums)"

        # 2. data\ in the dist stays out of the zip
        $r = Pack (New-Dist -Extra @("data\noite.db", "data\logs\a.log")) "0.1.0" $false "0.1.0"
        $bad = @(Get-ZipEntryNames $r.Zip | Where-Object { $_ -like "noite/data*" })
        if ($bad.Count -gt 0) { throw "self-test 2: data\ leaked into the zip: $($bad -join ', ')" }
        Write-Host "  ok  data\ excluded from the zip"

        # 3. missing required file is refused, naming it
        Expect-Throw { Pack (New-Dist -Skip @("imageformats\qjpeg.dll")) "0.1.0" $false "0.1.0" } `
            "imageformats\\qjpeg\.dll" "missing imageformats\qjpeg.dll"

        # 4. a .pdb inside the dist is refused
        Expect-Throw { Pack (New-Dist -Extra @("noite.pdb")) "0.1.0" $false "0.1.0" } `
            "noite\.pdb" ".pdb in the dist"

        # 3b. missing styles plugin
        Expect-Throw { Pack (New-Dist -Skip @("styles\qmodernwindowsstyle.dll")) "0.1.0" $false "0.1.0" } `
            "styles\\qmodernwindowsstyle\.dll" "missing styles\qmodernwindowsstyle.dll"
        Expect-Throw { Pack (New-Dist -Skip @("imageformats\qgif.dll")) "0.1.0" $false "0.1.0" } `
            "imageformats\\qgif\.dll" "missing imageformats\qgif.dll"

        # 5. version mismatch
        Expect-Throw { Pack (New-Dist) "0.2.0" $false "0.1.0-dev+abc" } `
            "exe version 0\.1\.0-dev\+abc .* release version 0\.2\.0" "-Version 0.2.0 vs exe 0.1.0-dev+abc"

        # 6. not x.y.z
        Expect-Throw { Pack (New-Dist) "1.0" $false "1.0" } "not x\.y\.z" "-Version 1.0"

        # missing build PDB, and no stray zips left behind
        $missingPdb = $pdb
        $pdb = Join-Path $root "nope.pdb"
        $d = New-Dist
        Expect-Throw { Pack $d "0.1.0" $false "0.1.0" } "PDB not found" "missing PDB"
        if (Test-Path -LiteralPath (Join-Path $root "out$($script:n)")) { throw "self-test: failed run left an output folder behind" }
        $pdb = $missingPdb

        # -Dist with a space and brackets
        $weird = Join-Path $root "my dist [x]"
        Copy-Item -LiteralPath (New-Dist -Extra @("data\noite.db")) -Destination $weird -Recurse
        $r = Pack $weird "0.1.0" $false "0.1.0"
        if ((Get-ZipEntryNames $r.Zip) -notcontains "noite/noite.exe") { throw "self-test: bracketed -Dist not packaged" }
        Write-Host "  ok  -Dist containing a space and [x]"

        # dry run takes the version from the exe
        $r = Pack (New-Dist) "" $true "0.1.0-dev+abc"
        if ((Split-Path -Leaf $r.Zip) -ne "noite-0.1.0-dev+abc-win64.zip") { throw "self-test dry-run: unexpected zip name $($r.Zip)" }
        Write-Host "  ok  dry run uses the exe's version"

        # 8.3 short paths (the GitHub runner's %TEMP% is C:\Users\RUNNER~1\...): entry names must not depend on path form
        $longDist = New-Dist
        $fso = New-Object -ComObject Scripting.FileSystemObject
        $shortDist = $fso.GetFolder($longDist).ShortPath
        if ($shortDist -eq $longDist) {
            Write-Host "  skipped: no 8.3 names (short-path regression case)"
        } else {
            $shortOut = $fso.GetFolder($root).ShortPath + "\out-short"
            # the staging folder derives from %TEMP%, so make that short too (as on the runner)
            $longTemp = Join-Path $root "a-long-temp-folder-name"
            New-Item -ItemType Directory -Path $longTemp | Out-Null
            $savedTemp = $env:TEMP
            $env:TEMP = $fso.GetFolder($longTemp).ShortPath
            try {
                $r = New-PackageZips -DistDir $shortDist -Version "0.1.0" -DryRun $false -ExeVersion "0.1.0" -OutDir $shortOut `
                    -PdbPath $pdb -PackagingDir $pkgDir -LicenseFile $lic
            } finally { $env:TEMP = $savedTemp }
            foreach ($z in @($r.Zip, $r.PdbZip)) {
                $zn = Get-ZipEntryNames $z
                $outside = @($zn | Where-Object { $_ -notlike "noite/*" })
                if ($outside.Count -gt 0) { throw "self-test short path: entries outside noite/ in ${z}: $($outside -join ', ')" }
            }
            $zn = Get-ZipEntryNames $r.Zip
            foreach ($need in @("noite/README.txt", "noite/noite.exe", "noite/platforms/qwindows.dll")) {
                if ($zn -notcontains $need) { throw "self-test short path: $need missing from the zip (entries: $($zn -join ', '))" }
            }
            Write-Host "  ok  8.3 short -DistDir/-OutDir ($shortDist) packages with clean entry names"
        }

        # -Static: one exe; the self-check and dumpbin are stand-ins (the real ones need a real static exe)
        function New-StaticExe([string[]]$Extra = @()) {
            $script:n++
            $d = Join-Path $root "static$($script:n)"
            New-Item -ItemType Directory -Path $d -Force | Out-Null
            Set-Content (Join-Path $d "noite.exe") "x" -Encoding ASCII
            foreach ($f in $Extra) { Set-Content (Join-Path $d $f) "self-check ok 0.1.0" -Encoding ASCII }
            return (Join-Path $d "noite.exe")
        }
        # A self-check stand-in: runs with the stage folder, writes noite-self-check.txt there, returns the exit code.
        function New-SelfCheck([string]$Text, [int]$Code, [string[]]$Drops = @()) {
            return {
                param($Stage)
                # a closure sees only its captured variables, so no script-scope $Utf8NoBom here
                if ($Text) { [System.IO.File]::WriteAllText((Join-Path $Stage "noite-self-check.txt"), $Text, (New-Object System.Text.UTF8Encoding($false))) }
                foreach ($f in $Drops) {
                    $p = Join-Path $Stage $f
                    New-Item -ItemType Directory -Path (Split-Path $p) -Force | Out-Null
                    Set-Content $p "x" -Encoding ASCII
                }
                $Code
            }.GetNewClosure()
        }
        $okCheck = New-SelfCheck "self-check ok 0.1.0" 0
        $okDeps = @("KERNEL32.dll", "USER32.dll", "WTSAPI32.dll", "dbghelp.dll")
        function PackStatic([string]$ExePath, [scriptblock]$Check, [string[]]$Deps, [string]$Ver = "0.1.0") {
            $out = Join-Path $root "out$($script:n)"
            return New-StaticPackageZips -ExePath $ExePath -Version $Ver -DryRun $false -ExeVersion $Ver -OutDir $out `
                -PdbPath $pdb -PackagingDir $pkgDir -LicenseFile $lic -DependentsOverride $Deps -SelfCheckOverride $Check
        }

        # S1. a complete static stage passes: exactly the six files, PDB zip and checksums as in M7a
        $r = PackStatic (New-StaticExe) $okCheck $okDeps
        $want = @("noite/noite.exe", "noite/README.txt", "noite/LICENSE.txt", "noite/THIRD-PARTY.txt",
                  "noite/licenses/LGPL-3.0.txt", "noite/licenses/GPL-3.0.txt")
        $got = @(Get-ZipEntryNames $r.Zip)
        $diff = @(Compare-Object ($want | Sort-Object) ($got | Sort-Object))
        if ($diff.Count) { throw "self-test static: zip entries are '$($got -join ', ')', expected '$($want -join ', ')'" }
        if ((Split-Path -Leaf $r.Zip) -ne "noite-0.1.0-win64.zip") { throw "self-test static: unexpected zip name $($r.Zip)" }
        if ((Get-ZipEntryNames $r.PdbZip) -notcontains "noite/noite.pdb") { throw "self-test static: noite.pdb missing from the PDB zip" }
        if (@(Get-Content $r.Sums).Count -ne 2) { throw "self-test static: SHA256SUMS.txt should have 2 lines" }
        if ($r.SelfCheck -ne "self-check ok 0.1.0") { throw "self-test static: self-check line '$($r.SelfCheck)'" }
        Write-Host "  ok  complete static stage packages (exactly the 6 files, PDB zip, 2 checksums)"

        # S2. noite-self-check.txt (written by the self-check into the stage) is never in the zip
        if (@($got | Where-Object { $_ -like "*noite-self-check.txt" }).Count) { throw "self-test static: noite-self-check.txt is in the zip" }
        Write-Host "  ok  noite-self-check.txt not in the static zip"

        # S3. a DLL in the stage is refused
        Expect-Throw { PackStatic (New-StaticExe) (New-SelfCheck "self-check ok 0.1.0" 0 @("Qt6Core.dll")) $okDeps } `
            "Qt6Core\.dll" "Qt6Core.dll in the static stage"

        # S4. a plugin folder is refused
        Expect-Throw { PackStatic (New-StaticExe) (New-SelfCheck "self-check ok 0.1.0" 0 @("platforms\qwindows.dll")) $okDeps } `
            "platforms\\" "platforms\ in the static stage"

        # S5. data\ (the exe must not create one) is refused
        Expect-Throw { PackStatic (New-StaticExe) (New-SelfCheck "self-check ok 0.1.0" 0 @("data\noite.db")) $okDeps } `
            "data\\" "data\ in the static stage"

        # S6. any other file is refused (the zip holds only the six files)
        Expect-Throw { PackStatic (New-StaticExe) (New-SelfCheck "self-check ok 0.1.0" 0 @("noite.log")) $okDeps } `
            "noite\.log" "unexpected noite.log in the static stage"

        # S7. a DLL dependent the single exe must not need is refused, case-insensitively
        Expect-Throw { PackStatic (New-StaticExe) $okCheck @("KERNEL32.dll", "VCRUNTIME140.dll") } `
            "VCRUNTIME140\.dll" "VCRUNTIME140.dll dependent"
        # Windows' own ICU (System32) is allowed; any other ICU DLL would have to be shipped, so it is refused
        $r = PackStatic (New-StaticExe) $okCheck @("KERNEL32.dll", "icuuc.dll", "ICUIN.dll", "icu.dll")
        if (-not (Test-Path -LiteralPath $r.Zip)) { throw "self-test static: system ICU dependents not packaged" }
        Write-Host "  ok  Windows' system ICU dependents (icuuc.dll, ICUIN.dll, icu.dll) accepted"
        Expect-Throw { PackStatic (New-StaticExe) $okCheck @("KERNEL32.dll", "icuuc74.dll") } `
            "icuuc74\.dll" "icuuc74.dll dependent"
        Expect-Throw { PackStatic (New-StaticExe) $okCheck @("KERNEL32.dll", "icudt74.dll") } `
            "icudt74\.dll" "icudt74.dll dependent"
        Expect-Throw { PackStatic (New-StaticExe) $okCheck @("KERNEL32.dll", "qt6core.dll") } `
            "qt6core\.dll" "qt6core.dll dependent"

        # S8. a failing self-check is refused, with its text
        Expect-Throw { PackStatic (New-StaticExe) (New-SelfCheck "self-check FAILED: missing plugin QJpegPlugin" 1) $okDeps } `
            "self-check FAILED: missing plugin QJpegPlugin" "failing self-check (exit 1)"

        # S9. exit 0 without a fresh result file is refused, even with a stale "ok" file next to the exe
        Expect-Throw { PackStatic (New-StaticExe -Extra @("noite-self-check.txt")) (New-SelfCheck "" 0) $okDeps } `
            "noite-self-check\.txt" "self-check exit 0, no result file (stale one next to the exe)"

        # S10. an ok line for another version is refused
        Expect-Throw { PackStatic (New-StaticExe) (New-SelfCheck "self-check ok 0.0.9" 0) $okDeps } `
            "self-check ok 0\.0\.9" "self-check ok for another version"

        # S11. missing exe
        Expect-Throw { PackStatic (Join-Path $root "nope\noite.exe") $okCheck $okDeps } "noite\.exe not found" "missing static exe"
    } finally {
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
    Write-Host "package.ps1 self-test passed"
}

if ($SelfTest) { Invoke-SelfTest; return }

if (-not $DryRun -and -not $Version) { throw "-Version <x.y.z> is required (or use -DryRun)" }
$outDir = Join-Path $src "dist"
if ($DryRun) { $outDir = Join-Path $outDir "dry-run" }

if ($Static) {
    if ($Dist) { throw "-Dist is not used with -Static (use -Exe)" }
    if (-not $Exe) { $Exe = Join-Path $src "build\Static\noite.exe" }
    $r = New-StaticPackageZips -ExePath $Exe -Version $Version -DryRun $DryRun.IsPresent -ExeVersion $ExeVersion `
        -OutDir $outDir -PdbPath (Join-Path (Split-Path -Parent $Exe) "noite.pdb") `
        -PackagingDir (Join-Path $src "packaging") -LicenseFile (Join-Path $src "LICENSE")
    Write-Host "Version $($r.Version)"
    Write-Host $r.SelfCheck
    Write-Host "dumpbin /dependents noite.exe:"
    $r.Dependents | ForEach-Object { Write-Host "  $_" }
    Write-Host "Contents of $(Split-Path -Leaf $r.Zip):"
    Get-ZipEntryNames $r.Zip | Sort-Object | ForEach-Object { Write-Host "  $_" }
    foreach ($f in @($Exe, $r.Zip, $r.PdbZip, $r.Sums)) {
        Write-Host ("{0,10:N0} bytes  {1}" -f (Get-Item -LiteralPath $f).Length, $f)
    }
    return
}
if ($Exe) { throw "-Exe is used only with -Static" }

if (-not $Dist) { $Dist = Join-Path $src "dist\noite" }

$r = New-PackageZips -DistDir $Dist -Version $Version -DryRun $DryRun.IsPresent -ExeVersion $ExeVersion `
    -OutDir $outDir -PdbPath (Join-Path $src "build\Release\noite.pdb") `
    -PackagingDir (Join-Path $src "packaging") -LicenseFile (Join-Path $src "LICENSE")

Write-Host "Version $($r.Version)"
Write-Host "Top level of $(Split-Path -Leaf $r.Zip):"
Get-ZipEntryNames $r.Zip | ForEach-Object { ($_ -split '/')[1] } | Where-Object { $_ } | Sort-Object -Unique |
    ForEach-Object { Write-Host "  noite/$_" }
foreach ($f in @($r.Zip, $r.PdbZip, $r.Sums)) {
    Write-Host ("{0,10:N0} bytes  {1}" -f (Get-Item -LiteralPath $f).Length, $f)
}
