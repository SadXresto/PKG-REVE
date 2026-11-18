# Building PKG Studio

## Requirements

| Component | Minimum | Notes |
| --- | --- | --- |
| CMake | 3.21 | 4.x works |
| C++ compiler | C++20 | GCC 11+, Clang 14+, MSVC 19.30+ |
| Qt | 6.5 | Core, Gui, Widgets, Concurrent, Sql + SQLite driver |
| Generator | Ninja or Makefiles | Ninja recommended |
| GoogleTest | fetched automatically | only when `PKGSTUDIO_BUILD_TESTS=ON` and no system GTest |

No Rust, Python, Capstone or LLDB is required at build time in this milestone.

## Linux (normal distribution)

```bash
# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build qt6-base-dev libqt6sql6-sqlite

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
./build/src/PKGStudio
```

Outputs: `build/src/PKGStudio` (dynamic, ~25 MB with debug info) and, when tests are enabled,
`build/tests/pkgstudio_tests`.

### Options

| Option | Default | Meaning |
| --- | --- | --- |
| `PKGSTUDIO_BUILD_TESTS` | `ON` | Build GoogleTest suite and register `ctest` |
| `PKGSTUDIO_ENABLE_SANITIZERS` | `OFF` | `-fsanitize=address,undefined` for GCC/Clang |
| `PKGSTUDIO_WARNINGS_AS_ERRORS` | `OFF` | Promote the strict warning set to errors |
| `PKGSTUDIO_EXTRA_LINK_DIRS` | empty | Extra link/rpath-link directories (list, `;`-separated) |

Warnings always enabled for GCC/Clang include `-Wall -Wextra -Wpedantic -Wshadow -Wconversion
-Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual
-Wnull-dereference -Wdouble-promotion`, with `QT_NO_CAST_FROM_ASCII` defined.

Example strict build:

```bash
cmake -S . -B build-strict -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DPKGSTUDIO_WARNINGS_AS_ERRORS=ON
cmake --build build-strict -j"$(nproc)"
```

## This workspace: self-contained toolchain

The development container has no system Qt, no CMake/Ninja and no root access. Two scripts make
the build reproducible without touching anything outside the repository:

```bash
scripts/setup-toolchain.sh     # once: CMake/Ninja/aqt in .venv-tools, Qt in .tooling/Qt,
                               #       needed system libraries extracted into .tooling/sysroot
source scripts/toolchain-env.sh
scripts/build.sh               # configure + build (tests ON by default)
```

`scripts/setup-toolchain.sh` performs:

1. `python3 -m venv .venv-tools` and installs `cmake`, `ninja`, `aqtinstall` via pip.
2. `aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O .tooling/Qt`.
3. Downloads Ubuntu jammy packages (fontconfig, freetype, xkbcommon, EGL/GL, expat, bz2, png,
   brotli, DejaVu fonts, `libgl-dev`, `libglx-dev`) and extracts them into `.tooling/sysroot`.
4. Creates unversioned `.so` symlinks in `.tooling/linklib` and a `fonts.conf` so headless
   rendering has a font directory and Qt can resolve its transitive libraries.

`scripts/toolchain-env.sh` exports `PATH`, `QT_ROOT`, `LD_LIBRARY_PATH`, `QT_PLUGIN_PATH`,
`QT_QPA_PLATFORM_PLUGIN_PATH` and `FONTCONFIG_FILE`, and selects the `offscreen` platform when
no display is present. It must be sourced, not executed.

Equivalent manual configure:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="$PWD/.tooling/Qt/6.8.3/gcc_64" \
  -DCMAKE_INCLUDE_PATH="$PWD/.tooling/sysroot/usr/include" \
  -DCMAKE_LIBRARY_PATH="$PWD/.tooling/linklib" \
  -DPKGSTUDIO_EXTRA_LINK_DIRS="$PWD/.tooling/linklib;$PWD/.tooling/sysroot/usr/lib/x86_64-linux-gnu"
cmake --build build -j6
```

`.tooling/` and `.venv-tools/` are git-ignored and must be recreated after a fresh clone.

## Windows 10/11 (primary target)

Not verified in this Linux environment; the steps below are the intended supported path.

1. Install Visual Studio 2022 (Desktop C++ workload), CMake and Qt 6.5+ with the
   `msvc2019_64`/`msvc2022_64` kit (including Qt SQL and the SQLite driver).
2. Configure with the Qt kit on `CMAKE_PREFIX_PATH`:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build --config RelWithDebInfo
```

3. Run `build\RelWithDebInfo\PKGStudio.exe`. For a portable folder, `windeployqt` can copy the
   required Qt DLLs and plugins next to the executable.

The code uses POSIX APIs only in guarded blocks (`mmap`, `open`, signal handling in
`FileSource`/`Logging`), with Windows equivalents; this has not been compiled with MSVC yet, so
treat the Windows build as untested until CI covers it.

## Tests and sanitizers

```bash
cmake -S . -B build -G Ninja -DPKGSTUDIO_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

# sanitizer build
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DPKGSTUDIO_BUILD_TESTS=ON -DPKGSTUDIO_ENABLE_SANITIZERS=ON
cmake --build build-asan -j
ASAN_OPTIONS=detect_leaks=0 ./build-asan/tests/pkgstudio_tests
```

See [TESTING.md](TESTING.md) for coverage and measured results.

## Headless verification

```bash
./build/src/PKGStudio --selftest -f samples/sample.elf -f samples/sample_ps4.pkg
# prints result: PASS and exits 0

./build/src/PKGStudio --screenshot out.png --screenshot-tab analysis -p samples \
    -f samples/sample_ps4.pkg
```

Both modes run the real startup path (database, settings, parsers, theme, widgets). On a
machine without a display, keep `QT_QPA_PLATFORM=offscreen` (set automatically by
`toolchain-env.sh`) or provide an X/Wayland session for real window rendering.

## Reproducibility notes

- No generated sources, no code generation, no vendored binary blobs.
- The only network access at build time is GoogleTest FetchContent when GTest is absent and the
  optional toolchain bootstrap (pip, Qt, Ubuntu packages).
- `CMAKE_BUILD_TYPE` defaults to `RelWithDebInfo`; use `Release` for distribution and `Debug`
  with sanitizers for verification.
- Builds are not bit-for-bit reproducible yet (timestamps in Qt resources and differing
  toolchains); this is a known gap.

## Packaging and installation

Two demo packaging paths exist:

- **Linux portable (verified in this environment).**
  `scripts/build.sh && scripts/package-portable.sh` creates
  `dist/PKGStudio-<version>-linux-x86_64/` and a `.tar.gz` containing the executable, the Qt
  libraries and plugins, the SQLite driver, fonts and the synthetic samples. The script then
  extracts the archive into a temporary folder and runs `--selftest` with `env -i` (fresh
  `HOME`, minimal `PATH`), so the reported artifact has no dependency on the Qt SDK, CMake or
  the repository. A desktop still provides the usual X11/glibc system libraries.
- **Windows installer (pipeline provided, not built or tested here).**
  `packaging/windows/build-installer.ps1` builds with MSVC, stages the executable, runs
  `windeployqt`, verifies the Qt DLLs and `qsqlite.dll`, and invokes Inno Setup with
  `packaging/windows/PKGStudio.iss`. This Linux environment has no MSVC, Windows Qt kit or
  Windows host, so no Windows EXE was produced or installed. Complete the checklist in
  `packaging/windows/README.md` on Windows before calling it tested.

No code signing, updater, macOS package or `cmake --install` target is provided. Development
builds are intentionally unsigned; Windows SmartScreen warnings are expected.

## Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| `Could not find Qt6Config.cmake` | Pass the Qt kit via `CMAKE_PREFIX_PATH` or source `toolchain-env.sh`. |
| Linker cannot find `libGL.so` / `libOpenGL.so` | Minimal images lack dev symlinks; use `PKGSTUDIO_EXTRA_LINK_DIRS` as shown. |
| `qt.qpa.plugin: could not load the Qt platform plugin "xcb"` | No X server. Use `offscreen` for tests or install the xcb platform dependencies. |
| Empty squares instead of glyphs, or no fonts headless | Point `FONTCONFIG_FILE` at a config with a real font directory. |
| Settings/shortcuts do not persist | Data directory not writable; check `Tools → Diagnostics` for the exact path. |
| Tests fail to fetch GoogleTest | Offline machine: install GTest system-wide so `find_package(GTest)` succeeds. |
