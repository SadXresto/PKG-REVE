<#
.SYNOPSIS
    Creates the PKG-REVE repository on GitHub, pushes main and publishes the
    v0.7.4 release with the installer and portable build from .\releases.

.DESCRIPTION
    Run once from the repository root on Windows:

        powershell -ExecutionPolicy Bypass -File .\tools\publish-release.ps1

    Needs Git for Windows and the GitHub CLI (`winget install --id GitHub.cli`).
    The script signs in with `gh auth login` when needed, verifies every file in
    releases\SHA256SUMS.txt, creates the repository if it does not exist, pushes
    main with its full history, then creates the release and uploads the files
    (re-running it refreshes the assets instead of failing).

.PARAMETER RepoName
    Repository name to create or reuse. Default: PKG-REVE.

.PARAMETER Owner
    GitHub account or organisation that should own the repository. Default: the
    signed-in user.

.PARAMETER Visibility
    private (default) or public.

.PARAMETER Tag
    Release tag to publish. Default: v0.7.4.

.PARAMETER ReleaseDir
    Folder holding the assets and SHA256SUMS.txt. Default: releases.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\tools\publish-release.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\tools\publish-release.ps1 -Visibility public
#>
[CmdletBinding()]
param(
    [string]$RepoName = 'PKG-REVE',
    [string]$Owner = '',
    [ValidateSet('private', 'public')]
    [string]$Visibility = 'private',
    [string]$Tag = 'v0.7.4',
    [string]$ReleaseDir = 'releases',
    [string]$Notes = 'PKG Studio 0.7.4 - glass UI, faster local model scan. Windows installer and portable build.'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-ExitCode {
    param([string]$What)
    if ($LASTEXITCODE -ne 0) {
        throw "$What failed (exit code $LASTEXITCODE)."
    }
}

# Runs a probe whose failure is expected; returns its exit code instead of
# letting the native command's stderr trip $ErrorActionPreference.
function Test-CommandQuiet {
    param([scriptblock]$Command)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Command 2>&1 | Out-Null
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot

if (-not (Test-Path -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt'))) {
    throw "CMakeLists.txt was not found in $repoRoot. Run the script from the PKG Studio checkout."
}
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'Git is not installed. Install Git for Windows (https://git-scm.com/download/win) first.'
}
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw 'GitHub CLI is not installed. Install it with: winget install --id GitHub.cli'
}

Write-Host 'Verifying the release assets...'
$sumPath = Join-Path (Join-Path $repoRoot $ReleaseDir) 'SHA256SUMS.txt'
if (-not (Test-Path -LiteralPath $sumPath)) {
    throw "Missing $sumPath."
}

$assets = New-Object 'System.Collections.Generic.List[string]'
foreach ($line in Get-Content -LiteralPath $sumPath) {
    if ($line -notmatch '^\s*([0-9a-fA-F]{64})\s+\*?(.+?)\s*$') {
        continue
    }
    $expected = $Matches[1].ToLowerInvariant()
    $name = $Matches[2]
    $path = Join-Path (Join-Path $repoRoot $ReleaseDir) $name
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing release asset: $name"
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) {
        throw "Checksum mismatch for $name`n  expected $expected`n  actual   $actual"
    }
    Write-Host ("  ok  {0}  {1}" -f $name, $actual)
    $assets.Add($path)
}
if ($assets.Count -eq 0) {
    throw "No release assets are listed in $sumPath."
}

if ((Test-CommandQuiet { gh auth status }) -ne 0) {
    Write-Host 'Signing in to GitHub...'
    & gh auth login
    Assert-ExitCode 'gh auth login'
}

$branch = (& git rev-parse --abbrev-ref HEAD).Trim()
Assert-ExitCode 'git rev-parse'
if ($branch -ne 'main') {
    if ((Test-CommandQuiet { git rev-parse --verify --quiet 'refs/heads/main' }) -eq 0) {
        Write-Host 'Switching to the existing main branch...'
        & git switch main
    } else {
        Write-Host "Creating main from $branch ..."
        & git switch -c main
    }
    Assert-ExitCode 'git switch'
}

$changes = & git status --porcelain
if ($changes) {
    Write-Warning 'Uncommitted changes stay behind: only committed work is pushed.'
}

$slug = $RepoName
if ($Owner) {
    $slug = '{0}/{1}' -f $Owner, $RepoName
}

if ((Test-CommandQuiet { gh repo view $slug }) -eq 0) {
    Write-Host "Repository $slug already exists."
} else {
    $visibilityFlag = '--private'
    if ($Visibility -eq 'public') {
        $visibilityFlag = '--public'
    }
    Write-Host "Creating $slug ..."
    & gh repo create $slug $visibilityFlag --source . --push `
        --description 'PKG Studio: package analysis and reverse-engineering IDE'
    Assert-ExitCode 'gh repo create'
}

if ((Test-CommandQuiet { git remote get-url origin }) -ne 0) {
    $remoteUrl = (& gh repo view $slug --json url -q .url).Trim()
    Assert-ExitCode 'gh repo view'
    & git remote add origin $remoteUrl
    Assert-ExitCode 'git remote add'
}

Write-Host 'Pushing main ...'
& git push -u origin main
if ($LASTEXITCODE -ne 0) {
    throw ('git push failed. If the repository was created with a README or licence, run ' +
           '"git pull --rebase origin main" and start the script again.')
}

if ((Test-CommandQuiet { gh release view $Tag }) -eq 0) {
    Write-Host "Release $Tag already exists; refreshing its assets."
} else {
    & gh release create $Tag --title ('PKG Studio ' + $Tag.TrimStart('v')) --notes $Notes
    Assert-ExitCode 'gh release create'
}

foreach ($asset in $assets) {
    Write-Host ("Uploading {0} ..." -f (Split-Path -Leaf $asset))
    & gh release upload $Tag $asset --clobber
    Assert-ExitCode ("gh release upload " + (Split-Path -Leaf $asset))
}

$repoUrl = (& gh repo view $slug --json url -q .url).Trim()
Assert-ExitCode 'gh repo view'
Write-Host ''
Write-Host "Done. Repository: $repoUrl"
Write-Host "Release:    $repoUrl/releases/tag/$Tag"
