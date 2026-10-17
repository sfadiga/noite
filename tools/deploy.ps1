# Builds the portable folder dist/noite: noite.exe, the Qt DLLs and plugins it needs, and the
# VC runtime DLLs next to it (no installer). Fails if the folder exceeds the size budget.
param(
    [ValidateSet("Debug", "Release")][string]$Config = "Release",
    [string]$QtDir = "$env:USERPROFILE\Qt\6.10.3\msvc2022_64",
    [int]$MaxMB = 45
)

$ErrorActionPreference = "Stop"
$src = Split-Path $PSScriptRoot
$exe = Join-Path $src "build\$Config\noite.exe"
if (-not (Test-Path $exe)) { throw "build first: $exe not found (tools/build.ps1 -Config $Config)" }

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null

$dist = Join-Path $src "dist\noite"
Remove-Item $dist -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory $dist | Out-Null
Copy-Item $exe $dist

$mode = if ($Config -eq "Release") { "--release" } else { "--debug" }
& "$QtDir\bin\windeployqt.exe" $mode --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime `
    --skip-plugin-types tls,networkinformation,generic (Join-Path $dist "noite.exe") | Out-Null
if ($LASTEXITCODE) { throw "windeployqt failed" }

# Not needed by a raster Widgets app, or not wanted in a portable folder. Qt Network only
# arrives through the TLS / network-information / TUIO-touch plugins skipped above; noite has
# no network code (D13), so it must not ship.
Get-ChildItem $dist -Recurse -File -Include "vc_redist*.exe", "dxcompiler.dll", "dxil.dll", "d3dcompiler_*.dll", "Qt6Network.dll" | Remove-Item
foreach ($dir in "tls", "networkinformation", "generic") { Remove-Item (Join-Path $dist $dir) -Recurse -Force -ErrorAction SilentlyContinue }

# App-local VC runtime instead of the installer.
$crt = Get-ChildItem "$env:VCToolsRedistDir\x64" -Directory -Filter "Microsoft.VC*.CRT" | Select-Object -First 1
if (-not $crt) { throw "VC runtime not found under $env:VCToolsRedistDir\x64" }
Copy-Item "$($crt.FullName)\msvcp140*.dll", "$($crt.FullName)\vcruntime140*.dll", "$($crt.FullName)\concrt140.dll" $dist

$files = Get-ChildItem $dist -Recurse -File
$mb = ($files | Measure-Object Length -Sum).Sum / 1MB
"dist: $dist"
"files: $($files.Count)  size: {0:N1} MB (budget {1} MB)" -f $mb, $MaxMB
$files | Sort-Object Length -Descending | Select-Object -First 5 | ForEach-Object { "  {0,6:N1} MB  {1}" -f ($_.Length / 1MB), $_.FullName.Substring($dist.Length + 1) }
if ($mb -gt $MaxMB) { throw "portable folder is over budget" }
