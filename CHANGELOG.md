# Changelog

All notable changes to PKG Studio are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions use semantic
versioning once a public release exists.

## [0.10.0] - 2026-11-22

Apex update: UI polish, the theme collection and the DevFlow productivity engine.

### Added

- **Theme collection**: Cyber Blue, Blood Circuit, Terminal Classic and Arctic Dev,
  added through the existing JSON theme system. Every built-in theme now carries a
  description for pickers, and a WCAG contrast helper backs readability tests.
- **Hex editor polish**: compact identity strip (file, size, offset, selection, view
  mode), header undo/redo buttons, bookmark and annotation navigation with gutter
  markers, and a bounded binary comparison dialog with jump-to-offset.
- **Output console**: text search, timestamp toggle, pause control, copy handling,
  confirmed clear, source navigation from compiler-style lines, and credential
  redaction before display.
- **Settings center**: ten searchable sections (Appearance, Editor, Hex Editor,
  GameForge, AI Providers, Performance, Keyboard Shortcuts, Plugins, Privacy and
  Security, Updates), per-setting and per-category reset, restore-defaults
  confirmation, descriptions, and live theme preview reverted on cancel.
- **DevFlow**: dashboard, build profile manager with real `cmake`/`meson`/`make`
  execution (no shell), error intelligence with local explanations and grouped
  diagnostics, SQLite-backed task board with starter templates, workspace snapshots
  with crash recovery, evidence-based project health checks, and bounded read-only
  workflows that generate a Markdown report.
- **Command center**: settings sections, recent projects and quick actions are now
  searchable alongside commands, open files, tools and help topics.
- **Quick actions**: hex, metadata, SHA-256 hash, comparison, bookmark, containing
  folder, path copy and file properties, offered only when supported.

### Changed

- Database schema v2 adds a `devflow` table for profiles, tasks, snapshots,
  workflows, error state and activity.
- Screenshot diagnostics accept `--screenshot-devflow-tab` and
  `--screenshot-size WIDTHxHEIGHT`.

## [0.9.1] - 2026-11-22

### Added

- **`samples/gameforge-demo`**: a small synthetic CMake game project (two targets,
  declared SDL2/OpenGL dependencies, undeclared `d3d11`/`xinput`/`pthread` link names,
  platform conditions, `CMakePresets.json` and Win32/D3D source) so the GameForge
  workspace can be demonstrated and screenshotted from a real project tree.
- **GameForge screenshot mode**: `--screenshot-tab gameforge` with
  `--screenshot-gameforge-tab inventory|graph|config|portability|intel` renders the
  real workspace after its background scan and analysis finish (30 s timeout) and
  widens the sidebar for a readable graph. Nothing about the panel changes in
  interactive use.

### Changed

- README documents the GameForge demo and shows a captured dependency graph;
  version 0.9.1.

## [0.9.0] - 2026-11-22

### Added

- **GameForge build-configuration inspector**: parses CMake (`CMakeLists.txt`,
  `*.cmake`), Meson, Makefiles, `project.godot` and Unreal `*.uproject` files into
  targets, dependencies with versions, compiler settings, architecture flags and
  platform conditions. Every finding keeps its `file:line` and the source line it
  came from, and double-clicking a row opens that file.
- **Dependency graph**: a layered, theme-aware canvas with zoom, pan, node search,
  selection and a details panel. Edges stated in a build file are solid; the project
  root and link-only dependencies are drawn dashed. Cycles are reported as paths,
  link entries that are never declared are listed as unresolved, and node/edge counts
  are capped with an explicit "limited" note.
- **Porting intelligence tab**: Windows, Linux/POSIX, architecture, graphics/input and
  external-dependency sections assembled only from parsed build files and the existing
  API scan, plus build-configuration recommendations that cite their evidence and
  state when nothing was found.
- Graph/config/intelligence refresh automatically after a project scan, on a worker
  thread with cancellation; a generation counter discards results from an older
  project.

### Changed

- GameForge's single "Analyze portability" action is now **Re-analyze**: one background
  pass refreshes the build config, graph, API scan and porting intelligence together.
- Version 0.9.0.

## [0.8.0] - 2026-11-22

### Added

- **GameForge workspace** (Ctrl+Shift+G): scans a local game project folder in the
  background and shows a categorised inventory with per-file and per-category sizes.
  Detects CMake, Meson, Makefile, Godot, Unity and Unreal projects from their marker
  files, skips VCS/engine-cache/build folders, never follows symbolic links and keeps
  the original project untouched.
- **Portability evidence report**: reads source-like files and reports known API
  families (Win32, Direct3D, Metal, OpenGL, Vulkan, SDL, GLFW, XInput, XAudio2,
  CoreAudio, OpenAL, FMOD/Wwise, pthread, std::thread) with occurrence counts and a
  `file:line` example. Rows open their first match in the hex editor. The report states
  its own limits: matches are evidence, not a build result or an automatic port.
- New GameForge help topic, activity-bar entry, command-palette command and Tools menu
  action.

### Changed

- Help → Feature status now separates the working GameForge scanner from the
  GameForge phases that are still planned (modding workbench, performance lab, build
  execution, export).

## [0.7.4] - 2026-11-22

### Changed

- **Glass UI**: panels, toolbars, buttons, inputs, tabs and menus are now translucent
  surfaces over a soft accent gradient that glows from the top-left corner, with rounded
  corners and hairline edges. The effect is generated from the active theme's colours, so
  every built-in and user theme picks it up; a wallpaper still replaces the gradient.
- Scrollbars are slimmer and translucent, and the status bar keeps its own theme colour
  instead of borrowing the activity bar's.

## [0.7.3] - 2026-11-21

### Fixed

- *Find on this computer* no longer looks hung: a local server that answers is read as soon as
  its body is complete instead of waiting out the per-request timeout, and disconnected mapped
  network drives are skipped before anything touches them (their first access can block for
  seconds). A healthy server now answers the scan in milliseconds.
- The RAM-fit hint shows a real em dash instead of a literal `\u2014` escape.

### Changed

- *List models* and the local scan share one model-list parser, so the two can no longer
  disagree about names, namespaced tags or sizes. Drive roots are only probed when they exist
  and are local.

## [0.7.2] - 2026-11-20

### Added

- **Find local models on this computer**: a button in the AI chat panel looks for Ollama, LM
  Studio, llama.cpp and vLLM on their usual loopback ports and reads Ollama model folders on
  every drive, so a model installed under a namespace (for example
  `huihui_ai/qwen3.4b:7b`) shows up with its exact tag. When no server is running it still lists
  the models found on disk and explains how to start the server. Each entry carries its on-disk
  size and whether it fits this machine's RAM; the scan sends a handful of tiny local requests
  and reads only the standard model folders, so it stays cheap on low-end machines.

### Fixed

- Choosing a local provider preset no longer contacts the endpoint. Models are listed only when
  *List models* or *Find on this computer* is pressed, so a server that is not running no longer
  writes an error into the status line by itself.
- Byte edits from a reply are tied to the file they were produced for: if that tab closes or
  another file becomes active, *Apply edits* is disabled instead of applying to the wrong
  document. The in-flight reply no longer keeps a raw editor pointer.
- Choosing a preset now shows its hint text, which was previously unused.

## [0.7.1] - 2026-11-20

### Added

- **Docked AI chat** (`Ctrl+Shift+I`): the assistant is now a chat panel docked beside the
  editors, like the chat views in VS Code and OpenCode, instead of a separate window. Pick a
  provider preset (Ollama, LM Studio, llama.cpp on this computer, or the OpenAI API/custom
  endpoint), press *List models* to offer the models already installed on the endpoint, and
  Enter sends while Shift+Enter adds a line. The panel follows the active editor, shows which
  file it will attach, and applies validated, undoable byte edits.
- **Application icon**: a drawn logo ships as the window/taskbar icon, the Welcome page mark,
  the Windows executable icon and the installer icon.

### Fixed

- The Windows build is a GUI-subsystem executable, so starting the app no longer opens an
  extra console window. `--selftest`, `--help` and `--version` still print to a console.
- The wallpaper transparency rule is scoped to the main window, so dialogs keep their normal
  opaque splitters and stacks.

## [0.7.0] - 2026-11-20

### Added

- **Readable listings (Code tab)**: any file is rendered as editable `offset  hex  ascii`
  text (`core/CodeListing`) and edited text is validated and assembled back as undoable byte
  edits. Works on ELF, PE, ZIP/APK and PSP/Vita/PS4 PKG headers; explicitly documented as a
  byte listing, not a CPU disassembler, in `docs/CODE-LISTING.md` and the help topic.
- **Local AI assistant** (`Ctrl+Shift+I`): OpenAI-compatible `/chat/completions` client with
  configurable endpoint/model and optional API key stored in `ai.key` (owner read/write, never
  logged). Prompts can include the open file's first 8 KiB of listing; `pkgstudio-edit` blocks
  in a reply enable *Apply edits to file*, which uses the same validated, undoable path as the
  Code tab. Loopback endpoints keep everything on the machine.
- **Image themes**: themes carry an optional `backgroundImage` and `backgroundOpacity`; the
  image is copied into the theme folder, painted behind the window and shown through
  translucent panels. Editable in the theme editor and via *View → Set theme background
  image*.
- **Licence and policies**: proprietary `LICENSE` with an anti-piracy notice, `TERMS.md`,
  `PRIVACY.md`, and matching offline help topics (Privacy policy, Terms of use, AI assistant,
  Readable listings).
- Tests: `tests/test_code_listing.cpp` (listing round trip, parsing, undoable apply, AI block
  extraction) and `tests/test_ai_client.cpp` (request/response against a local fake endpoint).

### Changed

- Version 0.7.0. The Windows ZIP and installer built earlier are 0.6.0 and do not include
  these features.

## [Unreleased]

### Added

- **Cross-built Windows x64 demo**: `packaging/windows/build-windows-cross.sh` builds
  `PKGStudio.exe` on Linux with MinGW-w64 gcc 13 and the official Qt 6.8.3 `mingw_64`
  binaries, stages the Qt runtime, plugins, MinGW runtime and samples, and produces a
  portable ZIP, an NSIS installer and `SHA256SUMS.txt` without a Windows host.
  `packaging/windows/PKGStudio.nsi` installs per user under
  `%LOCALAPPDATA%\Programs\PKG Studio` (no admin) with Start Menu shortcuts and an
  uninstall entry, using the `amd64-unicode` stub. Verified under Wine 10 only: self-test
  passes for the portable and installed copies (ELF and PE samples), and install and
  uninstall leave no folder, shortcut or registry key behind. Not run on real Windows.
- **Windows release workflow**: `.github/workflows/windows-release.yml` is a manual
  (`workflow_dispatch`) pipeline on `windows-2022` that generates the demo samples, builds
  the MSVC x64 app against Qt 6.8.3, stages the Qt runtime and plugins, bundles Microsoft's
  signed Visual C++ redistributable (installed only when the runtime is missing), creates the
  Inno Setup installer `PKG-Studio-Setup-x64.exe`, a portable ZIP
  and `SHA256SUMS.txt`, installs and uninstalls the result, and uploads the installer, ZIP,
  checksums and a pass/fail/untested report as the `PKG-Studio-Windows-x64` artifact. It can
  optionally publish a GitHub Release. Never executed from the Linux development
  environment, so the Windows artifact remains unverified.
- **Windows install/uninstall checks**: `packaging/windows/smoke-test.ps1` now also verifies
  the MSVC runtime, the Start Menu shortcut, the Add/Remove Programs entry, and that
  uninstall removes the install folder, shortcut and registry entry.
- **Release report**: `packaging/windows/make-release-report.ps1` produces
  `dist/release-report.md` with separate passed, failed and *not tested* sections plus
  artifact hashes, so unverified items are never reported as working.
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

### Fixed

- **File logging on Windows**: `src/core/Logging.cpp` used POSIX `open`/`write`/`flock`
  unconditionally, so the Windows build wrote no log at all. The POSIX path is now guarded
  by `Q_OS_LINUX` and other platforms append through `QFile` with the same rotation.
  Verified by the cross-built Windows executable writing `pkgstudio.log` under Wine.

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
