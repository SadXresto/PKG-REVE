// GameForge Phase 3: build-config parsing, dependency graph and porting
// intelligence, including malformed build files.
#include "core/gameforge/BuildConfig.h"
#include "core/gameforge/DependencyGraph.h"
#include "core/gameforge/GameProject.h"
#include "core/gameforge/PortingIntelligence.h"
#include "ui/DependencyGraphView.h"
#include "ui/GameForgePanel.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace pkg;
using namespace pkg::gameforge;
using pkg::ui::DependencyGraphView;

namespace {

void writeFile(const QString& path, const QByteArray& contents)
{
    const QFileInfo info(path);
    ASSERT_TRUE(QDir().mkpath(info.absolutePath()));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(contents), contents.size());
}

const ConfigFinding* firstFinding(const BuildConfigReport& report, ConfigKind kind,
                                  const QString& namePart = QString())
{
    for (const ConfigFinding& finding : report.findings) {
        if (finding.kind != kind)
            continue;
        if (namePart.isEmpty() || finding.name.contains(namePart, Qt::CaseInsensitive))
            return &finding;
    }
    return nullptr;
}

bool hasInsight(const QVector<IntelInsight>& insights, const QString& titlePart)
{
    for (const IntelInsight& insight : insights) {
        if (insight.title.contains(titlePart, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

const QByteArray kCmakeProject =
    "cmake_minimum_required(VERSION 3.21)\n"
    "project(Game VERSION 1.2.3 LANGUAGES CXX)\n"
    "set(CMAKE_CXX_STANDARD 17)\n"
    "find_package(SDL2 2.0 REQUIRED)\n"
    "add_library(engine STATIC src/engine.cpp)\n"
    "add_executable(game src/main.cpp src/render.cpp)\n"
    "target_link_libraries(game PRIVATE engine SDL2::SDL2 Threads::Threads)\n"
    "if(WIN32)\n"
    "  target_compile_definitions(game PRIVATE _WIN32)\n"
    "endif()\n"
    "add_compile_options(-m64)\n";

} // namespace

TEST(GameForgeConfig, ParsesCmakeTargetsSettingsAndConditions)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("CMakeLists.txt")), kCmakeProject);

    const ProjectScan scan = scanProject(dir.path());
    ASSERT_TRUE(scan.readable());
    const BuildConfigReport report = analyzeBuildConfig(scan);

    EXPECT_EQ(report.projectName, QStringLiteral("Game"));
    EXPECT_EQ(report.projectVersion, QStringLiteral("1.2.3"));
    EXPECT_EQ(report.targets.size(), 2);
    ASSERT_NE(report.findTarget(QStringLiteral("game")), nullptr);
    EXPECT_EQ(report.findTarget(QStringLiteral("game"))->type, QStringLiteral("executable"));
    EXPECT_EQ(report.findTarget(QStringLiteral("game"))->sources.size(), 2);
    EXPECT_EQ(report.findTarget(QStringLiteral("engine"))->type, QStringLiteral("library"));

    // SDL2 carries the version from find_package; the link entry does not.
    bool sawSdl = false;
    bool sawThreads = false;
    for (const DeclaredDependency& dependency : report.dependencies) {
        if (dependency.name == QLatin1String("SDL2")) {
            sawSdl = true;
            EXPECT_EQ(dependency.version, QStringLiteral("2.0"));
            EXPECT_TRUE(dependency.explicitDeclaration);
            EXPECT_EQ(dependency.declaredBy, QStringLiteral("find_package"));
        }
        if (dependency.name == QLatin1String("Threads::Threads")) {
            sawThreads = true;
            EXPECT_FALSE(dependency.explicitDeclaration);
        }
    }
    EXPECT_TRUE(sawSdl);
    EXPECT_TRUE(sawThreads);

    const ConfigFinding* standard = firstFinding(report, ConfigKind::CompilerSetting,
                                                 QStringLiteral("STANDARD"));
    ASSERT_NE(standard, nullptr);
    EXPECT_EQ(standard->file, QStringLiteral("CMakeLists.txt"));
    EXPECT_GT(standard->line, 0);
    EXPECT_FALSE(standard->evidence.isEmpty());

    ASSERT_NE(firstFinding(report, ConfigKind::PlatformCondition, QStringLiteral("WIN32")),
              nullptr);
    ASSERT_NE(firstFinding(report, ConfigKind::Architecture, QStringLiteral("64-bit")),
              nullptr);
}

TEST(GameForgeConfig, DependencyGraphConnectsDeclaredAndInferredEdges)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("CMakeLists.txt")), kCmakeProject);
    const ProjectScan scan = scanProject(dir.path());
    const BuildConfigReport config = analyzeBuildConfig(scan);
    const DependencyGraph graph = buildDependencyGraph(config, scan);

    const GraphNode* project = graph.find(QStringLiteral("project"));
    ASSERT_NE(project, nullptr);
    EXPECT_EQ(project->kind, NodeKind::Project);
    EXPECT_EQ(project->label, QStringLiteral("Game"));

    const GraphNode* game = graph.find(QStringLiteral("target:game"));
    ASSERT_NE(game, nullptr);
    EXPECT_EQ(game->kind, NodeKind::Target);
    EXPECT_EQ(game->file, QStringLiteral("CMakeLists.txt"));

    const GraphNode* sdl = graph.find(QStringLiteral("dep:sdl2::sdl2"));
    ASSERT_NE(sdl, nullptr);
    EXPECT_EQ(sdl->kind, NodeKind::Dependency);
    EXPECT_TRUE(sdl->confirmed) << "find_package declares it explicitly";
    EXPECT_NE(sdl->detail.indexOf(QStringLiteral("2.0")), -1);

    // engine is a project target, so the edge is confirmed; the project->target
    // edge is a view relation and must stay inferred.
    bool engineEdge = false;
    bool rootEdgeInferred = false;
    for (const GraphEdge& edge : graph.edges) {
        if (edge.from == QLatin1String("target:game")
            && edge.to == QLatin1String("target:engine")) {
            engineEdge = true;
            EXPECT_TRUE(edge.confirmed);
            EXPECT_GT(edge.line, 0);
        }
        if (edge.from == QLatin1String("project")
            && edge.to == QLatin1String("target:game")) {
            rootEdgeInferred = !edge.confirmed;
            EXPECT_FALSE(edge.evidence.isEmpty());
        }
    }
    EXPECT_TRUE(engineEdge);
    EXPECT_TRUE(rootEdgeInferred);
    EXPECT_GT(graph.inferredEdgeCount(), 0);

    // Threads::Threads is linked but never declared.
    bool threadsUnresolved = false;
    for (const QString& entry : graph.unresolved) {
        if (entry.contains(QLatin1String("Threads::Threads")))
            threadsUnresolved = true;
    }
    EXPECT_TRUE(threadsUnresolved);
    const GraphNode* threads = graph.find(QStringLiteral("dep:threads::threads"));
    ASSERT_NE(threads, nullptr);
    EXPECT_FALSE(threads->confirmed);
}

TEST(GameForgeConfig, CircularDependenciesAreReportedSafely)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("CMakeLists.txt")),
              QByteArray("project(Cycle)\n"
                         "add_library(a STATIC a.cpp)\n"
                         "add_library(b STATIC b.cpp)\n"
                         "target_link_libraries(a PRIVATE b)\n"
                         "target_link_libraries(b PRIVATE a)\n"));
    const ProjectScan scan = scanProject(dir.path());
    const BuildConfigReport config = analyzeBuildConfig(scan);
    const DependencyGraph graph = buildDependencyGraph(config, scan);

    ASSERT_EQ(graph.cycles.size(), 1);
    EXPECT_NE(graph.cycles.first().indexOf(QStringLiteral("->")), -1);
    EXPECT_EQ(graph.nodes.size(), 3); // project + a + b, no duplicate nodes
}

TEST(GameForgeConfig, NamespacedLinkResolvesToDeclaredPackage)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("CMakeLists.txt")),
              QByteArray("project(P)\n"
                         "find_package(SDL2 2.0 REQUIRED)\n"
                         "add_executable(game main.cpp)\n"
                         "target_link_libraries(game PRIVATE SDL2::SDL2)\n"));
    const ProjectScan scan = scanProject(dir.path());
    const BuildConfigReport config = analyzeBuildConfig(scan);
    const DependencyGraph graph = buildDependencyGraph(config, scan);
    EXPECT_TRUE(graph.unresolved.isEmpty());
    const GraphNode* node = graph.find(QStringLiteral("dep:sdl2::sdl2"));
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(node->confirmed);
}

TEST(GameForgeConfig, ParsesMesonMakefileGodotAndUnreal)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("meson.build")),
              QByteArray("project('m', 'cpp', version : '0.3')\n"
                         "sdl = dependency('sdl2', version : '>=2.0')\n"
                         "executable('game', 'main.cpp', dependencies : [sdl])\n"
                         "add_project_arguments('-m32', language : 'cpp')\n"));
    {
        const ProjectScan scan = scanProject(dir.path());
        const BuildConfigReport report = analyzeBuildConfig(scan);
        EXPECT_EQ(report.projectName, QStringLiteral("m"));
        EXPECT_EQ(report.projectVersion, QStringLiteral("0.3"));
        ASSERT_EQ(report.targets.size(), 1);
        ASSERT_EQ(report.targets.first().sources.size(), 1);
        EXPECT_EQ(report.targets.first().sources.first(), QStringLiteral("main.cpp"));
        bool versionedDependency = false;
        for (const auto& dependency : report.dependencies) {
            if (dependency.name == QLatin1String("sdl2"))
                versionedDependency = dependency.version == QLatin1String(">=2.0");
        }
        EXPECT_TRUE(versionedDependency);
        ASSERT_NE(firstFinding(report, ConfigKind::Architecture, QStringLiteral("32-bit")),
                  nullptr);
    }

    QTemporaryDir makeDir;
    ASSERT_TRUE(makeDir.isValid());
    writeFile(makeDir.filePath(QStringLiteral("Makefile")),
              QByteArray("# demo\n"
                         "CXXFLAGS = -m32 -pthread\n"
                         "all: game\n"
                         "game: main.o engine.o\n"
                         "\t$(CXX) -o game main.o engine.o\n"));
    {
        const ProjectScan scan = scanProject(makeDir.path());
        const BuildConfigReport report = analyzeBuildConfig(scan);
        ASSERT_GE(report.targets.size(), 2);
        ASSERT_NE(report.findTarget(QStringLiteral("game")), nullptr);
        ASSERT_NE(firstFinding(report, ConfigKind::Architecture, QStringLiteral("32-bit")),
                  nullptr);
        ASSERT_NE(firstFinding(report, ConfigKind::CompilerSetting,
                               QStringLiteral("POSIX threads")),
                  nullptr);
        bool prerequisite = false;
        for (const auto& dependency : report.dependencies) {
            if (dependency.name == QLatin1String("main.o")
                && dependency.declaredBy == QLatin1String("make prerequisite"))
                prerequisite = true;
        }
        EXPECT_TRUE(prerequisite);
    }

    QTemporaryDir godotDir;
    ASSERT_TRUE(godotDir.isValid());
    writeFile(godotDir.filePath(QStringLiteral("project.godot")),
              QByteArray("config_version=5\n"
                         "[application]\n"
                         "config/name=\"Forge Runner\"\n"
                         "run/main_scene=\"res://main.tscn\"\n"
                         "config/features=PackedStringArray(\"4.3\", \"Forward Plus\")\n"
                         "[rendering]\n"
                         "renderer/rendering_method=\"gl_compatibility\"\n"
                         "[input]\n"
                         "jump={\n\"deadzone\": 0.5\n}\n"));
    {
        const ProjectScan scan = scanProject(godotDir.path());
        const BuildConfigReport report = analyzeBuildConfig(scan);
        EXPECT_EQ(report.projectName, QStringLiteral("Forge Runner"));
        EXPECT_EQ(report.engineName, QStringLiteral("Godot"));
        EXPECT_EQ(report.engineVersion, QStringLiteral("4.3"));
        ASSERT_NE(firstFinding(report, ConfigKind::CompilerSetting, QStringLiteral("renderer")),
                  nullptr);
    }

    QTemporaryDir unrealDir;
    ASSERT_TRUE(unrealDir.isValid());
    writeFile(unrealDir.filePath(QStringLiteral("Forge.uproject")),
              QByteArray("{\"FileVersion\": 3, \"EngineAssociation\": \"5.4\",\n"
                         " \"Modules\": [{\"Name\": \"Forge\", \"Type\": \"Runtime\"}],\n"
                         " \"Plugins\": [{\"Name\": \"EnhancedInput\", \"Enabled\": true},\n"
                         "              {\"Name\": \"Disabled\", \"Enabled\": false}]}\n"));
    {
        const ProjectScan scan = scanProject(unrealDir.path());
        const BuildConfigReport report = analyzeBuildConfig(scan);
        EXPECT_EQ(report.engineName, QStringLiteral("Unreal Engine"));
        EXPECT_EQ(report.engineVersion, QStringLiteral("5.4"));
        ASSERT_EQ(report.targets.size(), 1);
        EXPECT_EQ(report.targets.first().name, QStringLiteral("Forge"));
        ASSERT_EQ(report.dependencies.size(), 1);
        EXPECT_EQ(report.dependencies.first().name, QStringLiteral("EnhancedInput"));
    }
}

TEST(GameForgeConfig, MalformedInputsDoNotCrashOrInventFindings)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // Unterminated call: parsing stops instead of guessing arguments.
    writeFile(dir.filePath(QStringLiteral("CMakeLists.txt")),
              QByteArray("project(Broken)\nadd_executable(game src/main.cpp\n"));
    {
        const ProjectScan scan = scanProject(dir.path());
        const BuildConfigReport report = analyzeBuildConfig(scan);
        EXPECT_EQ(report.findTarget(QStringLiteral("game")), nullptr);
        EXPECT_FALSE(report.findings.isEmpty());
    }

    QTemporaryDir binaryDir;
    ASSERT_TRUE(binaryDir.isValid());
    writeFile(binaryDir.filePath(QStringLiteral("CMakeLists.txt")),
              QByteArray("project(Bin)\0add_executable(x y.cpp)", 34));
    {
        const ProjectScan scan = scanProject(binaryDir.path());
        const BuildConfigReport report = analyzeBuildConfig(scan);
        EXPECT_TRUE(report.targets.isEmpty());
        EXPECT_FALSE(report.warnings.isEmpty());
    }

    QTemporaryDir badJsonDir;
    ASSERT_TRUE(badJsonDir.isValid());
    writeFile(badJsonDir.filePath(QStringLiteral("Broken.uproject")),
              QByteArray("{\"Modules\": [{\"Name\": "));
    {
        const ProjectScan scan = scanProject(badJsonDir.path());
        const BuildConfigReport report = analyzeBuildConfig(scan);
        ASSERT_FALSE(report.warnings.isEmpty());
        EXPECT_NE(report.warnings.first().indexOf(QStringLiteral("not valid JSON")), -1);
        EXPECT_TRUE(report.targets.isEmpty());
    }

    // A very long single line and an empty file are bounded, not fatal.
    QTemporaryDir longDir;
    ASSERT_TRUE(longDir.isValid());
    writeFile(longDir.filePath(QStringLiteral("Makefile")),
              QByteArray("all: " + QByteArray(200000, 'x') + "\n"));
    writeFile(longDir.filePath(QStringLiteral("empty.mk")), QByteArray());
    {
        const ProjectScan scan = scanProject(longDir.path());
        const BuildConfigReport report = analyzeBuildConfig(scan);
        EXPECT_FALSE(report.analyzedFiles.isEmpty());
        EXPECT_GE(report.linesRead, 1);
    }
}

TEST(GameForgeConfig, ReportWithoutBuildFilesSaysSo)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("readme.md")), QByteArray("# notes\n"));
    const ProjectScan scan = scanProject(dir.path());
    const BuildConfigReport report = analyzeBuildConfig(scan);
    EXPECT_TRUE(report.analyzedFiles.isEmpty());
    ASSERT_FALSE(report.warnings.isEmpty());
    EXPECT_NE(report.warnings.first().indexOf(QStringLiteral("No supported build files")), -1);

    const DependencyGraph graph = buildDependencyGraph(report, scan);
    EXPECT_EQ(graph.nodes.size(), 1); // only the project root
    EXPECT_TRUE(graph.edges.isEmpty());
    EXPECT_TRUE(graph.unresolved.isEmpty());
}

TEST(GameForgeConfig, FileLimitMarksTheReportPartial)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("CMakeLists.txt")),
              QByteArray("project(One)\nadd_executable(one a.cpp)\n"));
    writeFile(dir.filePath(QStringLiteral("Makefile")), QByteArray("all: one\n"));
    const ProjectScan scan = scanProject(dir.path());
    ConfigLimits limits;
    limits.maxFiles = 1;
    const BuildConfigReport report = analyzeBuildConfig(scan, {}, limits);
    EXPECT_EQ(report.analyzedFiles.size(), 1);
    EXPECT_TRUE(report.truncated);
    EXPECT_FALSE(report.warnings.isEmpty());
}

TEST(GameForgeConfig, GraphNodeLimitIsHonest)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QByteArray cmake = "project(Big)\nadd_executable(game main.cpp)\n"
                       "target_link_libraries(game";
    for (int i = 0; i < 40; ++i)
        cmake += " lib" + QByteArray::number(i);
    cmake += ")\n";
    writeFile(dir.filePath(QStringLiteral("CMakeLists.txt")), cmake);
    const ProjectScan scan = scanProject(dir.path());
    const BuildConfigReport config = analyzeBuildConfig(scan);
    GraphLimits limits;
    limits.maxNodes = 5;
    const DependencyGraph graph = buildDependencyGraph(config, scan, limits);
    EXPECT_EQ(graph.nodes.size(), 5);
    EXPECT_TRUE(graph.truncated);
    EXPECT_FALSE(graph.warnings.isEmpty());
}

TEST(GameForgeConfig, PortingIntelligenceUsesOnlyEvidence)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("CMakeLists.txt")),
              QByteArray("project(Port)\n"
                         "add_executable(game main.cpp)\n"
                         "target_link_libraries(game PRIVATE zlib)\n"
                         "if(WIN32)\n"
                         "  add_definitions(-DWINDOWS)\n"
                         "endif()\n"
                         "add_compile_options(-pthread)\n"));
    const ProjectScan scan = scanProject(dir.path());
    const BuildConfigReport config = analyzeBuildConfig(scan);
    const DependencyGraph graph = buildDependencyGraph(config, scan);

    PortingReport portability;
    portability.scannedFiles = 3;
    PortingFinding directx;
    directx.id = QStringLiteral("directx");
    directx.label = QStringLiteral("Direct3D");
    directx.area = SignalArea::Graphics;
    directx.occurrences = 4;
    directx.fileCount = 2;
    directx.files = {QStringLiteral("src/render.cpp")};
    directx.example = QStringLiteral("src/render.cpp:12");
    directx.note = QStringLiteral("Direct3D is Windows-only.");
    portability.findings.append(directx);

    const PortingIntelligence intel = analyzePortingIntelligence(config, graph, portability);
    EXPECT_TRUE(hasInsight(intel.windows, QStringLiteral("WIN32")));
    EXPECT_TRUE(hasInsight(intel.windows, QStringLiteral("Direct3D")));
    EXPECT_TRUE(hasInsight(intel.graphicsInput, QStringLiteral("Direct3D")));
    EXPECT_TRUE(hasInsight(intel.recommendations, QStringLiteral("non-Windows")));
    EXPECT_TRUE(hasInsight(intel.recommendations, QStringLiteral("POSIX thread")));
    EXPECT_FALSE(intel.externalDependencies.isEmpty());
    EXPECT_FALSE(intel.notes.isEmpty());

    for (const auto& list : {intel.windows, intel.graphicsInput, intel.architecture}) {
        for (const IntelInsight& insight : list) {
            if (!insight.file.isEmpty()) {
                EXPECT_GT(insight.line, 0) << insight.title.toStdString();
            }
        }
    }
    for (const IntelInsight& insight : intel.windows) {
        EXPECT_NE((insight.title + insight.detail).indexOf(QStringLiteral("Win")), -1);
    }
}

TEST(GameForgeView, GraphCanvasSelectsAndFiltersNodes)
{
    DependencyGraphView view;
    EXPECT_FALSE(view.hasGraph());

    DependencyGraph graph;
    GraphNode project;
    project.id = QStringLiteral("project");
    project.label = QStringLiteral("Project");
    project.kind = NodeKind::Project;
    GraphNode target;
    target.id = QStringLiteral("target:game");
    target.label = QStringLiteral("game");
    target.kind = NodeKind::Target;
    target.file = QStringLiteral("CMakeLists.txt");
    target.line = 4;
    GraphNode dependency;
    dependency.id = QStringLiteral("dep:sdl2");
    dependency.label = QStringLiteral("SDL2");
    dependency.kind = NodeKind::Dependency;
    dependency.confirmed = true;
    graph.nodes = {project, target, dependency};
    graph.edges = {{project.id, target.id, false, QStringLiteral("view root"), {}, 0},
                   {target.id, dependency.id, true, QStringLiteral("link"), {}, 0}};

    view.setGraph(graph);
    EXPECT_TRUE(view.hasGraph());
    EXPECT_EQ(view.visibleNodeCount(), 3);

    view.selectNode(QStringLiteral("target:game"));
    EXPECT_EQ(view.selectedNodeId(), QStringLiteral("target:game"));

    view.setFilter(QStringLiteral("sdl"));
    EXPECT_EQ(view.visibleNodeCount(), 1);
    view.setFilter(QString());
    EXPECT_EQ(view.visibleNodeCount(), 3);

    // An empty graph is drawn as a message, not as a crash.
    view.setGraph(DependencyGraph{});
    EXPECT_FALSE(view.hasGraph());
}

TEST(GameForgePanelWidget, ScansProjectAndBuildsAnalysisInBackground)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("CMakeLists.txt")),
              QByteArray("project(Panel VERSION 2.0)\n"
                         "find_package(SDL2 2.0 REQUIRED)\n"
                         "add_executable(game src/main.cpp)\n"
                         "target_link_libraries(game PRIVATE SDL2::SDL2)\n"));
    writeFile(dir.filePath(QStringLiteral("src/main.cpp")),
              QByteArray("#include <SDL.h>\nint main() { return 0; }\n"));

    ui::GameForgePanel panel;
    EXPECT_FALSE(panel.hasScan());
    panel.setProjectRoot(dir.path());
    EXPECT_FALSE(panel.projectRoot().isEmpty());

    QElapsedTimer timer;
    timer.start();
    while ((panel.isScanning() || !panel.hasAnalysis()) && timer.elapsed() < 10000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    EXPECT_TRUE(panel.hasScan());
    EXPECT_FALSE(panel.isScanning()) << "background work must finish or be cancelled";
    EXPECT_EQ(panel.detectedKind(), QStringLiteral("CMake"));
    EXPECT_FALSE(panel.buildConfig().analyzedFiles.isEmpty());
    EXPECT_GE(panel.dependencyGraph().nodes.size(), 3); // project + game + SDL2
    EXPECT_FALSE(panel.portingIntelligence().notes.isEmpty());
    EXPECT_FALSE(panel.portabilityReport().findings.isEmpty());
    EXPECT_EQ(panel.buildConfig().projectVersion, QStringLiteral("2.0"));
}

TEST(GameForgeConfig, NoEvidenceProducesNoPlatformClaims)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("CMakeLists.txt")),
              QByteArray("project(Plain)\n"
                         "add_executable(game main.cpp)\n"
                         "set(CMAKE_CXX_STANDARD 20)\n"));
    const ProjectScan scan = scanProject(dir.path());
    const BuildConfigReport config = analyzeBuildConfig(scan);
    const DependencyGraph graph = buildDependencyGraph(config, scan);
    const PortingIntelligence intel =
        analyzePortingIntelligence(config, graph, PortingReport{});

    EXPECT_TRUE(intel.windows.isEmpty());
    EXPECT_TRUE(intel.linuxPlatform.isEmpty());
    EXPECT_TRUE(intel.graphicsInput.isEmpty());
    EXPECT_TRUE(intel.architecture.isEmpty());
    EXPECT_FALSE(intel.notes.isEmpty());
    bool mentionsArchitectureGap = false;
    for (const QString& note : intel.notes) {
        if (note.contains(QLatin1String("No architecture requirement")))
            mentionsArchitectureGap = true;
    }
    EXPECT_TRUE(mentionsArchitectureGap);
    // Nothing is declared as a problem and no recommendation cites a fake file.
    for (const IntelInsight& insight : intel.recommendations) {
        if (!insight.file.isEmpty()) {
            EXPECT_TRUE(QFileInfo::exists(dir.filePath(insight.file)));
        }
    }
}
