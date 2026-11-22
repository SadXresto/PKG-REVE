# Synthetic demo project for the GameForge workspace. It is written from
# scratch for offline demonstrations and contains no commercial code or assets.
# It is not buildable here: SDL2 and OpenGL are not bundled with PKG Studio.
cmake_minimum_required(VERSION 3.21)
project(NeonDrift VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_compile_definitions(NEONDRIFT_WINDOWS=1)
elseif(APPLE)
    add_compile_definitions(NEONDRIFT_MACOS=1)
else()
    add_compile_definitions(NEONDRIFT_LINUX=1)
endif()

find_package(SDL2 2.28 REQUIRED)
find_package(OpenGL REQUIRED)

add_library(neondrift_core STATIC
    src/renderer.cpp
    src/audio.cpp
)
target_include_directories(neondrift_core PUBLIC src)
target_link_libraries(neondrift_core PUBLIC SDL2::SDL2 OpenGL::GL)

if(WIN32)
    target_sources(neondrift_core PRIVATE src/platform_win32.cpp)
    target_link_libraries(neondrift_core PRIVATE d3d11 xinput)
elseif(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(neondrift_core PRIVATE Threads::Threads pthread)
endif()

add_executable(neondrift src/main.cpp)
target_link_libraries(neondrift PRIVATE neondrift_core)
