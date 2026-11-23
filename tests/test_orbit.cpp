// Orbit update (0.11.0): project intelligence, test center, smart diff, plugin
// SDK and AI patch review. Core logic only; UI is exercised by the screenshot
// mode and the application build.
#include "ai/AiClient.h"
#include "core/ProjectDiff.h"
#include "core/plugins/PluginManifest.h"
#include "core/projectnav/ProjectNavigator.h"
#include "devflow/TestCenter.h"
#include "ui/PatchReviewDialog.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {

void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(data);
}

QString readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

pkg::gameforge::ProjectFile projectFile(const QString& path, pkg::gameforge::FileCategory category)
{
    pkg::gameforge::ProjectFile file;
    file.relativePath = path;
    file.category = category;
    file.size = 128;
    return file;
}

pkg::gameforge::ProjectScan syntheticScan(const QString& root)
{
    pkg::gameforge::ProjectScan scan;
    scan.rootPath = root;
    scan.displayName = QStringLiteral("Demo");
    scan.kind = pkg::gameforge::ProjectKind::CMake;
    scan.kindEvidence = QStringLiteral("CMakeLists.txt");
    scan.files.append(projectFile(QStringLiteral("src/main.cpp"),
                                  pkg::gameforge::FileCategory::Source));
    scan.files.append(projectFile(QStringLiteral("CMakeLists.txt"),
                                  pkg::gameforge::FileCategory::Build));
    scan.buildFiles.append(QStringLiteral("CMakeLists.txt"));
    scan.totalBytes = 256;
    return scan;
}

pkg::gameforge::BuildConfigReport syntheticConfig()
{
    pkg::gameforge::BuildConfigReport config;
    config.analyzedFiles.append(QStringLiteral("CMakeLists.txt"));
    pkg::gameforge::BuildTarget target;
    target.name = QStringLiteral("demo");
    target.type = QStringLiteral("executable");
    target.file = QStringLiteral("CMakeLists.txt");
    target.line = 4;
    target.sources = {QStringLiteral("src/main.cpp")};
    target.links = {QStringLiteral("SDL2")};
    config.targets.append(target);

    pkg::gameforge::DeclaredDependency dependency;
    dependency.name = QStringLiteral("SDL2");
    dependency.version = QStringLiteral("2.28.0");
    dependency.declaredBy = QStringLiteral("find_package");
    dependency.file = QStringLiteral("CMakeLists.txt");
    dependency.line = 2;
    config.dependencies.append(dependency);
    return config;
}

pkg::gameforge::PortingReport syntheticPorting()
{
    pkg::gameforge::PortingReport report;
    report.scannedFiles = 1;
    pkg::gameforge::PortingFinding finding;
    finding.id = QStringLiteral("win32");
    finding.label = QStringLiteral("Win32 API");
    finding.area = pkg::gameforge::SignalArea::Platform;
    finding.occurrences = 3;
    finding.fileCount = 1;
    finding.files = {QStringLiteral("src/main.cpp")};
    finding.example = QStringLiteral("src/main.cpp:12");
    report.findings.append(finding);
    return report;
}

}  // namespace

// ---------------------------------------------------------------------------
// Project Navigator
// ---------------------------------------------------------------------------

TEST(ProjectNavigator, BuildsEvidenceBackedEntitiesAndRelations)
{
    const pkg::gameforge::ProjectScan scan = syntheticScan(QStringLiteral("/demo"));
    const pkg::gameforge::BuildConfigReport config = syntheticConfig();
    const pkg::gameforge::PortingReport porting = syntheticPorting();
    QVector<pkg::projectnav::ErrorInput> errors;
    errors.append(pkg::projectnav::ErrorInput{
        QStringLiteral("err-1"), QStringLiteral("src/main.cpp"), 5, QStringLiteral("error"),
        QStringLiteral("undeclared identifier 'x'")});

    const pkg::projectnav::Index index =
        pkg::projectnav::buildIndex(QStringLiteral("/demo"), scan, config,
                                    pkg::gameforge::DependencyGraph{}, porting, errors);
    EXPECT_FALSE(index.isEmpty());
    EXPECT_FALSE(index.cancelled);
    EXPECT_EQ(index.count(pkg::projectnav::EntityKind::File), 2);
    EXPECT_EQ(index.count(pkg::projectnav::EntityKind::Target), 1);
    EXPECT_EQ(index.count(pkg::projectnav::EntityKind::Dependency), 1);
    EXPECT_EQ(index.count(pkg::projectnav::EntityKind::BuildError), 1);
    EXPECT_EQ(index.count(pkg::projectnav::EntityKind::Portability), 1);
    EXPECT_GE(index.count(pkg::projectnav::EntityKind::SourceReference), 1);

    const pkg::projectnav::Entity* target = index.find(QStringLiteral("target:demo"));
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->confidence, pkg::projectnav::Confidence::Confirmed);
    EXPECT_EQ(target->line, 4);
    EXPECT_FALSE(target->evidence.isEmpty());

    const pkg::projectnav::Entity* dependency = index.find(QStringLiteral("dependency:sdl2"));
    ASSERT_NE(dependency, nullptr);
    EXPECT_EQ(dependency->confidence, pkg::projectnav::Confidence::Confirmed);

    bool links = false;
    for (const pkg::projectnav::Relation& relation : index.relationsOf(target->id)) {
        if (relation.to == dependency->id && relation.label == QStringLiteral("links")) {
            links = true;
            EXPECT_EQ(relation.confidence, pkg::projectnav::Confidence::Confirmed);
            EXPECT_FALSE(relation.evidence.isEmpty());
        }
    }
    EXPECT_TRUE(links);

    const QVector<int> matches = index.search(QStringLiteral("SDL2"));
    ASSERT_FALSE(matches.isEmpty());
    EXPECT_EQ(index.entities.at(matches.first()).label, QStringLiteral("SDL2"));
    EXPECT_TRUE(index.search(QStringLiteral("main.cpp")).size() >= 2);
    EXPECT_TRUE(index.search(QStringLiteral("nothing-matches-this")).isEmpty());

    // Every relationship names a real entity and carries evidence.
    for (const pkg::projectnav::Relation& relation : index.relations) {
        EXPECT_NE(index.find(relation.from), nullptr) << relation.from.toStdString();
        EXPECT_NE(index.find(relation.to), nullptr) << relation.to.toStdString();
    }
}

TEST(ProjectNavigator, ReportsCancellationWithoutInventingEntities)
{
    pkg::CancelToken token;
    token.cancel();
    const pkg::projectnav::Index index = pkg::projectnav::buildIndex(
        QStringLiteral("/demo"), syntheticScan(QStringLiteral("/demo")), syntheticConfig(),
        pkg::gameforge::DependencyGraph{},
        pkg::gameforge::PortingReport{}, {}, token);
    EXPECT_TRUE(index.cancelled);
    EXPECT_FALSE(index.notes.isEmpty());
}

TEST(ProjectNavigator, EmptyProjectIsExplicit)
{
    pkg::gameforge::ProjectScan scan;
    const pkg::projectnav::Index index = pkg::projectnav::buildIndex(
        QStringLiteral("/demo"), scan, {}, {}, {}, {});
    EXPECT_TRUE(index.isEmpty());
    EXPECT_FALSE(index.notes.isEmpty());
    EXPECT_EQ(pkg::projectnav::buildIndex(QString(), scan, {}, {}, {}, {}).notes.size(), 1);
}

// ---------------------------------------------------------------------------
// Smart project diff
// ---------------------------------------------------------------------------

TEST(ProjectDiff, ClassifiesFilesConfigurationDependenciesAndPortability)
{
    QTemporaryDir left;
    QTemporaryDir right;
    ASSERT_TRUE(left.isValid() && right.isValid());

    writeFile(QDir(left.path()).filePath(QStringLiteral("same.txt")), QByteArrayLiteral("same\n"));
    writeFile(QDir(right.path()).filePath(QStringLiteral("same.txt")), QByteArrayLiteral("same\n"));
    writeFile(QDir(left.path()).filePath(QStringLiteral("gone.txt")), QByteArrayLiteral("old\n"));
    writeFile(QDir(right.path()).filePath(QStringLiteral("new.txt")), QByteArrayLiteral("new\n"));
    writeFile(QDir(left.path()).filePath(QStringLiteral("changed.txt")),
              QByteArrayLiteral("left version\n"));
    writeFile(QDir(right.path()).filePath(QStringLiteral("changed.txt")),
              QByteArrayLiteral("right version\n"));

    writeFile(QDir(left.path()).filePath(QStringLiteral("CMakeLists.txt")),
              QByteArrayLiteral("cmake_minimum_required(VERSION 3.20)\n"
                                "project(Demo)\n"
                                "find_package(SDL2 2.0 REQUIRED)\n"
                                "add_executable(demo src/main.cpp)\n"));
    writeFile(QDir(right.path()).filePath(QStringLiteral("CMakeLists.txt")),
              QByteArrayLiteral("cmake_minimum_required(VERSION 3.20)\n"
                                "project(Demo)\n"
                                "find_package(SDL2 2.30 REQUIRED)\n"
                                "add_executable(demo src/main.cpp)\n"
                                "target_compile_options(demo PRIVATE -Wall)\n"));
    writeFile(QDir(left.path()).filePath(QStringLiteral("src/main.cpp")),
              QByteArrayLiteral("#include <windows.h>\nint main(){return 0;}\n"));
    writeFile(QDir(right.path()).filePath(QStringLiteral("src/main.cpp")),
              QByteArrayLiteral("int main(){return 0;}\n"));

    const pkg::projectdiff::DiffReport report =
        pkg::projectdiff::compareProjects(left.path(), right.path());
    EXPECT_FALSE(report.cancelled);
    EXPECT_GE(report.added, 1);
    EXPECT_GE(report.removed, 1);
    EXPECT_GE(report.modified, 2);
    EXPECT_GE(report.unchanged, 1);

    bool changedFound = false;
    for (const auto& change : report.files) {
        if (change.path == QStringLiteral("changed.txt")) {
            changedFound = true;
            EXPECT_EQ(change.kind, pkg::projectdiff::ChangeKind::Modified);
        }
        if (change.path == QStringLiteral("same.txt"))
            EXPECT_EQ(change.kind, pkg::projectdiff::ChangeKind::Unchanged);
    }
    EXPECT_TRUE(changedFound);

    EXPECT_FALSE(report.configChanges.isEmpty());
    for (const auto& change : report.configChanges) {
        if (change.file == QStringLiteral("CMakeLists.txt")) {
            EXPECT_EQ(change.kind, pkg::projectdiff::ChangeKind::Modified);
            EXPECT_FALSE(change.sampleLines.isEmpty());
            EXPECT_TRUE(change.summary.contains(QStringLiteral("Configuration")));
        }
    }

    bool dependencyChanged = false;
    for (const auto& change : report.dependencyChanges) {
        if (change.name == QLatin1String("SDL2")) {
            dependencyChanged = true;
            EXPECT_EQ(change.change, QStringLiteral("changed"));
            EXPECT_EQ(change.leftVersion, QStringLiteral("2.0"));
            EXPECT_EQ(change.rightVersion, QStringLiteral("2.30"));
        }
    }
    EXPECT_TRUE(dependencyChanged);
    EXPECT_FALSE(report.portabilityChanges.isEmpty());

    const QString markdown = pkg::projectdiff::toMarkdown(report);
    EXPECT_TRUE(markdown.contains(QStringLiteral("# PKG Studio project diff")));
    EXPECT_TRUE(markdown.contains(QStringLiteral("## Dependency changes")));
    EXPECT_TRUE(markdown.contains(QStringLiteral("CMakeLists.txt")));
}

TEST(ProjectDiff, HandlesInvalidFoldersCancellationAndSameFolder)
{
    const pkg::projectdiff::DiffReport empty =
        pkg::projectdiff::compareProjects(QString(), QString());
    EXPECT_TRUE(empty.isEmpty());
    EXPECT_FALSE(empty.notes.isEmpty());

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const pkg::projectdiff::DiffReport same =
        pkg::projectdiff::compareProjects(dir.path(), dir.path());
    EXPECT_TRUE(same.isEmpty());
    EXPECT_FALSE(same.notes.isEmpty());

    pkg::CancelToken token;
    token.cancel();
    const pkg::projectdiff::DiffReport cancelled =
        pkg::projectdiff::compareProjects(dir.path(), QDir::tempPath(), token);
    EXPECT_TRUE(cancelled.cancelled);
}

// ---------------------------------------------------------------------------
// Plugin SDK
// ---------------------------------------------------------------------------

TEST(PluginSdk, ValidatesManifestsAndDependencies)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    writeFile(QDir(directory.path()).filePath(QStringLiteral("actions.json")),
              QByteArrayLiteral("{\"version\":1,\"actions\":[]}"));

    pkg::plugins::PluginManifest manifest;
    manifest.id = QStringLiteral("com.example.hello");
    manifest.name = QStringLiteral("Hello");
    manifest.version = QStringLiteral("1.2.3");
    manifest.apiVersion = QStringLiteral("1");
    manifest.entry = QStringLiteral("actions.json");
    manifest.permissions = {QStringLiteral("files.read")};

    const auto valid = pkg::plugins::validateManifest(manifest, directory.path());
    EXPECT_TRUE(valid.valid()) << valid.errors().join(QStringLiteral("; ")).toStdString();
    EXPECT_TRUE(valid.warnings().isEmpty());

    pkg::plugins::PluginManifest broken = manifest;
    broken.id = QStringLiteral("Bad ID");
    broken.version = QStringLiteral("1.2");
    broken.apiVersion = QStringLiteral("9");
    broken.entry = QStringLiteral("../escape.json");
    broken.permissions = {QStringLiteral("root.access")};
    const auto invalid = pkg::plugins::validateManifest(broken, directory.path());
    EXPECT_FALSE(invalid.valid());
    EXPECT_GE(invalid.errors().size(), 4);

    pkg::plugins::PluginManifest withDependency = manifest;
    pkg::plugins::PluginDependency dependency;
    dependency.id = QStringLiteral("com.example.base");
    dependency.minimumVersion = QStringLiteral("2.0.0");
    withDependency.dependencies.append(dependency);
    pkg::plugins::PluginManifest base;
    base.id = QStringLiteral("com.example.base");
    base.version = QStringLiteral("1.9.0");
    const auto tooOld = pkg::plugins::validateManifest(withDependency, directory.path(), {base});
    EXPECT_FALSE(tooOld.valid());
    base.version = QStringLiteral("2.1.0");
    EXPECT_TRUE(pkg::plugins::validateManifest(withDependency, directory.path(), {base}).valid());
    EXPECT_FALSE(pkg::plugins::validateManifest(withDependency, directory.path(), {}).valid());
}

TEST(PluginSdk, SemverAndExampleManifest)
{
    EXPECT_TRUE(pkg::plugins::isValidSemver(QStringLiteral("1.0.0")));
    EXPECT_TRUE(pkg::plugins::isValidSemver(QStringLiteral("10.20.30-beta.1+build")));
    EXPECT_FALSE(pkg::plugins::isValidSemver(QStringLiteral("1.0")));
    EXPECT_FALSE(pkg::plugins::isValidSemver(QStringLiteral("01.0.0")));
    EXPECT_EQ(pkg::plugins::compareSemver(QStringLiteral("1.0.0"), QStringLiteral("1.0.1")), -1);
    EXPECT_EQ(pkg::plugins::compareSemver(QStringLiteral("2.0.0"), QStringLiteral("1.9.9")), 1);
    EXPECT_EQ(pkg::plugins::compareSemver(QStringLiteral("1.2.3"), QStringLiteral("1.2.3")), 0);

    const QMap<QString, QString> files = pkg::plugins::examplePluginFiles();
    EXPECT_TRUE(files.contains(QStringLiteral("plugin.json")));
    EXPECT_TRUE(files.contains(QStringLiteral("actions.json")));
    QJsonParseError error{};
    const QJsonDocument document =
        QJsonDocument::fromJson(files.value(QStringLiteral("plugin.json")).toUtf8(), &error);
    EXPECT_EQ(error.error, QJsonParseError::NoError);
    const auto manifest = pkg::plugins::PluginManifest::fromJson(document.object());
    ASSERT_TRUE(manifest);
    EXPECT_TRUE(pkg::plugins::isValidPluginId(manifest.value().id));
    EXPECT_TRUE(pkg::plugins::isValidSemver(manifest.value().version));
}

TEST(PluginSdk, ExportsChecksummedBundleWithoutTouchingSource)
{
    QTemporaryDir source;
    QTemporaryDir destination;
    ASSERT_TRUE(source.isValid() && destination.isValid());
    writeFile(QDir(source.path()).filePath(QStringLiteral("plugin.json")),
              QByteArrayLiteral("{\"id\":\"com.example.hello\"}"));
    writeFile(QDir(source.path()).filePath(QStringLiteral("actions.json")),
              QByteArrayLiteral("{\"version\":1}"));
    writeFile(QDir(source.path()).filePath(QStringLiteral("data/notes.txt")),
              QByteArrayLiteral("hello\n"));
    const QString sourceBefore = readFile(QDir(source.path()).filePath(QStringLiteral("notes.txt")));

    pkg::plugins::PluginManifest manifest;
    manifest.id = QStringLiteral("com.example.hello");
    manifest.name = QStringLiteral("Hello");
    manifest.version = QStringLiteral("0.1.0");
    manifest.entry = QStringLiteral("actions.json");

    const auto exported = pkg::plugins::exportBundle(manifest, source.path(), destination.path());
    ASSERT_TRUE(exported) << exported.error().detail.toStdString();
    EXPECT_EQ(exported.value().files.size(), 3);
    EXPECT_TRUE(QFileInfo::exists(exported.value().checksumFile));
    const QString checksums = readFile(exported.value().checksumFile);
    EXPECT_TRUE(checksums.contains(QStringLiteral("actions.json")));
    EXPECT_TRUE(checksums.contains(QStringLiteral("data/notes.txt")));
    EXPECT_TRUE(QFileInfo::exists(
        QDir(exported.value().directory).filePath(QStringLiteral("manifest.json"))));
    EXPECT_EQ(readFile(QDir(source.path()).filePath(QStringLiteral("notes.txt"))), sourceBefore);

    // A second export into the same destination refuses to overwrite.
    const auto again = pkg::plugins::exportBundle(manifest, source.path(), destination.path());
    EXPECT_FALSE(again);

    pkg::plugins::PluginManifest escaping = manifest;
    escaping.entry = QStringLiteral("../outside.json");
    EXPECT_FALSE(pkg::plugins::exportBundle(escaping, source.path(), destination.path()));
}

// ---------------------------------------------------------------------------
// Test Center
// ---------------------------------------------------------------------------

TEST(TestCenter, DetectsOnlyPresentFrameworks)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    writeFile(QDir(directory.path()).filePath(QStringLiteral("CMakeLists.txt")),
              QByteArrayLiteral("cmake_minimum_required(VERSION 3.20)\n"
                                "project(Demo)\n"
                                "enable_testing()\n"
                                "find_package(GTest REQUIRED)\n"
                                "add_test(NAME smoke COMMAND demo)\n"));
    writeFile(QDir(directory.path()).filePath(QStringLiteral("package.json")),
              QByteArrayLiteral("{\"name\":\"demo\",\"scripts\":{\"test\":\"node test.js\"}}"));
    writeFile(QDir(directory.path()).filePath(QStringLiteral("Cargo.toml")),
              QByteArrayLiteral("[package]\nname=\"demo\"\nversion=\"0.1.0\"\n"));
    writeFile(QDir(directory.path()).filePath(QStringLiteral("tests/test_demo.py")),
              QByteArrayLiteral("def test_one():\n    assert True\n"));

    const pkg::gameforge::ProjectScan scan = pkg::gameforge::scanProject(directory.path());
    const QVector<pkg::devflow::TestFrameworkInfo> frameworks =
        pkg::devflow::detectTestFrameworks(directory.path(), scan);
    EXPECT_GE(frameworks.size(), 4);

    QSet<QString> names;
    for (const auto& info : frameworks) {
        names.insert(info.name);
        EXPECT_TRUE(info.detected);
        EXPECT_FALSE(info.evidence.isEmpty());
        if (!info.runnable)
            EXPECT_FALSE(info.unavailableReason.isEmpty());
    }
    EXPECT_TRUE(names.contains(QStringLiteral("CTest")));
    EXPECT_TRUE(names.contains(QStringLiteral("GoogleTest")));
    EXPECT_TRUE(names.contains(QStringLiteral("npm test")));
    EXPECT_TRUE(names.contains(QStringLiteral("cargo test")));
    EXPECT_TRUE(names.contains(QStringLiteral("pytest")));
}

TEST(TestCenter, ParsesCtestGoogleTestPytestAndCargoOutput)
{
    const QString ctest =
        QStringLiteral("1/2 Test #1: alpha ....................   Passed    0.01 sec\n"
                       "2/2 Test #2: beta .....................***Failed    0.02 sec\n"
                       "The following tests FAILED:\n\t2 - beta (Failed)\n");
    auto report = pkg::devflow::parseTestOutput(pkg::devflow::TestFramework::CTest, ctest, 8);
    EXPECT_TRUE(report.recognizedOutput);
    EXPECT_EQ(report.passed, 1);
    EXPECT_EQ(report.failed, 1);
    EXPECT_EQ(report.failureGroups.size(), 1);

    const QString gtest =
        QStringLiteral("[ RUN      ] Suite.A\n[       OK ] Suite.A (0 ms)\n"
                       "[ RUN      ] Suite.B\n"
                       "tests/demo_test.cpp:12: Failure\nExpected equality of these values\n"
                       "[  FAILED  ] Suite.B (0 ms)\n");
    report = pkg::devflow::parseTestOutput(pkg::devflow::TestFramework::GoogleTest, gtest, 1);
    EXPECT_EQ(report.passed, 1);
    EXPECT_EQ(report.failed, 1);
    ASSERT_EQ(report.cases.size(), 2);
    EXPECT_EQ(report.cases.at(1).file, QStringLiteral("tests/demo_test.cpp"));
    EXPECT_EQ(report.cases.at(1).line, 12);

    const QString pytest =
        QStringLiteral("FAILED tests/test_demo.py::test_one - assert 1 == 2\n"
                       "FAILED tests/test_demo.py::test_two - assert 2 == 3\n"
                       "=== 2 failed, 1 passed in 0.10s ===\n");
    report = pkg::devflow::parseTestOutput(pkg::devflow::TestFramework::PyTest, pytest, 1);
    EXPECT_EQ(report.failed, 2);
    EXPECT_TRUE(report.recognizedOutput);

    const QString cargo = QStringLiteral("running 2 tests\n"
                                         "test tests::it_works ... ok\n"
                                         "test tests::it_breaks ... FAILED\n");
    report = pkg::devflow::parseTestOutput(pkg::devflow::TestFramework::CargoTest, cargo, 101);
    EXPECT_EQ(report.passed, 1);
    EXPECT_EQ(report.failed, 1);

    const QString unknown = QStringLiteral("something happened\n");
    const auto unrecognized = pkg::devflow::parseTestOutput(
        pkg::devflow::TestFramework::NpmTest, unknown, 0);
    EXPECT_FALSE(unrecognized.recognizedOutput);
    EXPECT_EQ(unrecognized.total(), 0);
    EXPECT_FALSE(unrecognized.notes.isEmpty());
}

TEST(TestCenter, GroupsFailuresAndReportsRegressions)
{
    QVector<pkg::devflow::TestCaseResult> cases;
    for (const QString& name : {QStringLiteral("a"), QStringLiteral("b")}) {
        pkg::devflow::TestCaseResult result;
        result.name = name;
        result.status = pkg::devflow::TestStatus::Failed;
        result.message = QStringLiteral("tests/x.cpp:12: expected 1 got 2");
        cases.append(result);
    }
    const auto groups = pkg::devflow::groupTestFailures(cases);
    ASSERT_EQ(groups.size(), 1);
    EXPECT_EQ(groups.first().count, 2);
    EXPECT_FALSE(groups.first().fingerprint.contains(QStringLiteral("x.cpp")));

    pkg::devflow::TestRunRecord previous;
    previous.id = QStringLiteral("1");
    previous.framework = QStringLiteral("CTest");
    previous.failureFingerprints = {QStringLiteral("old failure")};
    pkg::devflow::TestRunRecord current = previous;
    current.id = QStringLiteral("2");
    current.failureFingerprints = {QStringLiteral("new failure")};
    const QStringList regression = pkg::devflow::regressionSummary(previous, current);
    EXPECT_EQ(regression.size(), 2);
    EXPECT_TRUE(regression.join(QStringLiteral(" ")).contains(QStringLiteral("New failure")));
    EXPECT_TRUE(regression.join(QStringLiteral(" ")).contains(QStringLiteral("No longer")));

    const QString markdown =
        pkg::devflow::testReportMarkdown(current, pkg::devflow::TestOutputReport{},
                                         QStringLiteral("raw output"));
    EXPECT_TRUE(markdown.contains(QStringLiteral("# Test run report")));
    EXPECT_TRUE(markdown.contains(QStringLiteral("CTest")));
    EXPECT_TRUE(markdown.contains(QStringLiteral("raw output")));

    const QJsonObject json = current.toJson();
    const pkg::devflow::TestRunRecord roundTrip =
        pkg::devflow::TestRunRecord::fromJson(json);
    EXPECT_EQ(roundTrip.framework, QStringLiteral("CTest"));
    EXPECT_EQ(roundTrip.failureFingerprints, current.failureFingerprints);
}

TEST(TestCenter, RunsARealProcessAndCancelsIt)
{
    pkg::devflow::TestFrameworkInfo info;
    info.framework = pkg::devflow::TestFramework::Unknown;
    info.name = QStringLiteral("echo");
    info.detected = true;
    info.runnable = true;
    info.program = QStringLiteral("/bin/echo");
    info.arguments = {QStringLiteral("orbit-runner")};
    info.workingDirectory = QDir::tempPath();

    pkg::devflow::TestRunner runner;
    QString output;
    bool finished = false;
    bool cancelled = false;
    QObject::connect(&runner, &pkg::devflow::TestRunner::outputReceived,
                     [&output](const QString& text) { output += text; });
    QObject::connect(&runner, &pkg::devflow::TestRunner::finished,
                     [&finished, &cancelled](int, QProcess::ExitStatus, const QString&,
                                             bool wasCancelled) {
                         finished = true;
                         cancelled = wasCancelled;
                     });
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&runner, &pkg::devflow::TestRunner::finished, &loop, &QEventLoop::quit);
    EXPECT_TRUE(runner.start(info));
    timeout.start(8000);
    loop.exec();
    EXPECT_TRUE(finished);
    EXPECT_FALSE(cancelled);
    EXPECT_TRUE(output.contains(QStringLiteral("orbit-runner")));

    // Cancellation terminates the process and reports it as cancelled.
    pkg::devflow::TestFrameworkInfo sleeper = info;
    sleeper.program = QStringLiteral("/bin/sleep");
    sleeper.arguments = {QStringLiteral("20")};
    finished = false;
    cancelled = false;
    QEventLoop cancelLoop;
    QTimer cancelTimeout;
    cancelTimeout.setSingleShot(true);
    QObject::connect(&cancelTimeout, &QTimer::timeout, &cancelLoop, &QEventLoop::quit);
    QObject::connect(&runner, &pkg::devflow::TestRunner::finished, &cancelLoop,
                     &QEventLoop::quit);
    EXPECT_TRUE(runner.start(sleeper));
    QTimer::singleShot(120, &runner, &pkg::devflow::TestRunner::cancel);
    cancelTimeout.start(8000);
    cancelLoop.exec();
    EXPECT_TRUE(finished);
    EXPECT_TRUE(cancelled);
    EXPECT_FALSE(runner.isRunning());
}

// ---------------------------------------------------------------------------
// AI patch review
// ---------------------------------------------------------------------------

TEST(AiPatchReview, ExtractsFileProposalAndBuildsDiff)
{
    const pkg::FileProposal proposal = pkg::extractFileProposal(
        QStringLiteral("Here is the file:\n```pkgstudio-file\nhello world\n```\nDone."));
    ASSERT_TRUE(proposal.found);
    EXPECT_TRUE(proposal.bytes.contains("hello world"));

    const pkg::FileProposal missing =
        pkg::extractFileProposal(QStringLiteral("No fence here."));
    EXPECT_FALSE(missing.found);
    EXPECT_FALSE(missing.note.isEmpty());

    const QByteArray before = QByteArrayLiteral("alpha\nbeta\ngamma\n");
    const QByteArray same = QByteArrayLiteral("alpha\nBETA\ngamma\n");
    const QByteArray longer = QByteArrayLiteral("alpha\nbeta\ngamma\ndelta\n");
    EXPECT_TRUE(pkg::ui::PatchReviewDialog::canApplyInPlace(before, same));
    EXPECT_FALSE(pkg::ui::PatchReviewDialog::canApplyInPlace(before, longer));

    const QString diff = pkg::ui::PatchReviewDialog::diffText(before, same);
    EXPECT_TRUE(diff.contains(QStringLiteral("-beta")));
    EXPECT_TRUE(diff.contains(QStringLiteral("+BETA")));
    EXPECT_TRUE(diff.contains(QStringLiteral("--- current")));
}

TEST(OrbitFiles, DocumentationExists)
{
    const QString root = QStringLiteral(PKGSTUDIO_SOURCE_DIR);
    EXPECT_TRUE(QFileInfo::exists(QDir(root).filePath(QStringLiteral("docs/PLUGIN-SDK.md"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(root).filePath(QStringLiteral("docs/ORBIT.md"))));
}
