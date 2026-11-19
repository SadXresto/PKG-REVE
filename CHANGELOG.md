# Changelog

All notable changes to PKG Studio are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions use semantic
versioning once a public release exists.

## [Unreleased]

### Added

- **Windows CI**: `.github/workflows/windows-installer.yml` builds the MSVC x64 application,
  packages the Inno Setup installer and runs `packaging/windows/smoke-test.ps1`, which installs
  the EXE silently, runs `--selftest` on a bundled sample, captures the About and Feature status
  dialogs, verifies the PNGs and uninstalls. Installer and smoke report are uploaded as
  artifacts. The workflow has not been run from this Linux environment.
- **Dialog capture**: `--screenshot-dialog about|feature-status` (with `--screenshot <path>`)
  renders either dialog headlessly and fails with exit code 2 if it stays blank, so CI and
  documentation can prove the two explanation dialogs actually render. The About box moved into
  `ui/AboutDialog.*` so it can be built without a main window.
- `tests/test_dialogs.cpp` covers the About and Feature status contents and asserts that both
  dialogs render to non-blank images.

### Changed

- Help dialogs are built by `HelpDialog::createTopicDialog` and shown by `showTopic`, so the
  same construction path is used interactively, by tests and by the diagnostics CLI.

## [0.6.0] — 2026-11-18

### Added

- **Demo-ready first launch**: the Welcome tab shows a development-build identity, a *Start
  here* block with synthetic ELF and APK samples, a sample-project action, demo tips and a
  *What works in this build?* button. Sample actions disable themselves with an explanation when
  the files are not installed next to the application instead of failing later.
- **Help → Feature status**: an offline page separating implemented and tested features, partial
  or experimental behaviour and planned work.
- **About screen**: version, explicit development-build label, build type, Qt version, platform,
  data directory and sample location, with a detailed diagnostics section.
- `samples/README.md`: what each synthetic file demonstrates, a five-minute demonstration script
  and the honest limits.
- File and Help menu entries for the sample file/project and feature status; sample opening
  reveals the analysis tab when parsing finishes (including cache hits).
- `scripts/package-portable.sh`: a self-contained Linux demo folder and `.tar.gz` bundling the
  Qt runtime, SQLite driver, fonts and samples, verified by extracting it and running an isolated
  `--selftest` with `env -i`.
- `packaging/windows/`: MSVC build + `windeployqt` staging script, Inno Setup definition and a
  required Windows verification checklist. **Not built or tested in the Linux development
  environment.**

### Changed

- Version 0.6.0. The compiled build type is shown in About.
- Launching with explicit files or a project no longer restores the previous session on top of
  them; only a plain launch restores the workspace.
- The Welcome page explains why the recent list is empty and points at the samples.

### Known limitations

- No Windows EXE was produced or installed here: the environment has no MSVC, Windows Qt kit or
  Windows host. The Linux portable bundle is the verified artifact; the Windows checklist is
  mandatory before publishing an installer.
- The build is unsigned and the About dialog was not captured in an automated visual test.

## [0.5.1] — 2026-11-18

### Added

- **Form-based structure template editor**: add, edit, remove and reorder fields (name, type,
  offset, length, byte order, array count and description) and build nested structs by adding a
  struct and then using *Add nested field* inside it. Offsets accept decimal or `0x` notation;
  names, lengths and numeric ranges are checked before anything is applied. Removing a struct
  with children, or changing its type, asks first.
- The template dialog now has a **Form** tab next to **JSON (advanced)** for the same template.
  Switching tabs carries the layout across, so form work can be reviewed as JSON and
  hand-written templates can be opened in the form. Invalid JSON keeps the user in the JSON tab
  instead of discarding it.
- Three widget tests: loading and rebuilding a template unchanged, nested
  add/replace/remove/move operations, and actionable validation messages.

### Changed

- Version 0.5.1. New templates start from the two-field example in both tabs rather than only
  in the JSON text.

### Known limitations

- Fields are reordered with *Move up* / *Move down*; drag-and-drop reordering is not implemented.
  The form still uses static offsets and lengths: no dynamic expressions, unions or bitfields.

## [0.5.0] — 2026-11-17

### Added

- **Visual binary structure builder** beside the hex view: JSON templates with offsets, fixed
  lengths, primitive/text/bytes types, per-field endianness, arrays and nested structs. A
  hierarchical tree shows decoded values; tree selection selects and scrolls the hex range and
  the hex cursor highlights the deepest containing field. 64-bit integers display hex with the
  decimal value in the tooltip to keep columns readable.
- Template validation (schema, duplicate sibling names, depth/array/field caps, file bounds) with
  an explicit errors panel. Out-of-bounds fields are marked `not read` and explained; values are
  never guessed.
- Template editor dialog (JSON with Validate / Save / Apply), user templates in
  `<data>/templates/*.json`, template loading from disk and a folder shortcut.
- Built-in ELF64, ZIP local-header and PNG IHDR example templates, shipped both as editable files
  in `templates/` and compiled into the app; equality between the two is tested.
- `docs/STRUCTURE-TEMPLATES.md` and an offline in-app structure-template guide.
- **Smart command center**: ranked search over commands (with shortcuts and descriptions), open
  files, the analysis tab, settings-related commands, offline documentation topics and recent
  actions, with arrow-key navigation and clear empty-state suggestions.
- Offline help viewer with Getting started, Keyboard shortcuts, Structure templates and
  Limitations topics, exposed through Help and the command center.

### Changed

- Version 0.5.0. `Ctrl+Shift+B` toggles the structure builder; Tools and Help menus gained the
  template and documentation entries.
- Recent actions are kept for the session (not persisted) and are clearly labelled as such.

### Known limitations

- Templates use a single base at offset 0 and static lengths; dynamic length/offset expressions,
  unions and data-dependent arrays are not supported. They are layouts, not format detection.
- Priorities 2–7 and 9–10 of the product brief (intelligence map, diff, learning mode, flow
  explorer, reports, community hub, onboarding/auto-save, extended quality gates) are not built.

## [0.4.0] — 2026-11-16

### Added

- Asynchronous saving on the shared worker pool with a window-modal progress dialog reporting
  backup and write progress. Cancel stops at the next 4 MiB chunk, removes the temporary file,
  keeps all edits and leaves the original untouched.
- `HexDocument::saveTo` and `atomic::backupFile` accept a `CancelToken` and a byte-progress
  callback; cancellation is honoured before creating the temporary file, between chunks and
  immediately before the atomic commit.
- Six tests covering pre-cancelled saves, cancellation mid-stream (including leftover temp-file
  checks), monotonic progress reporting and cancelled backups.

### Changed

- Version 0.4.0. Saving is no longer performed on the UI thread; the file is reopened and the
  edit journal reset on the UI thread only after the worker committed successfully.
- The save-failure dialog now distinguishes a failed write from a failed reload after a
  successful write.
- Closing a modified tab or the window still waits for Save/Discard/Cancel; cancelling the
  progress dialog aborts the close instead of losing edits.

## [0.3.0] — 2026-11-16

### Added

- **Void Black** design system and default theme (`#0a0a0a` surfaces, `#00ff41` accent, green
  borders), Neon Hex theme, component QSS rules, design-token documentation and contrast tests.
- Preferred monospace font resolution (JetBrains Mono, Fira Code, Cascadia, Source Code Pro,
  Consolas, Liberation/DejaVu Mono) with the system fixed font as fallback.
- Live byte inspector overlay: offset, hex, decimal, binary, ASCII, signed/unsigned 8/16/32/64-bit
  values in both endiannesses, and 32/64-bit floats; updates on hover, cursor move and selection.
- Bounded signature scanner with per-hit confidence and validation notes; results appear in the
  analysis page ("Detected signatures") and are overlaid on the hex bytes. Survives the analysis
  cache via JSON and rejects unvalidated guesses.
- Same-length Replace All after a search, with confirmation, hex/text replacement, undo per byte
  and a clear error when the replacement length differs.
- Diagnostic `--screenshot-tab inspector`, verified Void Black and inspector screenshots.

### Changed

- Version 0.3.0. New installations default to Void Black; existing saved theme choices are kept.
- Inspector and badge component styling moved into the shared theme sheet rather than local styles.

### Known limitations

- Insert/delete, multi-range selection, bookmarks, binary comparison, disassembly, AI, plugins
  and installers remain future phases.

## [0.2.0] — 2026-11-16

### Added

- Opt-in byte editing in the hex workspace: two-digit hex entry in the hex column, printable
  ASCII entry in the ASCII column, pending-nibble preview and accent-coloured modified bytes.
- Per-file undo/redo journal (`Ctrl+Z`, `Ctrl+Shift+Z`) that restores original bytes, drops the
  redo branch after a new edit, and reports the modified state in the title, tab and status bar.
- Save and Save As (`Ctrl+S`, `Ctrl+Shift+S`) with an overwrite confirmation, a timestamped
  backup next to the original, and an atomic whole-file write that keeps edits on failure.
- Automatic re-analysis after saving, recent-file update, and close/quit prompts for unsaved
  edits (Save / Discard / Cancel).
- Tests: document editing, undo/redo branching, chunked save across the 4 MiB boundary, invalid
  destinations, and a widget-level key-event test through the real `HexView`.

### Changed

- Version bumped to 0.2.0. The status bar now distinguishes read-only, editing and modified
  states, and the file header exposes the editing toggle instead of a static read-only label.
- `HexDocument` reads, row rendering, selections and copy now include pending edits; the hex
  view rereads its bounded window after every change.

## [0.1.0] — 2026-11-15

First working milestone: desktop shell, analysis core and read-only hex workspace.

### Added

- C++20 / Qt 6 Widgets application with CMake build, strict warnings, optional ASan+UBSan and
  an in-repo toolchain bootstrap for minimal Linux images.
- VS Code-style shell: activity bar, explorer, recent panel, project search, tabbed editors,
  output/diagnostics panel, status bar, command palette, welcome page, drag and drop.
- Read-only hex workspace over memory-mapped files: cursor/selection, offset navigation,
  copy as hex/ASCII, streaming find with case-insensitive mode and cancellation.
- Format analysis core: parser registry with probe scoring, overflow-checked reads, bounded
  parsing and a raw fallback; parsers for ELF32/64 (both endians), PE32/PE32+, ZIP, Android APK
  (ZIP level), PSP/PS Vita PKG and PS4 PKG container headers.
- Analysis page per file with sections/fields/warnings, copy and export of text reports.
- SQLite persistence (schema v1): settings, recent files/projects, session restore and an
  analysis cache keyed by path, size and mtime.
- Four themes (Dark, Light, Darcula-inspired, Cyberpunk), theme editor, user theme save,
  import/export, theme menu; adjustable editor font size and hex layout.
- Settings dialog: general, hex editor, performance, logging and shortcut customization with
  reset; values clamped on load and save.
- Rotating redacted file log, local crash handler (signal + raw addresses only), atomic file
  writes and backup helper.
- Automated suite: 51 GoogleTest cases across primitives, parsers, hex/search, data layer,
  deterministic fuzz-style malformed input and large-file performance bounds.
- Documentation: README, ARCHITECTURE, BUILD, SECURITY, TESTING, FORMATS, ROADMAP and this
  changelog; headless self-test and screenshot diagnostics.

### Fixed during development

- Theme stylesheet replacement corrupted longer role names (`#1e1e1eText`).
- Layout cycle caused 100% CPU spin at startup.
- Parser registry was never populated, so every file fell back to raw bytes.
- Nearly invisible hint label and black activity-bar icons at startup.
- Out-of-bounds `span.first(64)` in the raw fallback parser (found by AddressSanitizer).
- Settings save failed on null string values (`NOT NULL constraint failed`).
- Synthetic ZIP fixtures used uninitialized padding from `QByteArray::resize`.

### Known limitations

- Hex editing, undo/redo and safe write-back are not implemented; the workspace is read-only.
- No disassembler, debugger, modding studio, plugin loader, AI assistant or license manager.
- PS5 packages are not supported or claimed.
- No installer, no Windows build verified in this environment, panels are not freely dockable.
