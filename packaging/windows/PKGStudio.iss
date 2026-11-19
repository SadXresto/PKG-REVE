; Inno Setup 6 script for the PKG Studio Windows demo installer.
;
; packaging/windows/build-installer.ps1 stages the application (Qt DLLs,
; plugins, samples, templates) and invokes this script with:
;   /DAppVersion=<version> /DStageDir=<absolute staged folder>
;   /DSetupBaseName=<installer file name without extension>
;
; The GitHub workflow passes SetupBaseName=PKG-Studio-Setup-x64. See
; packaging/windows/README.md for the verification checklist.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\..\dist\PKGStudio-win64"
#endif
#ifndef SetupBaseName
  #define SetupBaseName "PKGStudio-setup"
#endif

[Setup]
AppId={{6A1E9B2C-7D34-4F0B-9E52-3C8A5D1F7B64}
AppName=PKG Studio
AppVersion={#AppVersion}
AppVerName=PKG Studio {#AppVersion} (development demo)
AppPublisher=PKG Studio
DefaultDirName={autopf}\PKG Studio
DefaultGroupName=PKG Studio
DisableProgramGroupPage=yes
OutputDir=..\..\dist
OutputBaseFilename={#SetupBaseName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 and 11 only; the application needs a modern Qt 6 runtime.
MinVersion=10.0
PrivilegesRequired=admin
UninstallDisplayIcon={app}\PKGStudio.exe
; Development demo build: not code-signed. SmartScreen may show a warning; that
; is documented and expected, not something the installer tries to bypass.

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
; The redistributable is not part of the application folder; it is extracted to
; a temporary folder and installed only when the machine lacks the runtime.
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "vc_redist.x64.exe"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\PKG Studio"; Filename: "{app}\PKGStudio.exe"
Name: "{group}\PKG Studio samples"; Filename: "{app}\samples"
Name: "{autodesktop}\PKG Studio"; Filename: "{app}\PKGStudio.exe"; Tasks: desktopicon

[Run]
; Installs the Microsoft Visual C++ runtime only when it is not already present.
; /quiet /norestart is the documented silent mode; Inno ignores the exit code,
; and an existing newer runtime makes the redistributable a no-op.
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Microsoft Visual C++ runtime..."; Flags: waituntilterminated; Check: NeedsVcRuntime
Filename: "{app}\PKGStudio.exe"; Description: "Launch PKG Studio"; Flags: nowait postinstall skipifsilent

[Code]
function NeedsVcRuntime: Boolean;
begin
  Result := not FileExists(ExpandConstant('{sys}\vcruntime140.dll'));
end;
