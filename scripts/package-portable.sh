#!/usr/bin/env bash
# Build a self-contained, portable Linux demo folder and tar.gz.
#
#   scripts/build.sh && scripts/package-portable.sh
#
# The bundle contains the application, the required Qt libraries and plugins,
# the SQLite driver, fonts and the synthetic samples. It does not require the
# Qt SDK or this repository at runtime. The script finishes by extracting the
# archive into a temporary folder and running --selftest with an empty
# environment, so a broken bundle fails here instead of on a demo machine.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
# shellcheck source=scripts/toolchain-env.sh
source scripts/toolchain-env.sh

VERSION="$(sed -n 's/^    VERSION \([0-9][0-9.]*\)$/\1/p' CMakeLists.txt | head -1)"
BUILD_DIR="${PKGSTUDIO_BUILD_DIR:-build}"
BINARY="$BUILD_DIR/src/PKGStudio"
NAME="PKGStudio-$VERSION-linux-x86_64"
OUT="$ROOT/dist/$NAME"

if [ -z "$VERSION" ]; then
    echo "error: could not read the project version from CMakeLists.txt" >&2
    exit 1
fi
if [ ! -x "$BINARY" ]; then
    echo "error: $BINARY is missing. Run scripts/build.sh first." >&2
    exit 1
fi
if [ -z "${QT_ROOT:-}" ] || [ ! -d "$QT_ROOT/lib" ]; then
    echo "error: QT_ROOT is not usable; run scripts/setup-toolchain.sh" >&2
    exit 1
fi

echo "== staging $NAME =="
rm -rf "$OUT"
mkdir -p "$OUT"/bin "$OUT"/lib "$OUT"/share/fonts
mkdir -p "$OUT"/plugins/platforms "$OUT"/plugins/sqldrivers \
    "$OUT"/plugins/imageformats "$OUT"/plugins/styles "$OUT"/plugins/iconengines

cp "$BINARY" "$OUT/bin/PKGStudio"
strip --strip-unneeded "$OUT/bin/PKGStudio" 2>/dev/null || true

# Only the plugins this application needs: the offscreen and X11 platforms,
# the SQLite driver, and the image/style/icon plugin folders Qt expects.
cp "$QT_ROOT/plugins/platforms/libqoffscreen.so" "$OUT/plugins/platforms/"
cp "$QT_ROOT/plugins/platforms/libqminimal.so" "$OUT/plugins/platforms/"
if [ -f "$QT_ROOT/plugins/platforms/libqxcb.so" ]; then
    cp "$QT_ROOT/plugins/platforms/libqxcb.so" "$OUT/plugins/platforms/"
fi
cp "$QT_ROOT/plugins/sqldrivers/libqsqlite.so" "$OUT/plugins/sqldrivers/"
for plugin_dir in imageformats styles iconengines; do
    if [ -d "$QT_ROOT/plugins/$plugin_dir" ]; then
        cp -a "$QT_ROOT/plugins/$plugin_dir/." "$OUT/plugins/$plugin_dir/"
    fi
done

cp -a samples "$OUT/samples"
cp -a templates "$OUT/templates"
cp README.md "$OUT/README.md"
cp CHANGELOG.md "$OUT/CHANGELOG.md"

FONT_DIR="$SYSROOT/usr/share/fonts/truetype/dejavu"
if [ -d "$FONT_DIR" ]; then
    cp "$FONT_DIR"/*.ttf "$OUT/share/fonts/"
else
    echo "warning: no bundled DejaVu fonts; the demo relies on system fonts" >&2
fi

cat > "$OUT/run-pkgstudio.sh" <<'LAUNCHER'
#!/usr/bin/env bash
# Portable launcher: keeps every library, plugin and sample inside this folder.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/plugins/platforms"
export PKGSTUDIO_SAMPLES_DIR="$HERE/samples"

CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/pkgstudio-demo"
mkdir -p "$CACHE/fontcache"
cat > "$CACHE/fonts.conf" <<EOF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>$HERE/share/fonts</dir>
  <cachedir>$CACHE/fontcache</cachedir>
</fontconfig>
EOF
export FONTCONFIG_FILE="$CACHE/fonts.conf"

if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
fi

exec "$HERE/bin/PKGStudio" "$@"
LAUNCHER
chmod +x "$OUT/run-pkgstudio.sh"

cat > "$OUT/README-DEMO.txt" <<TEXT
PKG Studio $VERSION — portable Linux demo build
================================================

Run:    ./run-pkgstudio.sh
Samples: ./samples (also available from Help → Open sample workspace)
Data:    \$HOME/.local/share/PKGStudio (settings, log, cache)

This folder is self-contained: Qt, the SQLite driver, fonts and the sample
files are included. The build is a development/demo build, not a signed
release. Help → Feature status lists implemented, partial and planned features.

Diagnostics: ./run-pkgstudio.sh --selftest -f samples/sample.elf
TEXT

echo "== copying required shared libraries =="
COPIED=" "
QUEUE=("$OUT/bin/PKGStudio")
while IFS= read -r -d '' plugin; do
    QUEUE+=("$plugin")
done < <(find "$OUT/plugins" -name '*.so' -print0)

index=0
while [ "$index" -lt "${#QUEUE[@]}" ]; do
    file="${QUEUE[$index]}"
    index=$((index + 1))
    while IFS= read -r library; do
        [ -n "$library" ] || continue
        case "$library" in
            "$QT_ROOT"/*|"$SYSROOT"/*) ;;
            *) continue ;;
        esac
        base="$(basename "$library")"
        case "$COPIED" in
            *" $base "*) continue ;;
        esac
        cp -L "$library" "$OUT/lib/$base"
        COPIED="$COPIED$base "
        QUEUE+=("$library")
    done < <(ldd "$file" 2>/dev/null | awk '$2 == "=>" && $3 ~ /^\// { print $3 } $1 ~ /^\// { print $1 }')
done

echo "== creating archive =="
mkdir -p "$ROOT/dist"
tar -C "$ROOT/dist" -czf "$ROOT/dist/$NAME.tar.gz" "$NAME"
(cd "$ROOT/dist" && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256")

echo "== verifying the extracted bundle without the development environment =="
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT
tar -xzf "$ROOT/dist/$NAME.tar.gz" -C "$TEMP_DIR"
mkdir -p "$TEMP_DIR/home"
if ! env -i HOME="$TEMP_DIR/home" PATH=/usr/bin:/bin \
        "$TEMP_DIR/$NAME/run-pkgstudio.sh" --selftest -f "$TEMP_DIR/$NAME/samples/sample.elf" \
        > "$TEMP_DIR/selftest.log" 2>&1; then
    echo "error: the portable bundle failed its isolated self-test" >&2
    cat "$TEMP_DIR/selftest.log" >&2
    exit 1
fi
grep -E "result: PASS|version:|open files:" "$TEMP_DIR/selftest.log"

echo
echo "Portable demo: $OUT"
echo "Archive:        $ROOT/dist/$NAME.tar.gz"
cat "$ROOT/dist/$NAME.tar.gz.sha256"
