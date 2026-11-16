#!/usr/bin/env bash
# Source this file to point Qt and the build at the toolchain that lives inside
# this workspace. It is intentionally explicit so that no system directory is
# modified and nothing outside the project is required.
#
#   source scripts/toolchain-env.sh
#
# The directories are created by scripts/setup-toolchain.sh.

PKGSTUDIO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PKGSTUDIO_ROOT

export PATH="$PKGSTUDIO_ROOT/.venv-tools/bin:$PATH"

export QT_ROOT="$PKGSTUDIO_ROOT/.tooling/Qt/6.8.3/gcc_64"
export SYSROOT="$PKGSTUDIO_ROOT/.tooling/sysroot"
export LINKLIB="$PKGSTUDIO_ROOT/.tooling/linklib"

# Qt runtime
export LD_LIBRARY_PATH="$QT_ROOT/lib:$SYSROOT/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$QT_ROOT/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$QT_ROOT/plugins/platforms"

# Fonts extracted into the local sysroot (headless rendering needs a font dir)
export FONTCONFIG_FILE="$PKGSTUDIO_ROOT/.tooling/fonts.conf"

# Headless by default in CI/containers; override to xcb on a real desktop.
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
fi
