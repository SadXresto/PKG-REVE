# Windows demo installer

This folder contains the Windows build and installer pipeline for PKG Studio:

- `build-installer.ps1` — builds the MSVC x64 application, stages it, runs
  `windeployqt`, checks that the Qt runtime and SQLite driver are present, then
  invokes Inno Setup.
- `PKGStudio.iss` — Inno Setup 6 installer: Program Files install, Start Menu
  entries, optional desktop shortcut, uninstaller and a launch-after-install
  option. User settings stay in `%APPDATA%\PKGStudio` and are not touched by
  uninstall.

## Status: not built or tested in this environment

The development environment for this repository is Linux. It has no MSVC,
MinGW, Windows Qt kit or Windows machine, so **no Windows EXE was produced or
tested here**. The scripts encode the intended supported path; treat them as
untested until someone runs the checklist below on Windows.

A Windows-only release cannot be declared demo-ready from a Linux-only test
run. The tested portable build for this repository is the Linux folder created
by `scripts/package-portable.sh`.

## Prerequisites

1. Windows 10/11 x64.
2. Visual Studio 2022 with **Desktop development with C++**.
3. CMake 3.21+ available in the Developer PowerShell.
4. Qt 6.5+ for MSVC 2022 x64, including **Qt SQL** and the SQLite plugin.
   Example kit: `C:\Qt\6.8.3\msvc2022_64`.
5. [Inno Setup 6](https://jrsoftware.org/isinfo.php) (provides `ISCC.exe`).

## Build

From a Developer PowerShell for VS 2022 at the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File packaging/windows/build-installer.ps1 `
    -QtDir C:\Qt\6.8.3\msvc2022_64
```

Outputs:

- `dist\PKGStudio-<version>-win64\` — portable staged folder (could be zipped
  instead of installed).
- `dist\PKGStudio-<version>-setup.exe` — Inno Setup installer.

Use `-SkipBuild` to repackage an existing `build-windows` output. The script
fails if `windeployqt` is missing, if any of the core Qt DLLs or
`sqldrivers\qsqlite.dll` is absent, or if Inno Setup is not installed.

## Verification checklist (required before calling it tested)

Run this on a **clean Windows VM without Visual Studio, Qt or the repository**.
Record the results; do not mark items as passed without running them.

1. Run `PKGStudio-<version>-setup.exe`. SmartScreen may warn because the demo
   build is unsigned; confirm the publisher/flow is the one you built.
2. Install to the default location, launch from the Start Menu shortcut.
3. First launch shows the Welcome tab:
   - "Development build <version> · demo samples included · not a signed release"
   - Open ELF sample, Open APK sample and Open sample project are **enabled**.
4. **Open ELF sample**: analysis tab opens automatically, shows `ELF64`,
   `entry 0x401000` and no errors. Hover a byte to see the inspector.
5. `Ctrl+Shift+B` shows the structure builder with the ELF64 built-in template.
6. **Open APK sample**: `ZIP`/`APK` analysis lists `AndroidManifest.xml`.
7. **Help → Feature status** opens the implemented/partial/planned list, and
   **Help → About** shows version, `Development build`, build type, Qt version,
   data directory and sample path.
8. Save-path check on a scratch copy: enable editing, change one byte, save,
   confirm the timestamped backup appears and the file is written.
9. Search (`Ctrl+F`), command palette (`Ctrl+Shift+P`), Settings (`Ctrl+,`),
   theme editor and output panel toggle all open and close without errors.
10. Close and relaunch: session restore reopens the last files; launching with
    an explicit file does **not** reopen the old session.
11. Uninstall from Settings → Apps: shortcuts and installed files are removed;
    `%APPDATA%\PKGStudio` (settings, logs, user templates) is intentionally
    retained and can be deleted manually.

Until every applicable item is recorded as passed on Windows, report the
installer as **untested**.
