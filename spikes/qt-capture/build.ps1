# Throwaway spike build: configures and builds qt-capture with CMake + Ninja inside the
# VS Build Tools dev shell. Release builds are deployed with windeployqt so the exe runs
# outside the dev shell.
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

$src = $PSScriptRoot
$bin = Join-Path $src "build\$Config"
cmake -S $src -B $bin -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_PREFIX_PATH=$QtDir"
if ($LASTEXITCODE) { throw "cmake configure failed" }
cmake --build $bin
if ($LASTEXITCODE) { throw "cmake build failed" }

$deploy = if ($Config -eq "Release") { "--release" } else { "--debug" }
& "$QtDir\bin\windeployqt.exe" $deploy --no-translations --no-system-d3d-compiler --no-opengl-sw (Join-Path $bin "qt-capture.exe") | Out-Null
if ($LASTEXITCODE) { throw "windeployqt failed" }
"built: $(Join-Path $bin 'qt-capture.exe')"

if ($Test) {
    ctest --test-dir $bin --output-on-failure -V
    if ($LASTEXITCODE) { throw "tests failed" }
}
