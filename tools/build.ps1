# Configures and builds noite with CMake + Ninja inside the VS Build Tools dev shell.
# -Test runs the test suite afterwards (tools/run-tests.ps1).
param(
    [ValidateSet("Debug", "Release")][string]$Config = "Debug",
    [string]$QtDir = "$env:USERPROFILE\Qt\6.10.3\msvc2022_64",
    [switch]$Test
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path "$QtDir\bin\qtpaths.exe")) { throw "Qt not found at $QtDir" }

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath
if (-not $vs) { throw "Visual Studio Build Tools not found" }
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null

$src = Split-Path $PSScriptRoot
$bin = Join-Path $src "build\$Config"
cmake -S $src -B $bin -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_PREFIX_PATH=$QtDir"
if ($LASTEXITCODE) { throw "cmake configure failed" }
cmake --build $bin
if ($LASTEXITCODE) { throw "cmake build failed" }

if ($Test) {
    & (Join-Path $PSScriptRoot "run-tests.ps1") -Config $Config -QtDir $QtDir
    if ($LASTEXITCODE) { throw "tests failed" }
}
