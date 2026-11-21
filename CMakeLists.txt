cmake_minimum_required(VERSION 3.21)

project(PKGStudio
    VERSION 0.7.3
    DESCRIPTION "Package analysis and reverse-engineering IDE"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global language / build defaults
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(PKGSTUDIO_BUILD_TESTS "Build the automated test suite" ON)
option(PKGSTUDIO_ENABLE_SANITIZERS "Build with AddressSanitizer + UBSan (GCC/Clang)" OFF)
option(PKGSTUDIO_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# ---------------------------------------------------------------------------
# Toolchain requirements
# ---------------------------------------------------------------------------
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Concurrent Sql Network)

qt_standard_project_setup()

# Qt 6 ships no dev symlinks for the system libraries it depends on in some
# minimal Linux images; PKGSTUDIO_EXTRA_LINK_DIRS lets a build environment
# point at an extracted sysroot. See BUILD.md.
set(PKGSTUDIO_EXTRA_LINK_DIRS "" CACHE STRING "Extra library search directories")

# Minimal Linux images can lack the unversioned development symlinks that Qt's
# imported targets expect. Extra directories listed here are used both for
# linking and for transitive dependency resolution (-rpath-link).
foreach(pkgstudio_dir IN LISTS PKGSTUDIO_EXTRA_LINK_DIRS)
    link_directories(${pkgstudio_dir})
    add_link_options("-Wl,-rpath-link,${pkgstudio_dir}")
endforeach()

add_library(pkgstudio_build_options INTERFACE)
target_compile_options(pkgstudio_build_options INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused
        -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion>)
if(PKGSTUDIO_WARNINGS_AS_ERRORS)
    target_compile_options(pkgstudio_build_options INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Werror>)
endif()
target_compile_definitions(pkgstudio_build_options INTERFACE
    QT_NO_CAST_FROM_ASCII
    QT_NO_URL_CAST_FROM_STRING)
if(PKGSTUDIO_ENABLE_SANITIZERS)
    target_compile_options(pkgstudio_build_options INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fsanitize=address,undefined -fno-omit-frame-pointer>)
    target_link_options(pkgstudio_build_options INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fsanitize=address,undefined>)
endif()

add_subdirectory(src)

if(PKGSTUDIO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
