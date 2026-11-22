#include "data/Database.h"
#include "devflow/BuildRunner.h"
#include "devflow/DevFlowModels.h"
#include "devflow/DevFlowStore.h"
#include "devflow/ErrorIntelligence.h"
#include "devflow/ProjectHealth.h"
#include "devflow/WorkflowRunner.h"
#include "ui/DevFlowPanel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>
#include <memory>

using namespace pkg;
using namespace pkg::devflow;

namespace {

QString write(const QDir& dir, const QString& name, const QByteArray& bytes)
{
    dir.mkpath(QStringLiteral("."));
    const QString path = dir.filePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(bytes);
    file.close();
    return path;
}

BuildProfile sampleProfile(const QString& projectPath)
{
    BuildProfile profile;
    profile.id = QStringLiteral("profile-1");
    profile.name = QStringLiteral("Debug build");
    profile.projectPath = projectPath;
    profile.buildSystem = BuildSystem::CMake;
    profile.buildDirectory = QStringLiteral("build");
    profile.configuration = QStringLiteral("Debug");
    profile.target = QStringLiteral("game");
    profile.arguments = {QStringLiteral("--parallel"), QStringLiteral("4")};
    profile.environment.insert(QStringLiteral("CCACHE_DIR"), QStringLiteral("/tmp/ccache"));
    profile.createdAt = QDateTime::currentDateTime();
    return profile;
}

}  // namespace

TEST(DevFlowModelsTest, ProfileJsonRoundTripAndValidation)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    BuildProfile profile = sampleProfile(dir.path());
    ASSERT_TRUE(profile.validate());

    const auto restored = BuildProfile::fromJson(profile.toJson());
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored.value().name, profile.name);
    EXPECT_EQ(restored.value().buildSystem, BuildSystem::CMake);
    EXPECT_EQ(restored.value().target, QStringLiteral("game"));
    EXPECT_EQ(restored.value().arguments, profile.arguments);
    EXPECT_EQ(restored.value().environment.value(QStringLiteral("CCACHE_DIR")),
              QStringLiteral("/tmp/ccache"));

    BuildProfile unnamed = profile;
    unnamed.name.clear();
    EXPECT_FALSE(unnamed.validate());

    BuildProfile unknown = profile;
    unknown.buildSystem = BuildSystem::Unknown;
    EXPECT_FALSE(unknown.validate());

    BuildProfile missingProject = profile;
    missingProject.projectPath = dir.filePath(QStringLiteral("missing"));
    EXPECT_FALSE(missingProject.validate());
}

TEST(DevFlowModelsTest, TaskAndSnapshotRoundTrip)
{
    ProjectTask task;
    task.id = QStringLiteral("task-1");
    task.projectPath = QStringLiteral("/tmp/project");
    task.title = QStringLiteral("Fix link error");
    task.description = QStringLiteral("Add the missing library.");
    task.priority = TaskPriority::High;
    task.status = TaskStatus::InProgress;
    task.linkedFile = QStringLiteral("src/main.cpp");
    task.linkedError = QStringLiteral("error|main.cpp|undefined reference");
    task.createdAt = QDateTime::currentDateTime();
    task.updatedAt = task.createdAt;
    ASSERT_TRUE(task.validate());

    const auto restored = ProjectTask::fromJson(task.toJson());
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored.value().priority, TaskPriority::High);
    EXPECT_EQ(restored.value().status, TaskStatus::InProgress);
    EXPECT_EQ(restored.value().linkedError, task.linkedError);

    ProjectTask untitled = task;
    untitled.title = QStringLiteral("   ");
    EXPECT_FALSE(untitled.validate());

    WorkspaceSnapshot snapshot;
    snapshot.id = QStringLiteral("snapshot-1");
    snapshot.name = QStringLiteral("Review layout");
    snapshot.projectPath = QStringLiteral("/tmp/project");
    snapshot.openFiles = {QStringLiteral("/tmp/project/a.cpp"),
                          QStringLiteral("/tmp/project/b.cpp")};
    snapshot.activeIndex = 1;
    snapshot.sidebarId = QStringLiteral("devflow");
    snapshot.splitterSizes = {290, 990};
    snapshot.cursorOffsets.insert(QStringLiteral("/tmp/project/a.cpp"), 0x40);
    snapshot.createdAt = QDateTime::currentDateTime();
    ASSERT_TRUE(snapshot.validate());

    const auto restoredSnapshot = WorkspaceSnapshot::fromJson(snapshot.toJson());
    ASSERT_TRUE(restoredSnapshot);
    EXPECT_EQ(restoredSnapshot.value().openFiles.size(), 2);
    EXPECT_EQ(restoredSnapshot.value().activeIndex, 1);
    EXPECT_EQ(restoredSnapshot.value().cursorOffsets.value(QStringLiteral("/tmp/project/a.cpp")),
              0x40u);
    EXPECT_FALSE(restoredSnapshot.value().summary().isEmpty());
}

TEST(DevFlowModelsTest, WorkflowsRejectUnsupportedOrImpossibleSteps)
{
    WorkflowDefinition workflow;
    workflow.id = QStringLiteral("workflow-1");
    workflow.name = QStringLiteral("Review");
    workflow.steps = WorkflowDefinition::supportedSteps();
    EXPECT_TRUE(workflow.validate());

    WorkflowDefinition unknown = workflow;
    unknown.steps = {QStringLiteral("run-arbitrary-script")};
    EXPECT_FALSE(unknown.validate());

    WorkflowDefinition reportFirst;
    reportFirst.id = QStringLiteral("workflow-2");
    reportFirst.name = QStringLiteral("Report only");
    reportFirst.steps = {QStringLiteral("report")};
    EXPECT_FALSE(reportFirst.validate());

    const auto restored = WorkflowDefinition::fromJson(workflow.toJson());
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored.value().steps, workflow.steps);
}

TEST(DevFlowModelsTest, QuickActionsMatchSupportedCapabilities)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = write(QDir(dir.path()), QStringLiteral("a.bin"), "abc");
    const QVector<QuickAction> actions = quickActionsForFile(file, false);
    QStringList ids;
    for (const QuickAction& action : actions)
        ids << action.id;
    EXPECT_TRUE(ids.contains(QStringLiteral("hex.open")));
    EXPECT_TRUE(ids.contains(QStringLiteral("metadata")));
    EXPECT_TRUE(ids.contains(QStringLiteral("hash")));
    EXPECT_TRUE(ids.contains(QStringLiteral("compare")));
    EXPECT_TRUE(ids.contains(QStringLiteral("properties")));
    EXPECT_FALSE(ids.contains(QStringLiteral("bookmark")));

    const QVector<QuickAction> withHex = quickActionsForFile(file, true);
    ids.clear();
    for (const QuickAction& action : withHex)
        ids << action.id;
    EXPECT_TRUE(ids.contains(QStringLiteral("bookmark")));

    const QVector<QuickAction> directory = quickActionsForFile(dir.path(), false);
    ids.clear();
    for (const QuickAction& action : directory)
        ids << action.id;
    EXPECT_FALSE(ids.contains(QStringLiteral("hash")));
    EXPECT_TRUE(ids.contains(QStringLiteral("folder")));
}

TEST(ErrorIntelligenceTest, ParsesGccAndMsvcDiagnostics)
{
    const QString output = QStringLiteral(
        "src/main.cpp:12:5: error: 'missing' was not declared in this scope\n"
        "src/main.cpp:20:1: warning: unused variable 'x' [-Wunused-variable]\n"
        "src/main.cpp(30,7): error C2065: 'foo': undeclared identifier\n"
        "FAILED: game\n"
        "ninja: build stopped: subcommand failed.\n");
    const ErrorReport report = parseBuildOutput(output);
    EXPECT_EQ(report.diagnostics.size(), 3);
    EXPECT_EQ(report.errorCount(), 2);
    EXPECT_EQ(report.warningCount(), 1);
    EXPECT_EQ(report.diagnostics.at(0).file, QStringLiteral("src/main.cpp"));
    EXPECT_EQ(report.diagnostics.at(0).line, 12);
    EXPECT_EQ(report.diagnostics.at(0).column, 5);
    EXPECT_EQ(report.diagnostics.at(2).code, QStringLiteral("C2065"));
    EXPECT_FALSE(report.unparsedLines.isEmpty());
}

TEST(ErrorIntelligenceTest, GroupsDuplicatesAndExplainsLocally)
{
    const QString output = QStringLiteral(
        "src/a.cpp:1:1: error: undefined reference to 'engine_init'\n"
        "src/b.cpp:9:2: error: undefined reference to 'engine_init'\n"
        "src/c.cpp:3:1: error: no such file or directory: 'sdl2/sdl.h'\n");
    const ErrorReport report = parseBuildOutput(output);
    const QVector<DiagnosticGroup> groups = groupDiagnostics(report.diagnostics);
    ASSERT_EQ(groups.size(), 2);
    EXPECT_EQ(groups.at(0).count, 2);
    EXPECT_FALSE(groups.at(0).fingerprint.isEmpty());
    EXPECT_FALSE(groups.at(0).explanation.isEmpty());
    EXPECT_TRUE(groups.at(0).suggestion.toLower().contains(QStringLiteral("library")));

    EXPECT_FALSE(explainDiagnostic(QStringLiteral("undefined reference to 'x'")).isEmpty());
    EXPECT_FALSE(suggestFix(QStringLiteral("undefined reference to 'x'")).isEmpty());
    EXPECT_TRUE(explainDiagnostic(QStringLiteral("totally unknown failure")).isEmpty());
}

TEST(ErrorIntelligenceTest, MalformedOutputIsHandledHonestly)
{
    const QString output = QStringLiteral("random text\n:\n:12:34\nerror: without a file\n");
    const ErrorReport report = parseBuildOutput(output);
    EXPECT_TRUE(report.diagnostics.isEmpty());
    EXPECT_FALSE(report.unparsedLines.isEmpty());
    EXPECT_TRUE(groupDiagnostics(report.diagnostics).isEmpty());
}

TEST(DevFlowStoreTest, PersistsProfilesTasksSnapshotsAndWorkflowsInSqlite)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    Database database;
    ASSERT_TRUE(database.open(dir.filePath(QStringLiteral("test.db"))));
    EXPECT_GE(database.schemaVersion(), 2);

    DevFlowStore store(database);
    const BuildProfile profile = sampleProfile(dir.path());
    QString error;
    ASSERT_TRUE(store.saveProfile(profile, &error)) << error.toStdString();
    ASSERT_EQ(store.profiles().size(), 1);
    EXPECT_EQ(store.profiles().first().name, profile.name);

    ProjectTask task;
    task.id = QStringLiteral("task-1");
    task.projectPath = dir.path();
    task.title = QStringLiteral("First task");
    task.createdAt = QDateTime::currentDateTime();
    task.updatedAt = task.createdAt;
    ASSERT_TRUE(store.saveTask(task, &error));
    ASSERT_EQ(store.tasks(dir.path()).size(), 1);
    EXPECT_EQ(store.tasks(QStringLiteral("/other")).size(), 0);

    task.title = QStringLiteral("Renamed task");
    task.status = TaskStatus::Done;
    ASSERT_TRUE(store.saveTask(task, &error));
    ASSERT_EQ(store.tasks(dir.path()).size(), 1);
    EXPECT_EQ(store.tasks(dir.path()).first().title, QStringLiteral("Renamed task"));
    EXPECT_EQ(store.tasks(dir.path()).first().status, TaskStatus::Done);

    WorkspaceSnapshot snapshot;
    snapshot.id = QStringLiteral("snapshot-1");
    snapshot.name = QStringLiteral("Layout");
    snapshot.openFiles = {QStringLiteral("a.cpp")};
    snapshot.createdAt = QDateTime::currentDateTime();
    ASSERT_TRUE(store.saveSnapshot(snapshot, &error));
    EXPECT_EQ(store.snapshots().size(), 1);

    WorkflowDefinition workflow;
    workflow.id = QStringLiteral("workflow-1");
    workflow.name = QStringLiteral("Review");
    workflow.steps = {QStringLiteral("scan"), QStringLiteral("detect")};
    workflow.createdAt = QDateTime::currentDateTime();
    ASSERT_TRUE(store.saveWorkflow(workflow, &error));
    EXPECT_EQ(store.workflows().size(), 1);

    EXPECT_FALSE(store.isErrorResolved(dir.path(), QStringLiteral("fp")));
    store.setErrorResolved(dir.path(), QStringLiteral("fp"), true);
    EXPECT_TRUE(store.isErrorResolved(dir.path(), QStringLiteral("fp")));
    store.setErrorResolved(dir.path(), QStringLiteral("fp"), false);
    EXPECT_FALSE(store.isErrorResolved(dir.path(), QStringLiteral("fp")));

    store.removeTask(task.id);
    EXPECT_TRUE(store.tasks(dir.path()).isEmpty());
}

TEST(DevFlowHealthTest, ReportsOnlyEvidenceBackedFindings)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QDir root(dir.path());
    write(root, QStringLiteral("CMakeLists.txt"),
          "cmake_minimum_required(VERSION 3.20)\n"
          "project(Demo LANGUAGES CXX)\n"
          "add_executable(demo src/main.cpp)\n"
          "target_link_libraries(demo PRIVATE SDL2::SDL2)\n");
    write(root, QStringLiteral("src/notes.h"), "// present\n");

    const gameforge::ProjectScan scan = gameforge::scanProject(dir.path());
    ASSERT_TRUE(scan.readable());
    const gameforge::BuildConfigReport config =
        gameforge::analyzeBuildConfig(scan);
    const gameforge::DependencyGraph graph =
        gameforge::buildDependencyGraph(config, scan);
    const gameforge::PortingReport porting = gameforge::analyzePortability(scan);
    const HealthReport report =
        checkProjectHealth(dir.path(), scan, config, graph, porting);

    bool missingSource = false;
    for (const HealthFinding& finding : report.findings) {
        EXPECT_FALSE(finding.evidence.trimmed().isEmpty());
        EXPECT_FALSE(finding.confidence.trimmed().isEmpty());
        if (finding.id == QLatin1String("build.missing-source"))
            missingSource = true;
    }
    EXPECT_TRUE(missingSource);
    EXPECT_GE(report.checkedFiles, 2);
    EXPECT_GE(report.checkedTargets, 1);
}

TEST(DevFlowHealthTest, UnreadableProjectIsReportedNotScored)
{
    const gameforge::ProjectScan scan;
    const gameforge::BuildConfigReport config;
    const gameforge::DependencyGraph graph;
    const HealthReport report = checkProjectHealth(
        QStringLiteral("/nonexistent-project"), scan, config, graph, {});
    ASSERT_FALSE(report.findings.isEmpty());
    EXPECT_EQ(report.findings.first().severity, HealthSeverity::Critical);
    EXPECT_FALSE(report.noFindings());
}

TEST(DevFlowBuildTest, CommandUsesRealToolAndNoShell)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    BuildProfile profile = sampleProfile(dir.path());

    if (BuildRunner::toolAvailable(BuildSystem::CMake)) {
        QString error;
        const BuildCommand command = BuildRunner::commandFor(profile, &error);
        ASSERT_TRUE(command.isValid()) << error.toStdString();
        EXPECT_TRUE(QFileInfo(command.program).isAbsolute());
        EXPECT_TRUE(command.program.endsWith(QStringLiteral("cmake")));
        EXPECT_EQ(command.arguments.first(), QStringLiteral("--build"));
        EXPECT_TRUE(command.arguments.contains(QStringLiteral("Debug")));
        EXPECT_TRUE(command.arguments.contains(QStringLiteral("game")));
        EXPECT_TRUE(command.display.contains(QStringLiteral("--build")));
    }

    BuildProfile makefile = profile;
    makefile.buildSystem = BuildSystem::Makefile;
    makefile.compiler = QStringLiteral("/usr/bin/clang++");
    QString error;
    const BuildCommand command = BuildRunner::commandFor(makefile, &error);
    if (command.isValid())
        EXPECT_EQ(command.environment.value(QStringLiteral("CXX")),
                  QStringLiteral("/usr/bin/clang++"));

    BuildProfile invalid;
    invalid.name = QStringLiteral("Broken");
    EXPECT_FALSE(BuildRunner::commandFor(invalid, &error).isValid());
    EXPECT_FALSE(error.isEmpty());
}

TEST(DevFlowPanelTest, BuildsAllWorkspaceTabs)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    Database database;
    ASSERT_TRUE(database.open(dir.filePath(QStringLiteral("panel.db"))));

    ui::DevFlowHooks hooks;
    const QString project = dir.path();
    hooks.projectPath = [project]() { return project; };
    ui::DevFlowPanel panel(database, hooks);
    for (const QString& tab : {QStringLiteral("dashboard"), QStringLiteral("build"),
                               QStringLiteral("errors"), QStringLiteral("tasks"),
                               QStringLiteral("snapshots"), QStringLiteral("health"),
                               QStringLiteral("workflows")}) {
        EXPECT_FALSE(panel.selectTab(tab).isEmpty()) << tab.toStdString();
    }
    EXPECT_TRUE(panel.selectTab(QStringLiteral("nope")).isEmpty());
    EXPECT_FALSE(panel.isBuildRunning());
    EXPECT_FALSE(panel.isWorkflowRunning());
    EXPECT_FALSE(panel.isHealthRunning());
}
