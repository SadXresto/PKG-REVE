# Windows x64 installer and release pipeline

This folder builds the Windows demo installer for PKG Studio:

- `build-installer.ps1` — configures and builds the MSVC x64 application, stages the
  executable with `samples/` and `templates/`, deploys the Qt runtime and plugins with
  `windeployqt`, fetches Microsoft's signed `vc_redist.x64.exe` (Qt needs the shared MSVC
  runtime; loose runtime DLLs are not licensed for redistribution), verifies every
  required DLL and plugin, creates a portable ZIP, and runs Inno Setup.
- `PKGStudio.iss` — Inno Setup 6 script: Windows 10/11 x64, Program Files install,
  Start Menu entries, optional desktop shortcut, uninstaller and launch-after-install.
  User settings stay in `%APPDATA%\PKGStudio` and are not touched by uninstall.
- `smoke-test.ps1` — installs the built EXE silently, checks the installed files,
  runtime and shortcuts, runs the installed application, uninstalls, and writes
  `dist\smoke\report.md`.
- `make-release-report.ps1` — combines the smoke results, artifact sizes and SHA-256
  hashes into `dist\release-report.md` with explicit **passed**, **failed** and
  **not tested** sections.
- `.github/workflows/windows-release.yml` — the automated pipeline (see below).

## Status: pipeline added, no Windows run observed yet

The development environment for this repository is Linux: no MSVC, MinGW, Windows Qt
kit, Wine or Windows host. **No Windows EXE has been built, installed or tested here.**
Everything below describes the pipeline that produces and verifies one; the artifact is
**untested** until a run of the workflow (or a manual run of the scripts on Windows) has
completed and its report says so.

A Windows-only release cannot be declared demo-ready from a Linux-only test run. The
artifact verified in this repository's Linux environment is the portable Linux folder
made by `scripts/package-portable.sh`.

## GitHub Actions: Windows release

Workflow: `.github/workflows/windows-release.yml`, job `windows-installer`, runner
`windows-2022` (Windows Server 2022 x64). It is **manual only** (`workflow_dispatch`),
so each release is a deliberate action.

What a run does:

1. Checks out the branch and reads the version from `CMakeLists.txt`.
2. Generates the synthetic demo packages (`tools/make_test_packages.py samples`) —
   `samples/*.elf|apk|exe|pkg` are git-ignored generated files, only `samples/README.md`
   is committed.
3. Sets up the MSVC x64 environment (VS 2022), Qt **6.8.3** `win64_msvc2022_64` (with
   the Qt SQL module and SQLite driver) and Inno Setup 6.
4. Runs `build-installer.ps1 -SetupBaseName PKG-Studio-Setup-x64` (it downloads and
   signature-checks Microsoft's `vc_redist.x64.exe`) and produces:
   - `dist\PKG-Studio-Setup-x64.exe` — the installer,
   - `dist\PKGStudio-<version>-win64.zip` — portable copy of the staged folder,
   - `dist\SHA256SUMS.txt` — SHA-256 of both files.
5. Runs `smoke-test.ps1` (elevated runner): silent install, installed-file checks,
   Visual C++ runtime availability, Start Menu shortcut and Add/Remove Programs entry,
   `--selftest` on the installed sample, About and Feature status dialog captures,
   silent uninstall, and checks that the install folder, shortcut and registry entry
   are gone.
6. Composes `dist\release-report.md` from the results and writes it to the run's summary.
7. Uploads artifact **`PKG-Studio-Windows-x64`** (30 days): installer, ZIP,
   `SHA256SUMS.txt`, `release-report.md`, and `smoke/` (report, logs, dialog PNGs).
8. If `publish_release` was set, attaches the installer, ZIP, checksums and report to a
   GitHub Release at the given tag.

The workflow fails when the build or any smoke check fails. The upload and report steps
run with `if: always()`, so a failing run still leaves the logs and report behind for
diagnosis; the Release step only runs when every previous step succeeded.

### Running it

1. Push the branch to GitHub (see the repository owner's push instructions).
2. **Actions → Windows release → Run workflow**, choose the branch, leave
   `publish_release` off for a test build.
3. Wait for the run (about 10–20 minutes with a cold Qt cache).
4. Open the run summary for the pass/fail/untested table, then download
   **PKG-Studio-Windows-x64** from the run's *Artifacts* section.
5. Verify the download against `SHA256SUMS.txt`:
   `Get-FileHash .\PKG-Studio-Setup-x64.exe -Algorithm SHA256`.
6. Install and demo it, then record the manual checklist below.

To publish a release, run the workflow again with `publish_release` enabled and an
optional `release_tag` (default `v<version>-windows`). This requires the repository
`contents: write` permission, which the workflow requests.

### Reading the report honestly

`release-report.md` has three separate sections:

- **Passed on the runner** — checks the Windows runner executed successfully.
- **Failed on the runner** — anything that failed; the job itself also fails.
- **NOT tested by this run** — signed/SmartScreen behaviour, an interactive desktop
  walkthrough, Windows 10 specifically (the runner is a Server image), upgrade over an
  older install, `%APPDATA%` retention, multi-monitor/high-DPI, very large packages and
  non-admin installs. These are unverified, not "working".

A green run means: the installer built, installed, started the installed application,
analysed a bundled sample, rendered its dialogs and uninstalled cleanly **on that Windows
Server 2022 runner**. It does not mean the installer was code-signed, that SmartScreen is
silent, or that every interactive feature was clicked through.

## Local build (alternative to CI)

Prerequisites: Windows 10/11 x64, Visual Studio 2022 with **Desktop development with
C++**, CMake 3.21+, Qt 6.5+ for MSVC 2022 x64 (including Qt SQL and the SQLite plugin,
e.g. `C:\Qt\6.8.3\msvc2022_64`) and Inno Setup 6.

From a Developer PowerShell for VS 2022 at the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File packaging/windows/build-installer.ps1 `
    -QtDir C:\Qt\6.8.3\msvc2022_64 -SetupBaseName PKG-Studio-Setup-x64

powershell -ExecutionPolicy Bypass -File packaging/windows/smoke-test.ps1 `
    -Setup dist\PKG-Studio-Setup-x64.exe -ReportDir dist\smoke
```

`smoke-test.ps1` must run elevated because the installer is per-machine. Use `-SkipBuild`
to repackage an existing `build-windows` output, and `-VcRedist <path>` to reuse an already
downloaded redistributable. The build script fails if `windeployqt` is missing, if any core
Qt DLL, `sqldrivers\qsqlite.dll` or `platforms\qwindows.dll` is absent, if the
redistributable cannot be downloaded or is not validly Microsoft-signed, or if Inno Setup
is not installed. The portable folder carries `WINDOWS-README.txt` explaining how to run
`vc_redist.x64.exe` if Windows reports a missing runtime DLL.

## Manual verification checklist (required before calling it tested)

Run this on a **clean Windows 10 or 11 VM without Visual Studio, Qt or the repository**.
Record the results; never mark an item passed without running it.

1. Run `PKG-Studio-Setup-x64.exe`. It is unsigned, so SmartScreen may warn; confirm the
   file hash matches `SHA256SUMS.txt`.
2. Install to the default location, launch from the Start Menu shortcut.
3. First launch shows the Welcome tab:
   - "Development build <version> · demo samples included · not a signed release"
   - Open ELF sample, Open APK sample and Open sample project are **enabled**.
4. **Open ELF sample**: analysis tab opens automatically, shows `ELF64`,
   `entry 0x401000` and no errors. Hover a byte to see the inspector.
5. `Ctrl+Shift+B` shows the structure builder with the ELF64 built-in template.
6. **Open APK sample**: `ZIP`/`APK` analysis lists `AndroidManifest.xml`.
7. **Help → Feature status** opens the implemented/partial/planned list, and
   **Help → About** shows version, `Development build`, build type, Qt version, data
   directory and sample path.
8. Save-path check on a scratch copy: enable editing, change one byte, save, confirm the
   timestamped backup appears and the file is written.
9. Search (`Ctrl+F`), command palette (`Ctrl+Shift+P`), Settings (`Ctrl+,`), theme editor
   and output panel toggle all open and close without errors.
10. Close and relaunch: session restore reopens the last files; launching with an
    explicit file does **not** reopen the old session.
11. Uninstall from Settings → Apps: shortcuts and installed files are removed;
    `%APPDATA%\PKGStudio` (settings, logs, user templates) is intentionally retained and
    can be deleted manually.

Until every applicable item is recorded as passed on Windows, report the installer as
**untested**.
