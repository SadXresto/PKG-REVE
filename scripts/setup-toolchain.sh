#!/usr/bin/env bash
# Recreate the development toolchain inside the workspace.
#
# This container image has no root access and no apt package lists, so Qt and
# the few system libraries Qt needs are installed locally:
#   * CMake, Ninja and aqtinstall in a Python virtual environment
#   * Qt 6.8.3 (official binaries) under .tooling/Qt
#   * minimal runtime packages extracted under .tooling/sysroot
#
# Nothing outside the workspace is modified. On a normal Linux distribution use
# the packaged Qt 6 instead (see BUILD.md).
set -euo pipefail

PKGSTUDIO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PKGSTUDIO_ROOT"

TOOLING="$PKGSTUDIO_ROOT/.tooling"
QTVERSION="6.8.3"
DEB_DIR="$TOOLING/debs"
SYSROOT="$TOOLING/sysroot"
LINKLIB="$TOOLING/linklib"
mkdir -p "$TOOLING" "$DEB_DIR" "$SYSROOT" "$LINKLIB"

echo "== Python tooling =="
python3 -m venv .venv-tools
.venv-tools/bin/pip install --quiet --upgrade pip
.venv-tools/bin/pip install --quiet cmake ninja aqtinstall

echo "== Qt $QTVERSION =="
if [ ! -d "$TOOLING/Qt/$QTVERSION/gcc_64" ]; then
    .venv-tools/bin/aqt install-qt linux desktop "$QTVERSION" linux_gcc_64 -O "$TOOLING/Qt"
fi

echo "== System libraries extracted into the workspace =="
if [ ! -f "$TOOLING/pkgmap.txt" ]; then
    for component in main universe; do
        mkdir -p "$DEB_DIR/$component"
        curl -fsS -o "$DEB_DIR/$component/Packages.gz" \
            "http://archive.ubuntu.com/ubuntu/dists/jammy/$component/binary-amd64/Packages.gz"
        gunzip -f "$DEB_DIR/$component/Packages.gz"
    done
    for component in main universe; do
        awk '/^Package: /{p=$2} /^Filename: /{print p" "$2}' "$DEB_DIR/$component/Packages"
    done | sort -u > "$TOOLING/pkgmap.txt"
fi

PACKAGES=(
    libfontconfig1 libfreetype6 libxkbcommon0 libegl1 libglvnd0 libglx0
    libexpat1 libbz2-1.0 libpng16-16 libbrotli1 fonts-dejavu-core
    libopengl0 libgl-dev libglx-dev
)

for name in "${PACKAGES[@]}"; do
    filename="$(grep -m1 "^$name " "$TOOLING/pkgmap.txt" | cut -d' ' -f2 || true)"
    if [ -z "$filename" ]; then
        echo "warning: $name not found in the package index" >&2
        continue
    fi
    base="$(basename "$filename")"
    if [ ! -f "$DEB_DIR/$base" ]; then
        curl -fsS -o "$DEB_DIR/$base" "http://archive.ubuntu.com/ubuntu/$filename"
    fi
    dpkg-deb -x "$DEB_DIR/$base" "$SYSROOT"
done

# Unversioned link names for the linker and rpath-link for transitive lookups.
for library in "$SYSROOT"/usr/lib/x86_64-linux-gnu/lib*.so.*; do
    base="$(basename "$library")"
    ln -sf "$library" "$LINKLIB/${base%%.so*}.so"
done
ln -sf /lib/x86_64-linux-gnu/libGL.so.1 "$LINKLIB/libGL.so"
ln -sf "$SYSROOT/usr/lib/x86_64-linux-gnu/libOpenGL.so.0" "$LINKLIB/libOpenGL.so"
ln -sf "$SYSROOT/usr/lib/x86_64-linux-gnu/libGLX.so.0" "$LINKLIB/libGLX.so"

cat > "$TOOLING/fonts.conf" <<EOF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>$SYSROOT/usr/share/fonts</dir>
  <cachedir>$TOOLING/fontcache</cachedir>
</fontconfig>
EOF

echo "Toolchain ready. Use: source scripts/toolchain-env.sh && scripts/build.sh"
