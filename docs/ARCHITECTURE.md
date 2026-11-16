# Architecture

PKG Studio is split into four layers with a strict dependency direction:

```
app  ──▶  ui  ──▶  data  ──▶  core
             └──────────────▶  core
```

`core` knows nothing about Qt widgets or SQLite. `data` may use `core`. `ui` may use both.
`app` wires everything together and owns process-level state.

## Module map

| Area | Files | Responsibility |
| --- | --- | --- |
| Core primitives | `src/core/Result.h`, `SafeMath.h`, `ByteReader.*` | Error propagation, checked offset arithmetic, bounded big/little-endian reads |
| File access | `FileSource.*`, `HexDocument.*`, `ByteSearch.*` | Read-only memory mapping, row layout, streaming search |
| Formats | `FormatInfo.*`, `IFormatParser.h`, `ParserRegistry.*`, `parsers/*` | Format metadata model, parser interface, selection by probe score |
| Diagnostics | `Logging.*`, `ProcessInfo.*`, `AtomicFile.*` | Rotating redacted logs, crash handler, RSS/CPU info, atomic writes |
| Persistence | `data/Database.*`, `Settings.*`, `Theme.*`, `ThemeManager.*` | SQLite schema, typed preferences, theme model and QSS generation |
| UI | `ui/*` | Shell, widgets, dialogs, command registry |
| Startup | `app/Application.*`, `main.cpp` | CLI parsing, initialization order, self-test and screenshot modes |

## Startup sequence

1. `QApplication` is created; Fusion style is forced for consistent theming.
2. `Settings::dataDirectory()` resolves the data directory (`PKGSTUDIO_DATA_DIR` override).
3. `Logger` opens `logs/pkgstudio.log` (rotated at 2 MiB, one previous file) and installs a
   Qt message handler plus a POSIX crash handler.
4. `Database` opens `pkgstudio.db` and creates schema v1 if needed.
5. `Settings` loads persisted values and clamps them to safe ranges.
6. `ParserRegistry::registerBuiltins()` registers ELF, PE, ZIP, Vita PKG, PS4 PKG and raw.
7. `ThemeManager` loads the four built-in themes and any user themes; the active theme is
   rendered to QSS and applied.
8. `QThreadPool` is capped (default `clamp(cores, 1, 4)`, or a user value clamped to 1–64).
9. `MainWindow` restores session/recent state and applies editor preferences.

`--selftest` and `--screenshot` reuse exactly this path, so a headless pass is evidence that
real startup still works, not a separate mock.

## Analysis data flow

```
openFile(path)
  └─ FileSource::open        (mmap, read-only, size validation)
  └─ HexDocument             (row math, no full-file copy)
  └─ FileEditor              (hex view + analysis page)
  └─ cache lookup            (path + size + mtime)
  └─ QtConcurrent::run ─────▶ ParserRegistry::analyze
                                 ├─ probe() every parser; highest score wins
                                 ├─ try/catch around parse()
                                 ├─ RawFallbackParser when nothing matches
                                 └─ PackageInfo (sections, fields, warnings, partial flag)
  └─ QFutureWatcher<Result<PackageInfo>> ─▶ UI update + cache write
```

The UI thread never parses. A busy indicator appears while analysis runs; results for a closed
tab are discarded.

### Parser contract

```cpp
virtual int probe(const FileSource&) const = 0;               // 0..100
virtual Result<PackageInfo> parse(const FileSource&, const CancelToken&) const = 0;
```

Rules every parser follows:

- Probe only inspects a bounded prefix and never throws.
- All offsets are checked with `safe::inRange`/`safe::add` before reads.
- Counts from the file are capped (e.g. entries, sections, directory size) and reported as
  warnings instead of being trusted for allocations.
- Truncated but recognizable files return a `partial` result with warnings; unknown data
  returns an error, and the registry then falls back to raw-byte output.
- Parsers never decrypt, extract or modify content.

## Signature detection, inspector and replace

- `SignatureScanner` reads at most the first 4 MiB (plus a 64 KiB tail for the ZIP end record)
  and checks a fixed documented list. Each `SignatureHit` carries offset, length, category,
  confidence and a note describing the validation performed (`99` = structural validation,
  lower = magic-only or embedded match). Results are attached to `PackageInfo`, serialized into
  the analysis cache and rendered both as an analysis section and as hex-view underlines.
  The scanner never claims a format from an ambiguous or partial match.
- `ByteInspector` is a themed overlay owned by `FileEditor`, parented to the hex view. Hover,
  cursor and selection signals feed it; formatting lives in a static pure function so tests do
  not need a widget. It reads at most eight merged bytes through `HexDocument`.
- Replace runs only on offsets produced by a completed search and rejects a replacement whose
  length differs from the pattern, because the document model is still same-size. Applied bytes
  go through `HexDocument::setByte`, so they are journalled and undoable; the confirmation dialog
  states that the file on disk is untouched until Save.

## Error model

`Result<T>` holds either a value or a `Status` (`message` + optional `detail`). Expected
failures (bad signature, truncated header, cancelled search) are values, not exceptions.
Exceptions are still contained at the registry boundary so a third-party parser cannot take
down the process. Serious errors are logged and surfaced in the UI; they are never swallowed.

## Threading and memory

- Memory-mapped, read-only file access; no `readAll()` on arbitrary files.
- Hex view keeps a 256 KiB window plus a small overlap; repaints read only visible rows.
- Byte search streams 1 MiB chunks with pattern-length overlap, so matches across chunk
  boundaries are found without loading the file.
- Project search is bounded: 400 files, 2 MiB per file, 300 hits, binary files skipped, and it
  runs on a worker with a cancel token.
- Worker count is capped to keep the UI thread responsive on dual-core machines.
- Analysis results and recents are cached in SQLite; the cache is invalidated by size/mtime and
  can be cleared from Tools.
- Lightweight mode disables Qt menu/combo/tooltip animations and fades.

## Editing and save pipeline

`HexDocument` is the single source of truth for both display and saving:

- Pending edits live in an ordered `QMap<offset, value>` override layer on top of the read-only
  mapping. `byteAt`, `rowBytes` and `readMerged` apply it, so the hex view, selections, copies
  and analysis previews all show the same bytes.
- A journal of `{offset, before, after}` entries backs undo/redo. Consecutive edits append;
  undoing then editing truncates the redo branch. `isModified()` is true only while at least one
  override differs from the original byte, so undoing everything clears the dirty state.
- Edits are opt-in per file (`HexView::setEditingEnabled`) and are never written implicitly.
- `saveTo()` streams the file in 4 MiB chunks with overrides applied into a `QSaveFile`
  (temporary file in the destination directory + atomic rename). A failure cancels the write and
  leaves the original and the in-memory edits intact.
- The window layer performs the safety policy around `saveTo()`: overwrite confirmation,
  timestamped sibling backup via `atomic::backupFile`, recent-list update, and re-analysis of
  the saved bytes. `reloadFrom()` reopens the new file and clears the journal, so the saved
  state becomes the new baseline.
- Closing a modified tab or the window routes through Save / Discard / Cancel; cancelling aborts
  the close. File size cannot change: there is no insert/delete path yet.

## Persistence

SQLite schema v1 (`src/data/Database.cpp`):

| Table | Contents |
| --- | --- |
| `settings` | key/value preferences and shortcut overrides |
| `recent` | path, kind, last-used timestamp (deduplicated, pruned) |
| `session` | JSON state per session name (open project, files, active file) |
| `analysis_cache` | path, size, mtime, format label, JSON payload |

Reports and logs are written through `atomic::writeFile` (temporary file + rename), so an
interrupted export cannot corrupt an existing file. `atomic::backupFile` copies before writing.

## UI composition

`MainWindow` owns the layout, the command registry and all stateful workflows. Every menu item,
palette entry and shortcut is a `QAction` created by `addCommand(id, title, category, default
shortcut, handler)`; the palette and the Settings shortcut editor are generated from that same
registry, so there are no decorative or dead commands.

Sidebar panels are stacked widgets in a splitter; editors live in `EditorArea`; the output
panel is a tabbed log/diagnostics view. Themes are pure role→colour maps rendered once into a
QSS string, which is why switching themes is cheap and consistent.

## Extension points

- **New format**: implement `IFormatParser`, register it in `ParserRegistry` (plugin loading
  from shared libraries is planned, not implemented).
- **New theme**: JSON file in the themes directory, validated against required roles.
- **New command**: one `addCommand` call; palette, menu and shortcut customization follow.

See [PLUGIN_SDK.md](PLUGIN_SDK.md) for the planned isolation and permission model.
