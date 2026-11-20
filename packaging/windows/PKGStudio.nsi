; NSIS 3 installer for the PKG Studio Windows x64 build.
;
; Used by packaging/windows/build-windows-cross.sh, which stages the folder and
; invokes makensis with:
;   -DAppVersion=<version> -DStageDir=<staged folder> -DOutFile=<setup.exe>
;
; This is a per-user install (no administrator prompt): files go to
; %LOCALAPPDATA%\Programs\PKG Studio, shortcuts and the uninstall entry live
; under HKCU, and uninstalling removes exactly what the installer created.
; The build is not code-signed, so SmartScreen may warn on first run.

Unicode true
; 64-bit installer stub: the application targets Windows 10/11 x64 only, and a
; native x64 stub avoids running the installer through WOW64.
Target amd64-unicode

!include "MUI2.nsh"

!ifndef AppVersion
  !define AppVersion "0.0.0"
!endif
!ifndef StageDir
  !define StageDir "..\..\dist-win\PKGStudio-win64"
!endif
!ifndef OutFile
  !define OutFile "PKGStudio-Setup-x64.exe"
!endif

!define APPNAME "PKG Studio"
!define PUBLISHER "PKG Studio"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\PKGStudio"
!define APPKEY "Software\PKGStudio"

Name "${APPNAME} ${AppVersion}"
OutFile "${OutFile}"
InstallDir "$LOCALAPPDATA\Programs\PKG Studio"
InstallDirRegKey HKCU "${APPKEY}" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${AppVersion}.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "FileDescription" "${APPNAME} installer"
VIAddVersionKey "FileVersion" "${AppVersion}"
VIAddVersionKey "ProductVersion" "${AppVersion}"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "LegalCopyright" "Development demo build"

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\PKGStudio.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start PKG Studio"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "PKG Studio (required)" SEC_APP
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${StageDir}\*.*"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortCut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\PKGStudio.exe"
  CreateShortCut "$SMPROGRAMS\${APPNAME}\Samples.lnk" "$INSTDIR\samples"
  CreateShortCut "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk" "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "${APPKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${APPKEY}" "Version" "${AppVersion}"

  WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion" "${AppVersion}"
  WriteRegStr HKCU "${UNINSTKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\PKGStudio.exe"
  WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Samples.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk"
  RMDir "$SMPROGRAMS\${APPNAME}"

  RMDir /r "$INSTDIR"

  DeleteRegKey HKCU "${UNINSTKEY}"
  DeleteRegKey HKCU "${APPKEY}"
SectionEnd
