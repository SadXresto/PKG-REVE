#!/usr/bin/env bash
# Configure and build PKG Studio in the current workspace.
#
#   scripts/build.sh                # RelWithDebInfo, tests enabled
#   scripts/build.sh Debug
#   PKGSTUDIO_BUILD_TESTS=OFF scripts/build.sh
set -euo pipefail

PKGSTUDIO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PKGSTUDIO_ROOT"
# shellcheck source=scripts/toolchain-env.sh
source scripts/toolchain-env.sh

BUILD_TYPE="${1:-RelWithDebInfo}"
BUILD_DIR="${PKGSTUDIO_BUILD_DIR:-build}"
TESTS="${PKGSTUDIO_BUILD_TESTS:-ON}"

cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_PREFIX_PATH="$QT_ROOT" \
    -DCMAKE_INCLUDE_PATH="$SYSROOT/usr/include" \
    -DCMAKE_LIBRARY_PATH="$LINKLIB" \
    -DPKGSTUDIO_EXTRA_LINK_DIRS="$LINKLIB;$SYSROOT/usr/lib/x86_64-linux-gnu" \
    -DPKGSTUDIO_BUILD_TESTS="$TESTS"

cmake --build "$BUILD_DIR" -j "$(nproc)"
