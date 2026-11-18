; Inno Setup 6 script for the PKG Studio Windows demo installer.
;
; packaging/windows/build-installer.ps1 stages the application (Qt DLLs,
; plugins, samples, templates) and invokes this script with:
;   /DAppVersion=<version> /DStageDir=<absolute staged folder>
;
; This installer has NOT been built or executed in the Linux development
; environment. See packaging/windows/README.md for the required Windows
; verification checklist before distributing it.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\..\dist\PKGStudio-win64"
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
OutputBaseFilename=PKGStudio-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\PKGStudio.exe
; Development demo build: not code-signed. SmartScreen may show a warning; that
; is documented and expected, not something the installer tries to bypass.

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\PKG Studio"; Filename: "{app}\PKGStudio.exe"
Name: "{group}\PKG Studio samples"; Filename: "{app}\samples"
Name: "{autodesktop}\PKG Studio"; Filename: "{app}\PKGStudio.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\PKGStudio.exe"; Description: "Launch PKG Studio"; Flags: nowait postinstall skipifsilent
