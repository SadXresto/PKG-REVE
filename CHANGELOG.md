# Changelog

All notable changes to PKG Studio are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions use semantic
versioning once a public release exists.

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
