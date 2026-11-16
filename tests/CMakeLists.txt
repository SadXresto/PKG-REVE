include(FetchContent)

find_package(GTest QUIET)
if(NOT GTest_FOUND)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(pkgstudio_tests
    test_main.cpp
    test_core.cpp
    test_file_source.cpp
    test_parsers.cpp
    test_pkg_parsers.cpp
    test_signatures.cpp
    test_inspector.cpp
    test_hex_search.cpp
    test_replace.cpp
    test_editing.cpp
    test_ui_editing.cpp
    test_data.cpp
    test_fuzz.cpp
    test_performance.cpp
)

target_include_directories(pkgstudio_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pkgstudio_tests PRIVATE
    pkgstudio_app
    GTest::gtest
)

add_test(NAME pkgstudio_tests COMMAND pkgstudio_tests)
set_tests_properties(pkgstudio_tests PROPERTIES TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Sanitizer builds are opt-in: -DPKGSTUDIO_ENABLE_SANITIZERS=ON
