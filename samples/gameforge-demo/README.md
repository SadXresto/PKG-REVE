# NeonDrift — GameForge demo project

A small, synthetic CMake game project written for this repository. It exists so the
GameForge workspace can be shown and tested offline: nothing here is copied from a
commercial game, and it is **not buildable as-is** because SDL2 and OpenGL are not
bundled with PKG Studio.

Open it with `PKGStudio -p samples/gameforge-demo` (or **Ctrl+Shift+G** → *Open
project…*) and the GameForge panel scans it in the background.

## What it deliberately contains

| Element | Where | Why |
| --- | --- | --- |
| `project()` + two targets | `CMakeLists.txt` | project→target edges, a library and an executable |
| `find_package(SDL2 2.28 REQUIRED)`, `find_package(OpenGL)` | `CMakeLists.txt` | declared dependencies with a version |
| `SDL2::SDL2`, `OpenGL::GL` | `CMakeLists.txt` | namespaced links that resolve to those declarations (solid edges) |
| `d3d11`, `xinput`, `pthread` | `CMakeLists.txt` | link names with no declaration: drawn as inferred/unresolved |
| `if(WIN32)`, `elseif(UNIX AND NOT APPLE)` | `CMakeLists.txt` | platform conditions are reported, never evaluated |
| `-m64`, `/arch:AVX2`, `/O2` | `CMakePresets.json` | architecture and compiler settings |
| Win32, Direct3D 11, XInput | `src/platform_win32.cpp` | Windows API evidence for the portability and porting-intel tabs |
| OpenGL, SDL2 | `src/renderer.cpp`, `src/main.cpp` | graphics/input evidence on the Linux side |

The demo is proof of what the parsers read, not a statement that NeonDrift can be
built or ported. Every finding in the UI keeps its `file:line` so you can check it.
