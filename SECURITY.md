# Security policy and scope

## Intended use

PKG Studio analyses files that the user owns or is authorized to inspect: homebrew packages,
self-built binaries, public test packages and developer-authorized assets.

It deliberately does **not** implement:

- decryption of protected commercial packages,
- DRM removal or license circumvention,
- console security bypasses,
- extraction of copyrighted content,
- patching or writing to original packages (the hex workspace is read-only).

Requests to add such capability are out of scope regardless of the licence state of the tool.

## Data handling

- **No telemetry, no analytics, no network calls.** The application contains no HTTP client
  and does not contact any server at runtime.
- Analysed file contents are never uploaded anywhere. Files are memory-mapped read-only.
- Settings, recents, session state and the analysis cache live in the local data directory
  (`~/.local/share/PKGStudio` on Linux, `%APPDATA%\PKGStudio` on Windows).
- Logs are local-only. They record paths and status messages, not file contents.
- Crash reports are written locally. The POSIX handler logs only the signal number and raw
  instruction addresses — no file bytes, no paths, no environment variables, no user names.

## Secrets

- No API keys, tokens or passwords are compiled in, and none are required by this milestone.
- Third-party credentials (for example for a future AI provider) must be supplied by the user at
  runtime, stored outside the repository and never logged. AI features are not implemented.

## Local configuration storage

Preferences live in SQLite and theme files in JSON under the user's data directory. They are
ordinary user-owned files protected by operating-system permissions; PKG Studio does not add
its own encryption layer because it stores no credentials. Do not put secrets in theme files or
settings values. The application never writes to its installation directory.

## Edited files and backups

- Byte edits are held in memory until you save; opening a file never writes to it.
- Saving over an existing file writes a timestamped sibling copy
  (`name.pkgstudio-backup-YYYYMMDD-HHMMSS.bak`) before the atomic replacement. Backups contain
  the same bytes as the original and are ordinary user files; delete them when no longer needed.
- Saving uses a temporary file in the destination directory and an atomic rename. A failed or
  cancelled save leaves the original untouched and keeps the edits in memory.
- Editing cannot change file length, so a save never truncates or extends the original.
- Files opened read-only by the operating system cannot be saved; PKG Studio reports the error
  instead of attempting privilege escalation.

## Input handling and hardening

- File sizes and offsets are validated with overflow-checked arithmetic before every read.
- Counts read from files (entries, sections, items, directory size, search limits) are capped
  and reported as warnings rather than trusted for allocation.
- Parser code is wrapped so a failure cannot crash the process; unknown or malformed data
  degrades to a raw-byte view with an explicit message.
- Reports and logs use atomic writes (temporary file + rename) to avoid partial output.
- Automated tests include malformed, truncated and randomized inputs, and the suite is run under
  AddressSanitizer + UBSan (see [TESTING.md](TESTING.md)).
- Temporary files are created next to the destination with unique names, never in a shared
  world-writable directory.

## Dependency surface

Runtime dependencies are Qt 6 (Core, Gui, Widgets, Concurrent, Sql) and the SQLite Qt driver.
The test suite fetches GoogleTest at configure time when it is not installed. No JavaScript
runtime, embedded browser, package manager or auto-updater is bundled.

## Known limitations

- The in-tree Qt/runtime libraries used to build in this workspace came from official Qt
  binaries and Ubuntu jammy packages; a distributable release must be built against the target
  distribution's Qt with its security updates.
- There is no signed-update mechanism because there is no update mechanism at all.
- Plugin isolation does not exist yet, so no untrusted native code is loaded.

## Reporting a vulnerability

Do not open a public issue containing exploit details, private files or secrets. Report it
privately to the maintainers with: affected version, platform, steps to reproduce, and a
minimal non-confidential sample if one is needed. A response should be expected before public
disclosure. There is no bug-bounty programme.
