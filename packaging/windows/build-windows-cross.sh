#!/usr/bin/env bash
# Cross-build the PKG Studio Windows x64 demo release from Linux.
#
# Produces, in dist-win/:
#   PKGStudio-<version>-win64/            portable folder ready to copy to Windows
#   PKGStudio-<version>-win64.zip         portable folder as a ZIP
#   PKGStudio-<version>-Setup-x64.exe     per-user NSIS installer
#   SHA256SUMS.txt                        SHA-256 of the ZIP and the installer
#
# Requirements (set up once, see packaging/windows/README.md):
#   .tooling/winroot        MinGW-w64 x86_64 cross compiler + makensis, extracted
#                           from Ubuntu packages with dpkg-deb -x (no root needed)
#   .tooling/Qt-win/6.8.3/mingw_64   Qt for Windows (aqt install-qt windows
#                           desktop 6.8.3 win64_mingw --archives qtbase)
#   .tooling/Qt/6.8.3/gcc_64         Qt for Linux, same version, used as the
#                           host-tools set for moc/uic/rcc (QT_HOST_PATH)
#
# Limitations, stated up front: this build is NOT code-signed, it is compiled
# with MinGW-w64 (not MSVC), and it is NOT run on Windows by this script. The
# GitHub Actions workflow in .github/workflows/windows-release.yml is still the
# path to a smoke-tested installer.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

QT_WIN="${QT_WIN:-$ROOT/.tooling/Qt-win/6.8.3/mingw_64}"
QT_HOST="${QT_HOST:-$ROOT/.tooling/Qt/6.8.3/gcc_64}"
MINGW_ROOT="${MINGW_ROOT:-$ROOT/.tooling/winroot}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-win}"
DIST="${DIST:-$ROOT/dist-win}"

VERSION="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\+\([0-9]\+\.[0-9]\+\.[0-9]\+\).*/\1/p' CMakeLists.txt | head -1)"
[ -n "$VERSION" ] || { echo "error: cannot read version from CMakeLists.txt" >&2; exit 1; }

for path in "$QT_WIN/bin/Qt6Core.dll" "$QT_HOST/bin/qmake" "$MINGW_ROOT/usr/bin/x86_64-w64-mingw32-g++-posix" "$MINGW_ROOT/usr/bin/makensis"; do
    [ -e "$path" ] || { echo "error: missing $path (see packaging/windows/README.md)" >&2; exit 1; }
done

# windres shells out to a preprocessor called "x86_64-w64-mingw32-gcc", but
# this sysroot only ships the -posix suffixed drivers. Give it the name it
# expects so compiling the .rc icon resource works.
if [ ! -e "$MINGW_ROOT/usr/bin/x86_64-w64-mingw32-gcc" ]; then
    ln -sf x86_64-w64-mingw32-gcc-posix "$MINGW_ROOT/usr/bin/x86_64-w64-mingw32-gcc"
fi

export PATH="$MINGW_ROOT/usr/bin:$ROOT/.venv-tools/bin:$PATH"
export GCC_EXEC_PREFIX="$MINGW_ROOT/usr/lib/gcc/"
# makensis is extracted from a .deb, so it needs pointing at its own data files.
export NSISDIR="${NSISDIR:-$MINGW_ROOT/usr/share/nsis}"

# samples/ only carries README.md in a fresh checkout; the demo packages are
# generated so the installed app always has something to open.
if [ ! -f samples/sample.elf ]; then
    echo "== generating synthetic demo samples"
    python3 tools/make_test_packages.py samples
fi

echo "== configuring ($VERSION, Windows x64, MinGW-w64)"
cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/.tooling/mingw-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPKGSTUDIO_BUILD_TESTS=OFF \
    -DCMAKE_PREFIX_PATH="$QT_WIN" \
    -DQT_HOST_PATH="$QT_HOST"

echo "== building"
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

STAGE="$DIST/PKGStudio-$VERSION-win64"
rm -rf "$STAGE"
mkdir -p "$STAGE/platforms" "$STAGE/sqldrivers" "$STAGE/styles" "$STAGE/tls" \
    "$STAGE/imageformats"

echo "== staging application"
cp "$BUILD_DIR/PKGStudio.exe" "$STAGE/"
cp -r samples templates "$STAGE/"
cp README.md CHANGELOG.md LICENSE TERMS.md PRIVACY.md "$STAGE/"

echo "== staging Qt runtime and plugins"
for dll in Qt6Core Qt6Gui Qt6Widgets Qt6Sql Qt6Network; do
    cp "$QT_WIN/bin/$dll.dll" "$STAGE/"
done
# MinGW runtime: take it from the compiler that produced the binary so the
# libstdc++/libgcc symbols the executable requires are always present.
cp "$MINGW_ROOT/usr/lib/gcc/x86_64-w64-mingw32/13-posix/libstdc++-6.dll" "$STAGE/"
cp "$MINGW_ROOT/usr/lib/gcc/x86_64-w64-mingw32/13-posix/libgcc_s_seh-1.dll" "$STAGE/"
cp "$MINGW_ROOT/usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll" "$STAGE/"

cp "$QT_WIN/plugins/platforms/qwindows.dll" "$STAGE/platforms/"
cp "$QT_WIN/plugins/platforms/qoffscreen.dll" "$STAGE/platforms/"
cp "$QT_WIN/plugins/platforms/qminimal.dll" "$STAGE/platforms/"
cp "$QT_WIN/plugins/sqldrivers/qsqlite.dll" "$STAGE/sqldrivers/"
cp "$QT_WIN/plugins/styles/qmodernwindowsstyle.dll" "$STAGE/styles/"
# TLS backends for the optional AI endpoint (HTTPS) and image formats for theme
# wallpapers; without them the assistant and JPEG/WebP backgrounds fail at runtime.
cp "$QT_WIN/plugins/tls/"*.dll "$STAGE/tls/"
cp "$QT_WIN/plugins/imageformats/"*.dll "$STAGE/imageformats/"

echo "== verifying runtime is complete"
missing=0
for required in \
    PKGStudio.exe \
    Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Sql.dll Qt6Network.dll \
    libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll \
    platforms/qwindows.dll sqldrivers/qsqlite.dll tls/qschannelbackend.dll \
    imageformats/qjpeg.dll samples/sample.elf; do
    if [ ! -e "$STAGE/$required" ]; then
        echo "   MISSING: $required"
        missing=1
    fi
done
[ "$missing" -eq 0 ] || { echo "error: staged folder is incomplete" >&2; exit 1; }
# The executable must not import a DLL that is absent from the folder; anything
# it does import is either bundled above or a Windows system DLL.
echo "   imports:"
x86_64-w64-mingw32-objdump -p "$STAGE/PKGStudio.exe" | sed -n 's/^\s*DLL Name: /\t/p' | sort -u

cat > "$STAGE/WINDOWS-README.txt" <<EOF
PKG Studio $VERSION - portable Windows x64 demo build

Run PKGStudio.exe. Demo samples are in the samples folder (File > Open, or the
Welcome page). No installer is needed for this folder: copy it anywhere.

This build is unsigned, so Windows SmartScreen may warn on first launch. It is a
development demo, not a signed release: Help > Feature status lists what is
implemented, partial and planned.

Installed with the setup program instead? Use Start menu > PKG Studio, and
Settings > Apps > PKG Studio to uninstall.
EOF

echo "== creating portable ZIP"
ZIP="$DIST/PKGStudio-$VERSION-win64.zip"
rm -f "$ZIP"
python3 - "$STAGE" "$ZIP" <<'PY'
import os, sys, zipfile
stage, out = sys.argv[1], sys.argv[2]
root = os.path.dirname(stage)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for base, _dirs, files in os.walk(stage):
        for name in sorted(files):
            full = os.path.join(base, name)
            z.write(full, os.path.relpath(full, root))
print("   ", out, os.path.getsize(out), "bytes")
PY

echo "== compiling NSIS installer"
SETUP="$DIST/PKGStudio-$VERSION-Setup-x64.exe"
rm -f "$SETUP"
makensis -V2 \
    -DAppVersion="$VERSION" \
    "-DMUI_ICON=$ROOT/packaging/windows/pkgstudio.ico" \
    "-DStageDir=$STAGE" \
    "-DOutFile=$SETUP" \
    packaging/windows/PKGStudio.nsi

echo "== writing checksums"
( cd "$DIST" && sha256sum "PKGStudio-$VERSION-win64.zip" "PKGStudio-$VERSION-Setup-x64.exe" > SHA256SUMS.txt && cat SHA256SUMS.txt )

echo
echo "Artifacts in $DIST:"
ls -lh "$DIST"
