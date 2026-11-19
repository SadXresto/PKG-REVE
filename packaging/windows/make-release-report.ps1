# PKG Studio - compose the Windows release report.
#
#   powershell -ExecutionPolicy Bypass -File packaging/windows/make-release-report.ps1 `
#       -SmokeReport dist/smoke/report.md -Installer dist/PKG-Studio-Setup-x64.exe
#
# Reads the smoke-test table and writes dist/release-report.md with three clearly
# separated sections: checks that PASSED on the Windows runner, checks that
# FAILED, and items this run did NOT test. The report never upgrades an untested
# item to "working". Exits 0 even on failures so CI can still upload the report;
# the smoke-test step is what fails the job.

[CmdletBinding()]
param(
    [string]$SmokeReport = "dist/smoke/report.md",
    [string]$Installer = "dist/PKG-Studio-Setup-x64.exe",
    [string]$PortableZip = "",
    [string]$Checksums = "dist/SHA256SUMS.txt",
    [string]$Output = "dist/release-report.md",
    [string]$Version = "unknown",
    [string]$Commit = "",
    [string]$QtVersion = "6.8.3",
    [string]$Runner = "windows-2022"
)

$ErrorActionPreference = "Stop"

function Format-Bytes {
    param([long]$Bytes)
    if ($Bytes -ge 1MB) { return "{0:N1} MiB" -f ($Bytes / 1MB) }
    if ($Bytes -ge 1KB) { return "{0:N0} KiB" -f ($Bytes / 1KB) }
    return "$Bytes B"
}

$Checks = @()
if (Test-Path $SmokeReport) {
    foreach ($line in Get-Content $SmokeReport) {
        if ($line -match '^\|\s*(.+?)\s*\|\s*(PASS|FAIL)\s*\|\s*(.*?)\s*\|\s*$') {
            $Checks += [pscustomobject]@{
                Name   = $Matches[1]
                Result = $Matches[2]
                Detail = $Matches[3]
            }
        }
    }
}

$Passed = @($Checks | Where-Object { $_.Result -eq "PASS" })
$Failed = @($Checks | Where-Object { $_.Result -eq "FAIL" })
$Completed = $Passed.Count + $Failed.Count

$Hashes = @{}
if (Test-Path $Checksums) {
    foreach ($line in Get-Content $Checksums) {
        if ($line -match '^([0-9a-fA-F]{64})\s+\*?(.+)$') {
            $Hashes[$Matches[2].Trim()] = $Matches[1].ToLowerInvariant()
        }
    }
}

$Artifacts = New-Object System.Collections.Generic.List[object]
foreach ($file in @($Installer, $PortableZip)) {
    if (-not $file) { continue }
    if (Test-Path $file) {
        $item = Get-Item $file
        $name = $item.Name
        $hash = "not recorded"
        if ($Hashes.ContainsKey($name)) { $hash = $Hashes[$name] }
        $Artifacts.Add([pscustomobject]@{
                Name = $name
                Size = Format-Bytes $item.Length
                Hash = $hash
            })
    }
}

$Untested = @(
    "Code signing and SmartScreen: the installer is unsigned, so Windows shows an unknown-publisher prompt.",
    "Interactive walkthrough on a real desktop: dialogs are captured headlessly; clicking through the editor is not exercised.",
    "Windows 10 specifically: the runner is a Windows Server image, so Windows 10 is not covered by this run.",
    "Install over an existing older version (upgrade path) and rollback.",
    "Uninstall leaving ``%APPDATA%\PKGStudio`` user data untouched (documented behaviour, not asserted here).",
    "Multi-monitor, high-DPI scaling and theme/rendering quality.",
    "Long-running sessions, project databases and analysis of very large packages.",
    "Installer behaviour under a non-administrator account and with antivirus present."
)

$Lines = New-Object System.Collections.Generic.List[string]
$Lines.Add("# PKG Studio $Version - Windows x64 build report")
$Lines.Add("")
$Lines.Add("- Commit: ``$Commit``")
$Lines.Add("- Qt: $QtVersion (MSVC 2022 x64)")
$Lines.Add("- Runner: $Runner")
$Lines.Add("- Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') UTC$(if ($env:GITHUB_RUN_ID) { " - run $($env:GITHUB_RUN_ID)" })")
$Lines.Add("")
$Lines.Add("## Automated checks that PASSED on the runner")
$Lines.Add("")
if ($Passed.Count -gt 0) {
    $Lines.Add("| Check | Detail |")
    $Lines.Add("| --- | --- |")
    foreach ($check in $Passed) { $Lines.Add("| $($check.Name) | $($check.Detail) |") }
} else {
    $Lines.Add("None recorded.")
}
$Lines.Add("")
$Lines.Add("## Automated checks that FAILED on the runner")
$Lines.Add("")
if ($Failed.Count -gt 0) {
    $Lines.Add("| Check | Detail |")
    $Lines.Add("| --- | --- |")
    foreach ($check in $Failed) { $Lines.Add("| $($check.Name) | $($check.Detail) |") }
} else {
    $Lines.Add("None.")
}
$Lines.Add("")
$Lines.Add("## NOT tested by this run")
$Lines.Add("")
$Lines.Add("These are unverified, not working-by-assumption. See the manual")
$Lines.Add("checklist in ``packaging/windows/README.md``.")
$Lines.Add("")
foreach ($item in $Untested) { $Lines.Add("- $item") }
$Lines.Add("")
$Lines.Add("## Artifacts")
$Lines.Add("")
if ($Artifacts.Count -gt 0) {
    $Lines.Add("| File | Size | SHA-256 |")
    $Lines.Add("| --- | --- | --- |")
    foreach ($artifact in $Artifacts) {
        $Lines.Add("| $($artifact.Name) | $($artifact.Size) | ``$($artifact.Hash)`` |")
    }
} else {
    $Lines.Add("No artifacts were produced.")
}
$Lines.Add("")
$Lines.Add("## Verdict")
$Lines.Add("")
if ($Completed -eq 0) {
    $Lines.Add("**BLOCKED**: no installed-application checks were recorded, so the build")
    $Lines.Add("or smoke test did not complete. Do not treat this installer as tested.")
} elseif ($Failed.Count -gt 0) {
    $Lines.Add("**FAILED**: $($Failed.Count) of $Completed runner checks failed. Do not")
    $Lines.Add("distribute this installer.")
} else {
    $Lines.Add("**PASSED ON THIS RUNNER**: all $Completed installed-application checks passed")
    $Lines.Add("on $Runner. The installer ran, started, analysed a bundled sample, rendered")
    $Lines.Add("its dialogs and uninstalled cleanly. The untested items above still apply.")
}
$Lines.Add("")

$outDir = Split-Path -Parent $Output
if ($outDir -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}
$Lines | Set-Content -Path $Output -Encoding utf8
Write-Host "Release report: $Output ($($Passed.Count) passed, $($Failed.Count) failed)"
