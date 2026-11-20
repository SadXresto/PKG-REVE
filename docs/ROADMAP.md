# Roadmap and honest status

Status of the phases from the project brief. "Done" means implemented, built and exercised by
the automated tests, the self-test or a captured screenshot. Everything else is explicitly not
claimed.

## Phase 1 — Desktop shell: **done**

Sidebar, explorer, tabs, command palette, status bar, output panel, theme manager with four
themes and a theme editor, settings dialog with shortcut customization, logging, session
restore, recent files. Panels are resizable split views (not dockable floating windows).

## Product experience upgrade (2026-11-17)

- Priority 1 — Visual binary structure builder: **done for declarative templates** (JSON schema
  with a form-based field editor and advanced JSON view, nested structs, arrays, endianness,
  tree, hex sync, validation, built-ins, docs). Not implemented: drag-and-drop field reordering,
  dynamic lengths/offsets, unions.
- Priority 8 — Smart command center: **done** (commands with shortcuts/descriptions, open files,
  analysis, settings-related commands, offline documentation, recent actions, ranked search).
- Priorities 2–7 and 9–10: **not started** in this session.

## Demo readiness (2026-11-18)

- First-launch Welcome tab with a development-build identity, synthetic sample actions, demo
  tips and an honest feature-status page: **done**. Sample buttons disable themselves with an
  explanation when the samples are not installed.
- About screen with version, build type, Qt, platform, data directory and sample location:
  **done**. Feature status separates implemented, partial/experimental and planned work.
- Sample workspace: `samples/README.md` tour plus File/Help entries for the sample files and
  project. Samples remain synthetic and legally safe.
- Linux portable packaging with bundled Qt/fonts/samples and an isolated `--selftest`:
  **verified**. Windows Inno Setup pipeline: **provided but untested** (no Windows toolchain or
  host in this environment).

## Phase 2 — Design system / Void Black: **done**

Void Black default palette, Neon Hex, shared role tokens and component sheet, preferred
monospace font resolution, design documentation and contrast/default tests. Sidebar collapse,
free docking, workspace layouts and a configurable status bar are **not** implemented.

## Phase 2 (original brief) — Binary analysis core: **done for documented headers**

`Result`/`Status`, overflow-safe math, mmap file source, streaming search, parser registry with
probe scoring and raw fallback, ELF/PE/ZIP/APK/PSP-Vita-PKG/PS4-PKG header parsers, SQLite
analysis cache. Not done: ZIP64 directories, section/symbol tables, imports/exports.

## Phase 3 — Hex editor upgrades: **partial**

Implemented: live byte inspector (offset/hex/dec/bin/ASCII, signed/unsigned integers in both
endiannesses, floats), bounded signature scanner with confidence and analysis/cache integration,
hex overlay for signature ranges, same-length replace with undo, and cancellable background
saving with progress.

Not implemented: insert/delete (requires a variable-length document model), multi-range
selection, bookmarks/annotations, binary comparison and patch history.

## Phase 4 — Platform adapters: **partial**

PS Vita/PSP and PS4 container headers are implemented. PS5 is not implemented and not claimed.
APK is ZIP-level only. No extraction or install validation. Platform adapter abstraction beyond
`IFormatParser` (device workflows, package building) is not started.

## Phase 5 — Disassembly and debugging: **partial — byte listings only**

The Code tab disassembles *bytes* into editable text and assembles them back (see
[CODE-LISTING.md](CODE-LISTING.md)). This is not instruction decoding. There is no Capstone,
no LLDB, no registers/memory/breakpoints UI; those are absent rather than stubbed.

## Phase 6 — Modding studio: **partial — safe byte editing is in**

Implemented: opt-in same-size byte editing in the hex workspace, per-file undo/redo, modified
state tracking, Save / Save As with overwrite confirmation, timestamped backups, atomic
whole-file writes, and unsaved-change prompts on tab close and quit.

Not implemented: inserting/deleting bytes, multi-range editing or paste, asset/config-aware
editing, diff view, modification history beyond the in-memory undo journal, export validation,
package rebuilding, and any write path for encrypted or protected content.

## Phase 7 — Plugin SDK, AI assistant, license manager: **partial — AI assistant in**

A design sketch for plugins exists in [PLUGIN_SDK.md](PLUGIN_SDK.md); there is no loader, no
manifest parser and no sample plugin. The AI assistant is implemented as a local-endpoint
OpenAI-compatible client that proposes validated, undoable byte edits (no hosted account, no
relay). There is no license manager; licensing is a static `LICENSE` file.

## Phase 8 — Optimization, testing, hardening: **in progress**

Done: 73 automated tests (unit, malformed-input, fuzz-style, performance bounds, widget-level
key events, signatures, inspector, replace), AddressSanitizer + UBSan run, bounded memory
behaviour on an 8 GiB sparse file, no-animation lightweight mode, thread caps, atomic writes,
redacted rotating logs, crash handler.

Remaining: background saving with progress/cancellation (saves are synchronous today),
libFuzzer harnesses, broader UI automation tests, Windows CI and installer, long-running soak
tests, real-hardware low-end benchmarks, and testing against real (authorized) packages.
