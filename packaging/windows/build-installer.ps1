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
#   dist\PKGStudio-<version>-win64\             portable staged folder
#   dist\PKGStudio-<version>-win64.zip          portable ZIP of that folder
#   dist\<SetupBaseName>.exe                    installer (default name:
#                                               PKGStudio-<version>-setup.exe)
#   dist\SHA256SUMS.txt                         SHA-256 of the ZIP and installer
#
# This script has NOT been executed in the Linux development environment.
# .github/workflows/windows-release.yml runs it on a Windows runner; the
# installer is only "tested" once that run (or a manual run on a Windows VM)
# has recorded a passing report.

[CmdletBinding()]
param(
    [string]$QtDir = "C:\Qt\6.8.3\msvc2022_64",
    [string]$Version = "",
    [string]$Configuration = "Release",
    [string]$SetupBaseName = "",
    [string]$VcRedist = "",
    [string]$VcRedistUrl = "https://aka.ms/vs/17/release/vc_redist.x64.exe",
    [switch]$SkipBuild,
    [switch]$SkipPortableZip
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $Root

if (-not $Version) {
    $Match = Select-String -Path "CMakeLists.txt" -Pattern "^\s*VERSION ([0-9]+\.[0-9]+\.[0-9]+)"
    if (-not $Match) {
        throw "Could not read the project version from CMakeLists.txt"
    }
    $Version = $Match.Matches[0].Groups[1].Value
}
if (-not $SetupBaseName) {
    $SetupBaseName = "PKGStudio-$Version-setup"
}

$WindeployQt = Join-Path $QtDir "bin/windeployqt.exe"
if (-not (Test-Path $WindeployQt)) {
    throw "windeployqt.exe not found in '$QtDir'. Pass -QtDir <Qt MSVC kit>."
}

# samples/ holds only README.md in a fresh checkout; the demo packages are
# generated, so build them where possible instead of shipping an empty folder.
if (-not (Test-Path "samples/sample.elf")) {
    $Python = Get-Command "python" -ErrorAction SilentlyContinue
    if (-not $Python) {
        throw "samples/sample.elf is missing. Run: python tools/make_test_packages.py samples"
    }
    Write-Host "== generating synthetic demo samples =="
    & $Python.Source "tools/make_test_packages.py" "samples"
    if (($LASTEXITCODE -ne 0) -or (-not (Test-Path "samples/sample.elf"))) {
        throw "Could not generate samples; the installed app would have no demo files."
    }
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

# The Qt DLLs are built against the shared MSVC runtime, which Windows does not
# ship. Qt's documented approach is to install Microsoft's redistributable, not
# to copy loose runtime DLLs (those are not licensed for redistribution), so the
# installer bundles vc_redist.x64.exe and runs it only when it is needed.
Write-Host "== preparing the Visual C++ redistributable =="
New-Item -ItemType Directory -Path "dist" -Force | Out-Null
$VcRedistPath = $VcRedist
if (-not $VcRedistPath) {
    $VcRedistPath = "dist/vc_redist.x64.exe"
    if (-not (Test-Path $VcRedistPath)) {
        Write-Host "   downloading $VcRedistUrl"
        Invoke-WebRequest -Uri $VcRedistUrl -OutFile $VcRedistPath -UseBasicParsing
    }
}
if (-not (Test-Path $VcRedistPath)) {
    throw "Visual C++ redistributable not found at '$VcRedistPath'."
}
$Signature = Get-AuthenticodeSignature $VcRedistPath
if ($Signature.Status -ne "Valid" -or $Signature.SignerCertificate.Subject -notmatch "Microsoft") {
    throw "'$VcRedistPath' is not validly signed by Microsoft; refusing to bundle it."
}
Write-Host "   $VcRedistPath (signed by $($Signature.SignerCertificate.Subject))"

Write-Host "== staging application =="
Copy-Item $Exe $Stage
Copy-Item "samples" $Stage -Recurse
Copy-Item "templates" $Stage -Recurse
Copy-Item "README.md", "CHANGELOG.md" $Stage
Copy-Item $VcRedistPath (Join-Path $Stage "vc_redist.x64.exe")

Write-Host "== deploying Qt runtime =="
# --no-compiler-runtime keeps loose (non-redistributable) MSVC DLLs out of the
# folder; the bundle ships Microsoft's redistributable instead.
$DeployArgs = @(
    "--release", "--no-translations", "--no-system-d3d-compiler",
    "--no-opengl-sw", "--no-compiler-runtime",
    "--widgets", "--gui", "--core",
    (Join-Path $Stage "PKGStudio.exe")
)
& $WindeployQt @DeployArgs
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

# Confirm the runtime is really next to the executable before packaging.
foreach ($required in @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Concurrent.dll", "Qt6Sql.dll")) {
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

if (-not (Test-Path (Join-Path $Stage "vc_redist.x64.exe"))) {
    throw "The staged folder has no vc_redist.x64.exe; the installer would depend on the target already having the Visual C++ runtime."
}

$PortableReadme = @"
PKG Studio $Version - portable Windows x64 demo build

Run PKGStudio.exe. Samples are in the samples folder.

If Windows reports a missing DLL (VCRUNTIME140.dll / MSVCP140.dll), install the
Microsoft Visual C++ runtime by running vc_redist.x64.exe from this folder, then
start PKGStudio.exe again. This build is unsigned, so SmartScreen may warn.
Help > Feature status lists implemented, partial and planned features.
"@
Set-Content -Path (Join-Path $Stage "WINDOWS-README.txt") -Value $PortableReadme -Encoding utf8

$PortableZip = "dist/PKGStudio-$Version-win64.zip"
if (-not $SkipPortableZip) {
    Write-Host "== creating portable ZIP =="
    if (Test-Path $PortableZip) { Remove-Item $PortableZip -Force }
    Compress-Archive -Path (Join-Path $Stage "*") -DestinationPath $PortableZip -CompressionLevel Optimal
    if (-not (Test-Path $PortableZip)) { throw "Could not create $PortableZip" }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $Archive = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path $PortableZip).Path)
    try {
        $Entries = @($Archive.Entries | ForEach-Object { $_.FullName -replace '\\', '/' })
    } finally {
        $Archive.Dispose()
    }
    foreach ($required in @("PKGStudio.exe", "platforms/qwindows.dll",
            "sqldrivers/qsqlite.dll", "samples/sample.elf")) {
        if ($Entries -notcontains $required) {
            throw "The portable ZIP is missing $required"
        }
    }
}

$IsccPath = $null
$IsccCommand = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
if ($IsccCommand) {
    $IsccPath = $IsccCommand.Source
}
if (-not $IsccPath) {
    foreach ($Candidate in @(
            "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
            "C:\Program Files\Inno Setup 6\ISCC.exe")) {
        if (Test-Path $Candidate) { $IsccPath = $Candidate; break }
    }
}
if (-not $IsccPath) {
    throw "Inno Setup 6 (ISCC.exe) not found. Install it or add it to PATH."
}

$StagePath = (Resolve-Path $Stage).Path
Write-Host "== building installer ($SetupBaseName.exe) =="
& $IsccPath "packaging/windows/PKGStudio.iss" `
    "/DAppVersion=$Version" "/DStageDir=$StagePath" "/DSetupBaseName=$SetupBaseName"
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }

$Setup = "dist/$SetupBaseName.exe"
if (-not (Test-Path $Setup)) {
    throw "Expected installer at '$Setup'"
}

Write-Host "== writing checksums =="
$Files = @($Setup)
if (-not $SkipPortableZip) { $Files += $PortableZip }
$Lines = foreach ($File in $Files) {
    $Hash = (Get-FileHash $File -Algorithm SHA256).Hash.ToLowerInvariant()
    "$Hash  $(Split-Path -Leaf $File)"
}
$Lines | Set-Content -Path "dist/SHA256SUMS.txt" -Encoding ascii

Write-Host ""
Write-Host "Installer: $Setup"
Write-Host "Portable:  $PortableZip"
Write-Host "Staged:    $StagePath"
Write-Host "Checksums: dist/SHA256SUMS.txt"
Get-Content "dist/SHA256SUMS.txt" | ForEach-Object { Write-Host "  $_" }
Write-Host ""
Write-Host "Next: run packaging/windows/smoke-test.ps1 against this installer. Do not"
Write-Host "call it tested until that run (or the GitHub workflow) passes."
