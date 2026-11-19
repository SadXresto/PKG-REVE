# PKG Studio - installed-application smoke test.
#
#   powershell -ExecutionPolicy Bypass -File packaging/windows/smoke-test.ps1 `
#       -Setup dist\PKGStudio-0.6.0-setup.exe
#
# Installs the Inno Setup EXE silently into a scratch folder, then checks the
# *installed* application rather than the development build:
#   - the bundled Qt runtime and SQLite driver are present,
#   - --selftest starts, opens a bundled sample, analyses it and reports PASS,
#   - the About and Feature status dialogs render to non-blank PNGs.
# It finishes by uninstalling and writing dist\smoke\report.md.
#
# This proves the packaged runtime works on the Windows machine that ran it. It
# does not sign or audit the build, and it is not a substitute for the manual
# checklist in packaging/windows/README.md.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Setup,
    [string]$InstallDir = (Join-Path $env:ProgramFiles "PKG Studio Smoke"),
    [string]$ReportDir = "dist/smoke"
)

$ErrorActionPreference = "Stop"
$script:Failed = $false
$report = New-Object System.Collections.Generic.List[string]

function Add-Result {
    param([string]$Name, [bool]$Passed, [string]$Detail = "")
    $status = "FAIL"
    if ($Passed) {
        $status = "PASS"
    } else {
        $script:Failed = $true
    }
    $script:report.Add("| $Name | $status | $Detail |")
    Write-Host "$status  $Name  $Detail"
}

function Invoke-InstalledApp {
    param([string]$Exe, [string]$Arguments, [string]$LogName)
    $logPath = Join-Path $ReportDir $LogName
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Exe
    $info.Arguments = $Arguments
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $process = [System.Diagnostics.Process]::Start($info)
    if (-not $process.WaitForExit(120000)) {
        try { $process.Kill() } catch { }
        throw "PKG Studio did not exit within 120 seconds ($Arguments)"
    }
    $text = $process.StandardOutput.ReadToEnd() + $process.StandardError.ReadToEnd()
    Set-Content -Path $logPath -Value $text -Encoding utf8
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Output   = $text
        Log      = $logPath
    }
}

function Test-DialogScreenshot {
    param([string]$DialogId, [string]$FileName)
    $path = Join-Path $ReportDir $FileName
    $run = Invoke-InstalledApp -Exe $Exe `
        -Arguments "--screenshot `"$path`" --screenshot-dialog $DialogId" `
        -LogName "dialog-$DialogId.log"
    if ($run.ExitCode -ne 0) {
        Add-Result "$DialogId dialog captured" $false "exit $($run.ExitCode); see $($run.Log)"
        return
    }
    if (-not (Test-Path $path)) {
        Add-Result "$DialogId dialog captured" $false "no PNG at $path"
        return
    }
    $length = (Get-Item $path).Length
    $image = [System.Drawing.Image]::FromFile($path)
    try {
        $width = $image.Width
        $height = $image.Height
    } finally {
        $image.Dispose()
    }
    $ok = ($length -gt 4096) -and ($width -ge 300) -and ($height -ge 150)
    Add-Result "$DialogId dialog renders" $ok "${width}x${height}, $length bytes, $path"
}

$SetupPath = (Resolve-Path $Setup).Path
New-Item -ItemType Directory -Path $ReportDir -Force | Out-Null
$ReportDir = (Resolve-Path $ReportDir).Path
Add-Type -AssemblyName System.Drawing

Add-Result "installer file present" (Test-Path $SetupPath) $SetupPath

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
Add-Result "running elevated (Inno Setup requires admin)" $admin ""

$installLog = Join-Path $ReportDir "install.log"
$installArgs = @(
    "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-",
    "/LOG=$installLog", "/DIR=`"$InstallDir`""
)
$install = Start-Process -FilePath $SetupPath -ArgumentList $installArgs -Wait -PassThru
Add-Result "silent install exit code 0" ($install.ExitCode -eq 0) "exit $($install.ExitCode)"

$Exe = Join-Path $InstallDir "PKGStudio.exe"
$Sample = Join-Path $InstallDir "samples\sample.elf"
Add-Result "installed executable" (Test-Path $Exe) $Exe
Add-Result "installed sample files" (Test-Path $Sample) $Sample
foreach ($dll in @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Sql.dll")) {
    Add-Result "bundled $dll" (Test-Path (Join-Path $InstallDir $dll)) ""
}
Add-Result "bundled SQLite driver" (Test-Path (Join-Path $InstallDir "sqldrivers\qsqlite.dll")) ""

# An isolated data folder: the smoke test must not touch the runner's or the
# tester's real PKG Studio settings. Prefer the offscreen platform when
# windeployqt bundled it, otherwise let Qt use the normal Windows platform.
$OffscreenPlugin = Join-Path $InstallDir "platforms\qoffscreen.dll"
if (Test-Path $OffscreenPlugin) {
    $env:QT_QPA_PLATFORM = "offscreen"
} else {
    Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
}
$env:PKGSTUDIO_DATA_DIR = Join-Path $ReportDir "data"

if (Test-Path $Exe) {
    $selftest = Invoke-InstalledApp -Exe $Exe `
        -Arguments "--selftest -f `"$Sample`"" -LogName "selftest.log"
    Add-Result "selftest exit code 0" ($selftest.ExitCode -eq 0) `
        "exit $($selftest.ExitCode), log $($selftest.Log)"
    Add-Result "selftest reported PASS" ($selftest.Output -match "result: PASS") ""
    Add-Result "selftest opened the sample" ($selftest.Output -match "open files: 1") ""

    Test-DialogScreenshot -DialogId "about" -FileName "dialog-about.png"
    Test-DialogScreenshot -DialogId "feature-status" -FileName "dialog-feature-status.png"
}

# Uninstall even when a check failed, then confirm the executable is gone.
$uninstaller = Get-ChildItem $InstallDir -Filter "unins*.exe" -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($uninstaller) {
    $uninstall = Start-Process -FilePath $uninstaller.FullName `
        -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART") -Wait -PassThru
    $deadline = (Get-Date).AddSeconds(30)
    while ((Test-Path $Exe) -and ((Get-Date) -lt $deadline)) {
        Start-Sleep -Milliseconds 500
    }
    Add-Result "uninstall removed the executable" (-not (Test-Path $Exe)) `
        "uninstaller exit $($uninstall.ExitCode)"
} else {
    Add-Result "uninstaller present" $false "no unins*.exe in $InstallDir"
}

$reportPath = Join-Path $ReportDir "report.md"
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# PKG Studio Windows smoke test")
$lines.Add("")
$lines.Add("- Installer: ``$SetupPath``")
$lines.Add("- Install folder: ``$InstallDir``")
$lines.Add("- Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
$lines.Add("")
$lines.Add("| Check | Result | Detail |")
$lines.Add("| --- | --- | --- |")
foreach ($entry in $report) {
    $lines.Add($entry)
}
$lines | Set-Content -Path $reportPath -Encoding utf8

Write-Host ""
Write-Host "Report: $reportPath"
if ($script:Failed) {
    Write-Host "Smoke test FAILED"
    exit 1
}
Write-Host "Smoke test PASSED"
exit 0
