# Packages dist\noite into the release assets: dist\noite-<v>-win64.zip, dist\noite-<v>-win64-pdb.zip and
# dist\SHA256SUMS.txt (checksums of the two zips).
#   package.ps1 -Version 0.1.0   release: the exe's ProductVersion must equal -Version
#   package.ps1 -DryRun          same staging and checks, version read from the exe, output in dist\dry-run\
#   package.ps1 -SelfTest        runs the script's own checks on a synthetic dist in %TEMP%
# -ExeVersion overrides the version read from the exe (used by the self-test only).
# dist\noite\data is a live data folder: it is never copied, modified or zipped.
[CmdletBinding()]
param(
    [string]$Version,
    [switch]$DryRun,
    [switch]$SelfTest,
    [string]$Dist,
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

# Stages, checks and zips. Returns @{ Zip; PdbZip; Sums; Version }. Throws on any failed check.
function New-PackageZips {
    param(
        [string]$DistDir, [string]$Version, [bool]$DryRun, [string]$ExeVersion,
        [string]$OutDir, [string]$PdbPath, [string]$PackagingDir, [string]$LicenseFile
    )
    $exe = Join-Path $DistDir "noite.exe"
    if (-not (Test-Path -LiteralPath $exe)) { throw "noite.exe not found in $DistDir (run tools/deploy.ps1 first)" }
    if (-not $DryRun) {
        if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "-Version '$Version' is not x.y.z" }
    }
    $built = $ExeVersion
    if (-not $built) { $built = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion }
    if (-not $built) { throw "noite.exe has no ProductVersion" }
    if ($DryRun) {
        $Version = $built
    } elseif ($built -ne $Version) {
        throw ("exe version {0} {1} release version {2} {3} did you bump project(VERSION) in CMakeLists.txt before tagging?" -f $built, [char]0x2260, $Version, [char]0x2014)
    }

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

        # notices
        $readme = [System.IO.File]::ReadAllText((Join-Path $PackagingDir "README.txt"), [System.Text.Encoding]::UTF8)
        $readme = $readme.Replace("<version>", $Version) -replace "`r?`n", "`r`n"
        [System.IO.File]::WriteAllText((Join-Path $stage "README.txt"), $readme, $Utf8NoBom)
        Copy-Item -LiteralPath $LicenseFile (Join-Path $stage "LICENSE.txt") -Force
        Copy-Item -LiteralPath (Join-Path $PackagingDir "THIRD-PARTY.txt") $stage -Force
        New-Item -ItemType Directory -Path (Join-Path $stage "licenses") -Force | Out-Null
        Get-ChildItem -LiteralPath (Join-Path $PackagingDir "licenses") -File |
            Copy-Item -Destination (Join-Path $stage "licenses") -Force

        foreach ($r in $Required) {
            if (-not (Test-Path -LiteralPath (Join-Path $stage $r))) { throw "required file missing from the package: $r" }
        }
        if (Test-Path -LiteralPath (Join-Path $stage "data")) { throw "forbidden folder in the package: data\" }
        if (-not (Test-Path -LiteralPath $PdbPath)) { throw "PDB not found: $PdbPath (build Release first)" }

        # build the outputs in a temp folder; move them into place only when all three exist
        $work = Join-Path $tmp "out"
        New-Item -ItemType Directory -Path $work -Force | Out-Null
        $zipName = "noite-$Version-win64.zip"
        $pdbZipName = "noite-$Version-win64-pdb.zip"
        Write-ZipFromFolder $stage (Join-Path $work $zipName) "noite"

        $pdbDir = Join-Path $tmp "pdb"
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
        $zipPath = Join-Path $OutDir $zipName
        $pdbZipPath = Join-Path $OutDir $pdbZipName
        $sumsPath = Join-Path $OutDir "SHA256SUMS.txt"
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
    return @{ Zip = $zipPath; PdbZip = $pdbZipPath; Sums = $sumsPath; Version = $Version }
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
    } finally {
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
    Write-Host "package.ps1 self-test passed"
}

if ($SelfTest) { Invoke-SelfTest; return }

if (-not $Dist) { $Dist = Join-Path $src "dist\noite" }
if (-not $DryRun -and -not $Version) { throw "-Version <x.y.z> is required (or use -DryRun)" }
$outDir = Join-Path $src "dist"
if ($DryRun) { $outDir = Join-Path $outDir "dry-run" }

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
