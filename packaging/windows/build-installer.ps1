# PKG Studio - Windows demo build and installer staging.
#
# Run from a "Developer PowerShell for VS 2022" window on Windows 10/11:
#
#   powershell -ExecutionPolicy Bypass -File packaging/windows/build-installer.ps1 `
#       -QtDir C:\Qt\6.8.3\msvc2022_64
#
# Prerequisites:
#   - Visual Studio 2022 with the Desktop C++ workload
#   - CMake 3.21+ on PATH
#   - Qt 6.5+ MSVC x64 kit, including the SQLite driver
#   - Inno Setup 6 (ISCC.exe on PATH, or in Program Files)
#
# Output:
#   dist\PKGStudio-<version>-win64\      portable staged folder
#   dist\PKGStudio-<version>-setup.exe   installer
#
# This script has NOT been executed in the Linux development environment.
# Verify the installed application on Windows before calling the installer
# tested; see packaging/windows/README.md.

[CmdletBinding()]
param(
    [string]$QtDir = "C:\Qt\6.8.3\msvc2022_64",
    [string]$Version = "",
    [string]$Configuration = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $Root

if (-not $Version) {
    $Match = Select-String -Path "CMakeLists.txt" -Pattern "VERSION ([0-9]+\.[0-9]+\.[0-9]+)"
    if (-not $Match) {
        throw "Could not read the project version from CMakeLists.txt"
    }
    $Version = $Match.Matches[0].Groups[1].Value
}

$WindeployQt = Join-Path $QtDir "bin/windeployqt.exe"
if (-not (Test-Path $WindeployQt)) {
    throw "windeployqt.exe not found in '$QtDir'. Pass -QtDir <Qt MSVC kit>."
}

$BuildDir = "build-windows"

if (-not $SkipBuild) {
    Write-Host "== configuring =="
    cmake -S . -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
        -DCMAKE_PREFIX_PATH="$QtDir" -DPKGSTUDIO_BUILD_TESTS=OFF
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

    Write-Host "== building $Configuration =="
    cmake --build $BuildDir --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}

$Exe = Join-Path $BuildDir "$Configuration/PKGStudio.exe"
if (-not (Test-Path $Exe)) {
    throw "Expected build output at '$Exe'"
}

$Stage = "dist/PKGStudio-$Version-win64"
if (Test-Path $Stage) {
    Remove-Item $Stage -Recurse -Force
}
New-Item -ItemType Directory -Path $Stage | Out-Null

Write-Host "== staging application =="
Copy-Item $Exe $Stage
Copy-Item "samples" $Stage -Recurse
Copy-Item "templates" $Stage -Recurse
Copy-Item "README.md", "CHANGELOG.md" $Stage

Write-Host "== deploying Qt runtime =="
& $WindeployQt --release --no-translations --no-system-d3d-compiler `
    --no-opengl-sw --sql --widgets --gui --core (Join-Path $Stage "PKGStudio.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

# Confirm the runtime is really next to the executable before packaging.
foreach ($required in @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Sql.dll")) {
    if (-not (Test-Path (Join-Path $Stage $required))) {
        throw "Missing $required after windeployqt; the installer would not be standalone."
    }
}
$SqliteDriver = Join-Path $Stage "sqldrivers/qsqlite.dll"
if (-not (Test-Path $SqliteDriver)) {
    throw "Missing sqldrivers/qsqlite.dll after windeployqt; saving and settings would fail."
}
$PlatformPlugin = Join-Path $Stage "platforms/qwindows.dll"
if (-not (Test-Path $PlatformPlugin)) {
    throw "Missing platforms/qwindows.dll after windeployqt; the application would not start."
}

$IsccPath = $null
$IsccCommand = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
if ($IsccCommand) {
    $IsccPath = $IsccCommand.Source
}
if (-not $IsccPath) {
    $Candidate = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    if (Test-Path $Candidate) { $IsccPath = $Candidate }
}
if (-not $IsccPath) {
    throw "Inno Setup 6 (ISCC.exe) not found. Install it or add it to PATH."
}

$StagePath = (Resolve-Path $Stage).Path
Write-Host "== building installer =="
& $IsccPath "packaging/windows/PKGStudio.iss" `
    "/DAppVersion=$Version" "/DStageDir=$StagePath"
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }

$Setup = "dist/PKGStudio-$Version-setup.exe"
if (-not (Test-Path $Setup)) {
    throw "Expected installer at '$Setup'"
}
$Hash = Get-FileHash $Setup -Algorithm SHA256
Write-Host ""
Write-Host "Installer: $Setup"
Write-Host "SHA256:    $($Hash.Hash)"
Write-Host "Staged:    $StagePath"
Write-Host ""
Write-Host "Next: install it on a clean Windows machine and complete the checklist"
Write-Host "in packaging/windows/README.md before calling it tested."
