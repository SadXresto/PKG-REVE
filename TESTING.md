# Testing

## How to run

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPKGSTUDIO_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Or run the binary directly for GoogleTest filters and detailed output:

```bash
./build/tests/pkgstudio_tests
./build/tests/pkgstudio_tests --gtest_filter='FuzzTest.*'
```

The suite uses a `QApplication` on the headless `offscreen` platform and isolates its data with
`PKGSTUDIO_DATA_DIR=.testdata/<name>`; it never touches the user's real settings or database.

## About and Feature status dialogs

The two dialogs that explain the build can be captured without clicking through the UI:

```bash
QT_QPA_PLATFORM=offscreen ./build/src/PKGStudio \
    --screenshot artifacts/dialog-about.png --screenshot-dialog about
QT_QPA_PLATFORM=offscreen ./build/src/PKGStudio \
    --screenshot artifacts/dialog-feature-status.png --screenshot-dialog feature-status
```

The command exits 0 only when the dialog became visible and rendered a non-blank image, and
writes the PNG passed to `--screenshot`; exit code 2 means blank, invisible, unknown dialog id
or a failed write. `tests/test_dialogs.cpp` asserts the same contents (version, development-build
label, data directory, the three feature-status sections) and renders both dialogs through the
same helper. The Windows smoke test repeats the capture against the installed EXE.

Sanitizer build:

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DPKGSTUDIO_ENABLE_SANITIZERS=ON -DPKGSTUDIO_BUILD_TESTS=ON
cmake --build build-asan -j
ASAN_OPTIONS=detect_leaks=0 ./build-asan/tests/pkgstudio_tests
```

Leak detection is disabled because Qt's platform plugin keeps intentional process-lifetime
allocations; address and undefined-behaviour checking remain enabled.

## Current results

Measured on the development machine (Linux x64, headless `offscreen` platform, Qt 6.8.3,
RelWithDebInfo unless stated):

| Check | Result |
| --- | --- |
| `ctest` / direct run | **100 tests in 30 suites, all passed**, ~1.1 s |
| ASan + UBSan run (Debug) | **100/100 passed**, no reports, ~1.8 s |
| `--screenshot-dialog about` / `feature-status` | captured **458×280** and **680×560** PNGs, exit code 0; an unknown dialog id exits with code 2 and writes nothing |
| `--selftest` with 6 fixtures | PASS, 6 files analysed, exit code 0, RSS ≈ 44.6 MiB |
| 8 GiB sparse file: mmap, tail read, row count, search | passed, elapsed below the 1 ms timer resolution, RSS growth ≈ 816 KiB |
| 3000 random row reads on a 64 MiB file | passed in ~40 ms (test wall time) |

These numbers are for this environment only. No low-end Windows 10 hardware, no real console
package and no Windows build have been measured yet.

## Packaging verification

| Artifact | Status |
| --- | --- |
| Linux portable folder + `.tar.gz` (`scripts/package-portable.sh`) | **verified**: extracted into a temporary folder and run with `env -i` (fresh `HOME`, system `PATH` only) using `--selftest`, plus offscreen screenshots of the Welcome and structure views. Qt, ICU, fontconfig, fonts, the SQLite plugin and the samples are bundled. |
| Windows Inno Setup EXE (`packaging/windows/`) | **not built or tested**: this environment is Linux with no MSVC, Windows Qt kit or Windows host. A CI workflow (`.github/workflows/windows-installer.yml`) builds it on `windows-2022` and runs `packaging/windows/smoke-test.ps1`, but that workflow has not been executed from here. Do not publish the EXE as tested until a CI run is green and the checklist on a clean Windows VM is recorded. |

## What the suite covers

| Area | File | Examples |
| --- | --- | --- |
| Primitives | `test_core.cpp` | overflow checks, offset parsing, endian reads, overrun rejection, `Result`/JSON round-trip |
| File access | `test_file_source.cpp` | bounds, empty/missing files, 4 GiB sparse mapping |
| Parsers | `test_parsers.cpp`, `test_pkg_parsers.cpp` | ELF/PE/ZIP/APK/Vita/PS4 fields, truncation, invalid class, zip truncation, size mismatch, implausible counts |
| Hex + search | `test_hex_search.cpp` | row layout, chunk-boundary matches, case-insensitive search, hit limit, cancellation, empty pattern |
| Byte editing | `test_editing.cpp` | merged reads, undo/redo, redo-branch discard, no-op/out-of-range rejection, save content and size, 4 MiB chunk-boundary save, reload/reset, invalid destinations |
| Replace | `test_replace.cpp` | equal-length replacement at search offsets, undo, length mismatch, empty pattern, out-of-range offsets |
| Async save | `test_save.cpp` | pre-cancelled save creates nothing, mid-stream cancel leaves destination and temp files absent, monotonic progress to 100%, cancelled backup leaves source intact, backup byte-for-byte copy |
| Structure templates | `test_structure.cpp` | nested struct arrays, per-field endianness, out-of-bounds error nodes, invalid definitions, built-ins against real ELF/ZIP samples, JSON round-trip, shipped template files match built-ins, broken template files reported |
| Structure panel | `test_command.cpp` | tree population, tree selection offset/length signal, hex-to-tree highlight without feedback, visible validation diagnostics |
| Structure form | `test_structure_form.cpp` | template loads into the form unchanged, nested add/replace/remove/move, missing name, missing length, duplicate sibling names, empty template |
| Welcome page | `test_welcome.cpp` | development-build label, sample actions disabled when absent and enabled when installed, sample signals, empty recent-list explanation |
| Build dialogs | `test_dialogs.cpp` | About version/build/data locations, sample path and "not installed" fallback, Feature status sections, unknown topic rejected, both dialogs render to non-blank PNGs, missing dialog/empty path rejected |
| Command center | `test_command.cpp` | title/category/description/keyword matching, multi-term queries, stable ordering, no-match handling, empty query preserves order |
| Signatures | `test_signatures.cpp` | ELF/ZIP/PS4/Vita/PNG validation, embedded-match confidence, no false positives, MZ-only low confidence, analysis JSON + section integration |
| Inspector data | `test_inspector.cpp` | endianness, signed values, binary/hex/ASCII formatting, float interpretation, empty input |
| Theme system | `test_data.cpp` | Void Black exact tokens and default fallback, 7:1/4.5:1 contrast checks, built-in theme completeness |
| Editing widgets | `test_ui_editing.cpp` | real key events through `HexView`: editing is off by default, two-digit entry advances the cursor, disabling editing blocks input |
| Data layer | `test_data.cpp` | schema, settings, recents ordering, session, cache invalidation, settings clamps, atomic write/backup, logger filtering/redaction, theme validation and QSS substitution, user theme export |
| Fuzz-style | `test_fuzz.cpp` | 1500 random buffers, every truncation of every fixture, 300 random PKG headers with huge counts, 200 malformed ZIP directories |
| Performance bounds | `test_performance.cpp` | 8 GiB sparse file stays streamed, constant-time row access |

The upgrade added widget-level inspector/signature wiring, a same-length replace path and
cancellable background saving. All three are covered at the data level. Dialog-driven replace
confirmation and the save progress dialog's buttons are not automated; the nested event loop
around the dialog was reviewed but not clicked through by a test.

The fuzz tests are deterministic (fixed xorshift seed), so failures are reproducible. They are
not a substitute for coverage-guided fuzzing; see below.

## Regression coverage of fixed bugs

Each of these was found during development and has a test now:

- QSS role substitution corrupting values such as `#1e1e1eText` → `ThemeTest.StyleSheetSubstitutionIsComplete`.
- Duplicate/absent parser registration → registry assertions in parser tests and `--selftest`.
- Uninitialized test fixtures and out-of-bounds `span.first(64)` in the raw fallback (found by
  ASan) → fuzz-style random-buffer and truncation tests.
- Null `QString` binding violating the settings NOT NULL constraint → settings round-trip tests.

## Known gaps

- No general UI automation: widgets are exercised by `--selftest`, screenshots and one focused
  key-event test. Modal save/overwrite/close dialogs and focus/selection behaviour are not
  driven by automated tests; their logic is covered at the document level.
- No libFuzzer/AFL harness. The recommended next step is a small `LLVMFuzzerTestOneInput` wrapper
  calling `ParserRegistry::analyze` on a temp file, with the fixtures as seed corpus.
- The Windows CI workflow and installed-application smoke script exist but have not been run in
  this Linux environment; no installer has been produced or installed-tested here yet.
- No dependency-audit automation in this repository.
- Large-file tests use sparse files; they prove layout/streaming behaviour, not sustained I/O on
  a fragmented HDD.
- No memory-consumption cap test beyond RSS growth assertions.
- Saving is synchronous: the write is bounded in memory (4 MiB chunks) and atomic, but saving a
  very large edited file keeps the window busy for its duration. Background saving with progress
  and cancellation is not implemented.

## Test corpus

`tools/make_test_packages.py` generates deterministic synthetic fixtures into `samples/`:
ELF64, PE32+, ZIP-based APK, Vita PKG, PS4 PKG and an unrecognized blob. Every fixture is
created from code in this repository; no third-party or commercial files are included.
