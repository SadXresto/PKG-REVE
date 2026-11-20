# ---------------------------------------------------------------------------
# pkgstudio_core - binary analysis, file mapping, logging. No widgets.
# ---------------------------------------------------------------------------
add_library(pkgstudio_core STATIC
    core/AtomicFile.cpp
    core/ByteReader.cpp
    core/ByteSearch.cpp
    core/CodeListing.cpp
    core/FileSource.cpp
    core/FormatInfo.cpp
    core/FormatUtil.cpp
    core/HexDocument.cpp
    core/Logging.cpp
    core/ParserRegistry.cpp
    core/ProcessInfo.cpp
    core/Signature.cpp
    core/SignatureScanner.cpp
    core/Structure.cpp
    core/parsers/ElfParser.cpp
    core/parsers/ParserSupport.cpp
    core/parsers/PeParser.cpp
    core/parsers/PkgParsers.cpp
    core/parsers/ZipParser.cpp
)
target_include_directories(pkgstudio_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pkgstudio_core
    PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent pkgstudio_build_options)

# ---------------------------------------------------------------------------
# pkgstudio_app - data layer, UI and application wiring.
# ---------------------------------------------------------------------------
add_library(pkgstudio_app STATIC
    ai/AiClient.cpp
    app/Application.cpp
    data/Database.cpp
    data/Settings.cpp
    data/Theme.cpp
    data/ThemeManager.cpp
    ui/AboutDialog.cpp
    ui/ActivityBar.cpp
    ui/AiAssistantDialog.cpp
    ui/ByteInspector.cpp
    ui/CommandPalette.cpp
    ui/DialogCapture.cpp
    ui/EditorArea.cpp
    ui/FileEditor.cpp
    ui/FileExplorer.cpp
    ui/HelpDialog.cpp
    ui/HexView.cpp
    ui/Icons.cpp
    ui/MainWindow.cpp
    ui/OutputPanel.cpp
    ui/PackageInfoView.cpp
    ui/RecentPanel.cpp
    ui/SearchPanel.cpp
    ui/SettingsDialog.cpp
    ui/StructureFormEditor.cpp
    ui/StructurePanel.cpp
    ui/ThemeEditorDialog.cpp
    ui/WelcomePage.cpp
)
target_link_libraries(pkgstudio_app
    PUBLIC pkgstudio_core Qt6::Widgets Qt6::Sql Qt6::Network)
target_compile_definitions(pkgstudio_app PRIVATE
    PKGSTUDIO_BUILD_TYPE="${CMAKE_BUILD_TYPE}")

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
qt_add_executable(PKGStudio main.cpp)
target_link_libraries(PKGStudio PRIVATE pkgstudio_app)
target_compile_definitions(PKGStudio PRIVATE
    PKGSTUDIO_VERSION="${PROJECT_VERSION}")
set_target_properties(PKGStudio PROPERTIES
    OUTPUT_NAME "PKGStudio"
    WIN32_EXECUTABLE OFF)
