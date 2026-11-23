#include "devflow/TestCenter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace pkg::devflow {
namespace {

constexpr qint64 kReadLimit = 256 * 1024;

QString readText(const QString& path, qint64 limit = kReadLimit)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.read(limit));
}

QString firstMatchEvidence(const QString& root, const QString& relative,
                           const QStringList& needles)
{
    const QString text = readText(QDir(root).filePath(relative));
    if (text.isEmpty())
        return {};
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        for (const QString& needle : needles) {
            if (lines.at(i).contains(needle, Qt::CaseInsensitive)) {
                return QStringLiteral("%1:%2  %3")
                    .arg(relative)
                    .arg(i + 1)
                    .arg(lines.at(i).trimmed().left(120));
            }
        }
    }
    return {};
}

QString findCtestDirectory(const QString& root)
{
    QStringList pending{root};
    int visited = 0;
    while (!pending.isEmpty() && visited < 4000) {
        const QString directory = pending.takeFirst();
        const QDir dir(directory);
        if (QFileInfo::exists(dir.filePath(QStringLiteral("CTestTestfile.cmake"))))
            return directory;
        const QFileInfoList entries =
            dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& entry : entries) {
            ++visited;
            const QString name = entry.fileName();
            if (name == QLatin1String(".git") || name == QLatin1String(".svn")
                || name == QLatin1String("node_modules"))
                continue;
            if (QFileInfo::exists(QDir(entry.absoluteFilePath())
                                      .filePath(QStringLiteral("CTestTestfile.cmake"))))
                return entry.absoluteFilePath();
            // Two levels below the project root is enough for conventional
            // build/, build/Debug and out/build/<preset> layouts.
            if (QDir::cleanPath(entry.absoluteFilePath()) != QDir::cleanPath(root)) {
                const QString relative = QDir(root).relativeFilePath(entry.absoluteFilePath());
                if (relative.count(QLatin1Char('/')) < 2)
                    pending.append(entry.absoluteFilePath());
            }
        }
    }
    return {};
}

QStringList buildFilesNamed(const gameforge::ProjectScan& scan, const QStringList& suffixes)
{
    QStringList result;
    for (const QString& file : scan.buildFiles) {
        const QString lowered = file.toLower();
        for (const QString& suffix : suffixes) {
            if (lowered.endsWith(suffix)) {
                result.append(file);
                break;
            }
        }
    }
    return result;
}

QString interpreterFor(const QStringList& names)
{
    for (const QString& name : names) {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty())
            return path;
    }
    return {};
}

int firstNumber(const QString& text, const QString& pattern)
{
    const QRegularExpression expression(pattern);
    const auto match = expression.match(text);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

QString normalizedMessage(const QString& message)
{
    QString text = message.simplified().toLower();
    const QRegularExpression location(QStringLiteral("^[^\\s:]+:\\d+(:\\d+)?:?\\s*"));
    text.remove(location);
    return text.left(240);
}

void addCase(TestOutputReport& report, const TestCaseResult& result)
{
    report.cases.append(result);
    switch (result.status) {
    case TestStatus::Passed:
        ++report.passed;
        break;
    case TestStatus::Failed:
    case TestStatus::Error:
        ++report.failed;
        break;
    case TestStatus::Skipped:
        ++report.skipped;
        break;
    case TestStatus::Unknown:
        ++report.unknown;
        break;
    }
}

void parseCtest(const QString& output, TestOutputReport& report)
{
    const QRegularExpression line(
        QStringLiteral("^\\s*\\d+/\\d+\\s+Test\\s+#\\d+:\\s+(.+?)\\s+\\.{2,}\\s*"
                       "(\\*+)?\\s*(Passed|Failed|Timeout|Not Run|Skipped)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression plain(
        QStringLiteral("^\\s*Test\\s+#\\d+:\\s+(.+?)\\s*\\.{2,}(\\*+)?\\s*"
                       "(Passed|Failed|Timeout|Not Run|Skipped)"),
        QRegularExpression::CaseInsensitiveOption);
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString& text : lines) {
        auto match = line.match(text);
        if (!match.hasMatch())
            match = plain.match(text);
        if (!match.hasMatch())
            continue;
        TestCaseResult result;
        result.name = match.captured(1).trimmed();
        result.framework = QStringLiteral("CTest");
        const QString status = match.captured(3).toLower();
        if (status == QLatin1String("passed"))
            result.status = TestStatus::Passed;
        else if (status == QLatin1String("skipped") || status == QLatin1String("not run"))
            result.status = TestStatus::Skipped;
        else
            result.status = TestStatus::Failed;
        addCase(report, result);
    }
    report.recognizedOutput = !report.cases.isEmpty();
}

void parseGtest(const QString& output, TestOutputReport& report)
{
    const QRegularExpression ok(
        QStringLiteral("^\\[\\s+OK\\s+\\]\\s+(\\S+)\\s+\\("));
    const QRegularExpression failed(
        QStringLiteral("^\\[\\s+FAILED\\s+\\]\\s+(\\S+)(?:\\s+\\(.*\\))?\\s*$"));
    const QRegularExpression skipped(
        QStringLiteral("^\\[\\s+SKIPPED\\s+\\]\\s+(\\S+)"));
    const QStringList lines = output.split(QLatin1Char('\n'));
    QString lastFailure;
    for (const QString& text : lines) {
        auto match = ok.match(text);
        if (match.hasMatch()) {
            TestCaseResult result;
            result.name = match.captured(1);
            result.status = TestStatus::Passed;
            result.framework = QStringLiteral("GoogleTest");
            addCase(report, result);
            lastFailure.clear();
            continue;
        }
        match = skipped.match(text);
        if (match.hasMatch()) {
            TestCaseResult result;
            result.name = match.captured(1);
            result.status = TestStatus::Skipped;
            result.framework = QStringLiteral("GoogleTest");
            addCase(report, result);
            continue;
        }
        match = failed.match(text);
        if (match.hasMatch()) {
            TestCaseResult result;
            result.name = match.captured(1);
            result.status = TestStatus::Failed;
            result.message = lastFailure;
            const QRegularExpression location(
                QStringLiteral("([\\w./\\\\-]+\\.(?:cpp|cc|cxx|h|hpp)):(\\d+)"));
            const auto locationMatch = location.match(lastFailure);
            if (locationMatch.hasMatch()) {
                result.file = locationMatch.captured(1);
                result.line = locationMatch.captured(2).toInt();
            }
            result.framework = QStringLiteral("GoogleTest");
            addCase(report, result);
            lastFailure.clear();
            continue;
        }
        if (text.contains(QLatin1String(" Failure"))
            || text.contains(QLatin1String(": Failure"))) {
            lastFailure = text.trimmed();
        }
    }
    report.recognizedOutput = !report.cases.isEmpty();
}

void parseCatch2(const QString& output, TestOutputReport& report)
{
    const QRegularExpression failure(
        QStringLiteral("^(.+?)\\((\\d+)\\):\\s*FAILED:?\\s*(.*)$"));
    const QRegularExpression assertion(
        QStringLiteral("^(.+?)\\((\\d+)\\):\\s*(?:error|warning|fatal error):\\s*(.*)$"));
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString& text : lines) {
        auto match = failure.match(text);
        QString message;
        if (!match.hasMatch())
            match = assertion.match(text);
        if (!match.hasMatch())
            continue;
        TestCaseResult result;
        result.name = QStringLiteral("%1:%2").arg(match.captured(1), match.captured(2));
        result.file = match.captured(1);
        result.line = match.captured(2).toInt();
        result.message = match.captured(3).trimmed();
        result.status = TestStatus::Failed;
        result.framework = QStringLiteral("Catch2");
        addCase(report, result);
    }
    if (!report.cases.isEmpty())
        report.recognizedOutput = true;
    if (!report.recognizedOutput && output.contains(QLatin1String("All tests passed")))
        report.recognizedOutput = true;
}

void parsePytest(const QString& output, TestOutputReport& report)
{
    const QRegularExpression failed(
        QStringLiteral("^FAILED\\s+(\\S+?)(?:\\s+-\\s+(.*))?$"));
    const QRegularExpression passed(
        QStringLiteral("^(\\S+?::\\S+)\\s+PASSED(?:\\s+\\[.*\\])?\\s*$"));
    const QRegularExpression skipped(
        QStringLiteral("^(\\S+?::\\S+)\\s+SKIPPED(?:\\s+\\[.*\\])?\\s*$"));
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString& text : lines) {
        auto match = failed.match(text.trimmed());
        if (match.hasMatch()) {
            TestCaseResult result;
            result.name = match.captured(1);
            result.message = match.captured(2);
            result.status = TestStatus::Failed;
            result.framework = QStringLiteral("pytest");
            const int separator = result.name.indexOf(QStringLiteral("::"));
            if (separator > 0)
                result.file = result.name.left(separator);
            addCase(report, result);
            continue;
        }
        match = passed.match(text.trimmed());
        if (match.hasMatch()) {
            TestCaseResult result;
            result.name = match.captured(1);
            result.status = TestStatus::Passed;
            result.framework = QStringLiteral("pytest");
            addCase(report, result);
            continue;
        }
        match = skipped.match(text.trimmed());
        if (match.hasMatch()) {
            TestCaseResult result;
            result.name = match.captured(1);
            result.status = TestStatus::Skipped;
            result.framework = QStringLiteral("pytest");
            addCase(report, result);
        }
    }
    if (output.contains(QStringLiteral("=== ")) && output.contains(QLatin1String("passed")))
        report.recognizedOutput = true;
    if (!report.cases.isEmpty())
        report.recognizedOutput = true;
}

void parseCargo(const QString& output, TestOutputReport& report)
{
    const QRegularExpression line(
        QStringLiteral("^test\\s+(\\S+)\\s+\\.\\.\\.\\s+(ok|FAILED|ignored)"));
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString& text : lines) {
        const auto match = line.match(text.trimmed());
        if (!match.hasMatch())
            continue;
        TestCaseResult result;
        result.name = match.captured(1);
        result.framework = QStringLiteral("cargo test");
        const QString status = match.captured(2);
        result.status = status == QLatin1String("ok")
            ? TestStatus::Passed
            : (status == QLatin1String("ignored") ? TestStatus::Skipped : TestStatus::Failed);
        addCase(report, result);
    }
    report.recognizedOutput = !report.cases.isEmpty();
}

void parseNpm(const QString& output, TestOutputReport& report, int exitCode)
{
    const QRegularExpression line(
        QStringLiteral("^\\s*(?:\u2713|\u2714|\u221A|ok)\\s+(.+)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression failing(
        QStringLiteral("^\\s*(?:\u2717|\u2718|\u00D7|not ok|FAIL)\\s+(.+)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString& text : lines) {
        auto match = line.match(text);
        if (match.hasMatch()) {
            TestCaseResult result;
            result.name = match.captured(1).trimmed();
            result.status = TestStatus::Passed;
            result.framework = QStringLiteral("npm test");
            addCase(report, result);
            continue;
        }
        match = failing.match(text);
        if (match.hasMatch()) {
            TestCaseResult result;
            result.name = match.captured(1).trimmed();
            result.status = TestStatus::Failed;
            result.framework = QStringLiteral("npm test");
            addCase(report, result);
        }
    }
    if (!report.cases.isEmpty()) {
        report.recognizedOutput = true;
    } else if (exitCode >= 0) {
        report.notes.append(QStringLiteral(
            "The test command finished with exit code %1; its output did not name individual "
            "tests, so no per-test result is claimed.")
                               .arg(exitCode));
    }
}

}  // namespace

QString testFrameworkName(TestFramework framework)
{
    switch (framework) {
    case TestFramework::CTest:
        return QStringLiteral("CTest");
    case TestFramework::GoogleTest:
        return QStringLiteral("GoogleTest");
    case TestFramework::Catch2:
        return QStringLiteral("Catch2");
    case TestFramework::PyTest:
        return QStringLiteral("pytest");
    case TestFramework::NpmTest:
        return QStringLiteral("npm test");
    case TestFramework::CargoTest:
        return QStringLiteral("cargo test");
    case TestFramework::Unknown:
        return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

QString testFrameworkId(TestFramework framework)
{
    switch (framework) {
    case TestFramework::CTest:
        return QStringLiteral("ctest");
    case TestFramework::GoogleTest:
        return QStringLiteral("gtest");
    case TestFramework::Catch2:
        return QStringLiteral("catch2");
    case TestFramework::PyTest:
        return QStringLiteral("pytest");
    case TestFramework::NpmTest:
        return QStringLiteral("npm");
    case TestFramework::CargoTest:
        return QStringLiteral("cargo");
    case TestFramework::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString TestFrameworkInfo::displayCommand() const
{
    return QStringList(QStringList{program} + arguments).join(QLatin1Char(' '));
}

QVector<TestFrameworkInfo> detectTestFrameworks(const QString& root,
                                                const gameforge::ProjectScan& scan)
{
    QVector<TestFrameworkInfo> result;
    const QString ctestDirectory = findCtestDirectory(root);
    const QString ctest = QStandardPaths::findExecutable(QStringLiteral("ctest"));

    bool cmakeCtest = false;
    bool cmakeGtest = false;
    bool cmakeCatch = false;
    QString ctestEvidence;
    QString gtestEvidence;
    QString catchEvidence;
    for (const QString& file : buildFilesNamed(scan, {QStringLiteral("cmakelists.txt"),
                                                      QStringLiteral(".cmake")})) {
        if (ctestEvidence.isEmpty()) {
            ctestEvidence = firstMatchEvidence(root, file,
                                               {QStringLiteral("enable_testing"),
                                                QStringLiteral("add_test"),
                                                QStringLiteral("include(CTest"),
                                                QStringLiteral("include (CTest")});
            cmakeCtest = !ctestEvidence.isEmpty();
        }
        if (gtestEvidence.isEmpty()) {
            gtestEvidence = firstMatchEvidence(root, file,
                                               {QStringLiteral("gtest"),
                                                QStringLiteral("googletest"),
                                                QStringLiteral("GTest")});
            cmakeGtest = !gtestEvidence.isEmpty();
        }
        if (catchEvidence.isEmpty()) {
            catchEvidence = firstMatchEvidence(root, file,
                                               {QStringLiteral("Catch2"),
                                                QStringLiteral("catch2")});
            cmakeCatch = !catchEvidence.isEmpty();
        }
    }

    if (cmakeCtest || !ctestDirectory.isEmpty()) {
        TestFrameworkInfo info;
        info.framework = TestFramework::CTest;
        info.name = testFrameworkName(info.framework);
        info.detected = true;
        info.evidence = !ctestEvidence.isEmpty()
            ? ctestEvidence
            : QStringLiteral("CTestTestfile.cmake in %1").arg(ctestDirectory);
        info.program = ctest;
        info.arguments = {QStringLiteral("--output-on-failure")};
        info.workingDirectory = ctestDirectory.isEmpty() ? root : ctestDirectory;
        info.runnable = !ctest.isEmpty() && !ctestDirectory.isEmpty();
        if (ctest.isEmpty())
            info.unavailableReason = QStringLiteral("The ctest executable is not on PATH.");
        else if (ctestDirectory.isEmpty())
            info.unavailableReason =
                QStringLiteral("No configured build folder with CTestTestfile.cmake was found. "
                               "Configure the project first.");
        info.detail = QStringLiteral("Runs the CTest suite registered by the build files.");
        result.append(info);
    }
    if (cmakeGtest) {
        TestFrameworkInfo info;
        info.framework = TestFramework::GoogleTest;
        info.name = testFrameworkName(info.framework);
        info.detected = true;
        info.evidence = gtestEvidence;
        info.workingDirectory = ctestDirectory.isEmpty() ? root : ctestDirectory;
        info.runnable = !ctest.isEmpty() && !ctestDirectory.isEmpty();
        info.program = ctest;
        info.arguments = {QStringLiteral("--output-on-failure")};
        if (!info.runnable)
            info.unavailableReason =
                QStringLiteral("GoogleTest is registered through CMake/CTest; a configured "
                               "build folder is required to run it.");
        info.detail = QStringLiteral("GoogleTest binaries are launched through CTest.");
        result.append(info);
    }
    if (cmakeCatch) {
        TestFrameworkInfo info;
        info.framework = TestFramework::Catch2;
        info.name = testFrameworkName(info.framework);
        info.detected = true;
        info.evidence = catchEvidence;
        info.workingDirectory = ctestDirectory.isEmpty() ? root : ctestDirectory;
        info.runnable = !ctest.isEmpty() && !ctestDirectory.isEmpty();
        info.program = ctest;
        info.arguments = {QStringLiteral("--output-on-failure")};
        if (!info.runnable)
            info.unavailableReason =
                QStringLiteral("Catch2 is registered through CMake/CTest; a configured build "
                               "folder is required to run it.");
        info.detail = QStringLiteral("Catch2 binaries are launched through CTest.");
        result.append(info);
    }

    const QString python = interpreterFor({QStringLiteral("python3"), QStringLiteral("python")});
    QString pytestEvidence;
    for (const QString& file : {QStringLiteral("pytest.ini"), QStringLiteral("pyproject.toml"),
                                QStringLiteral("setup.cfg"), QStringLiteral("tox.ini")}) {
        if (!QFileInfo::exists(QDir(root).filePath(file)))
            continue;
        pytestEvidence = firstMatchEvidence(root, file, {QStringLiteral("pytest")});
        if (!pytestEvidence.isEmpty())
            break;
    }
    if (pytestEvidence.isEmpty()) {
        for (const gameforge::ProjectFile& file : scan.files) {
            const QString name = QFileInfo(file.relativePath).fileName();
            if (name.startsWith(QLatin1String("test_")) && name.endsWith(QLatin1String(".py"))) {
                pytestEvidence = file.relativePath;
                break;
            }
        }
    }
    if (!pytestEvidence.isEmpty()) {
        TestFrameworkInfo info;
        info.framework = TestFramework::PyTest;
        info.name = testFrameworkName(info.framework);
        info.detected = true;
        info.evidence = pytestEvidence;
        info.program = python;
        info.arguments = {QStringLiteral("-m"), QStringLiteral("pytest"), QStringLiteral("-q")};
        info.workingDirectory = root;
        info.runnable = !python.isEmpty();
        if (python.isEmpty())
            info.unavailableReason = QStringLiteral("No python3 or python interpreter was found "
                                                    "on PATH.");
        info.detail = QStringLiteral("Runs pytest, which reports pass/fail per test.");
        result.append(info);
    }

    const QString npm = QStandardPaths::findExecutable(QStringLiteral("npm"));
    const QString packageJson = QDir(root).filePath(QStringLiteral("package.json"));
    if (QFileInfo::exists(packageJson)) {
        QFile file(packageJson);
        QString testScript;
        if (file.open(QIODevice::ReadOnly)) {
            const QString text = QString::fromUtf8(file.read(kReadLimit));
            const QRegularExpression script(
                QStringLiteral("\"test\"\\s*:\\s*\"([^\"]*)\""));
            const auto match = script.match(text);
            if (match.hasMatch())
                testScript = match.captured(1);
        }
        if (!testScript.isEmpty()) {
            TestFrameworkInfo info;
            info.framework = TestFramework::NpmTest;
            info.name = testFrameworkName(info.framework);
            info.detected = true;
            info.evidence = QStringLiteral("package.json scripts.test: %1").arg(testScript);
            info.program = npm;
            info.arguments = {QStringLiteral("test"), QStringLiteral("--silent")};
            info.workingDirectory = root;
            info.runnable = !npm.isEmpty();
            if (npm.isEmpty())
                info.unavailableReason = QStringLiteral("npm is not on PATH.");
            info.detail = QStringLiteral("Runs the project's own npm test script.");
            result.append(info);
        }
    }

    const QString cargo = QStandardPaths::findExecutable(QStringLiteral("cargo"));
    const QString cargoToml = QDir(root).filePath(QStringLiteral("Cargo.toml"));
    if (QFileInfo::exists(cargoToml)) {
        TestFrameworkInfo info;
        info.framework = TestFramework::CargoTest;
        info.name = testFrameworkName(info.framework);
        info.detected = true;
        info.evidence = firstMatchEvidence(root, QStringLiteral("Cargo.toml"),
                                           {QStringLiteral("[package]"),
                                            QStringLiteral("[workspace]"),
                                            QStringLiteral("[[test]]")});
        if (info.evidence.isEmpty())
            info.evidence = QStringLiteral("Cargo.toml");
        info.program = cargo;
        info.arguments = {QStringLiteral("test"), QStringLiteral("--quiet")};
        info.workingDirectory = root;
        info.runnable = !cargo.isEmpty();
        if (cargo.isEmpty())
            info.unavailableReason = QStringLiteral("cargo is not on PATH.");
        info.detail = QStringLiteral("Runs cargo's unit, integration and doc tests.");
        result.append(info);
    }

    return result;
}

QString testStatusName(TestStatus status)
{
    switch (status) {
    case TestStatus::Passed:
        return QStringLiteral("passed");
    case TestStatus::Failed:
        return QStringLiteral("failed");
    case TestStatus::Skipped:
        return QStringLiteral("skipped");
    case TestStatus::Error:
        return QStringLiteral("error");
    case TestStatus::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

TestOutputReport parseTestOutput(TestFramework framework, const QString& output, int exitCode)
{
    TestOutputReport report;
    switch (framework) {
    case TestFramework::CTest:
        parseCtest(output, report);
        break;
    case TestFramework::GoogleTest:
        parseGtest(output, report);
        break;
    case TestFramework::Catch2:
        parseCatch2(output, report);
        break;
    case TestFramework::PyTest:
        parsePytest(output, report);
        break;
    case TestFramework::CargoTest:
        parseCargo(output, report);
        break;
    case TestFramework::NpmTest:
        parseNpm(output, report, exitCode);
        break;
    case TestFramework::Unknown:
        break;
    }
    report.failureGroups = groupTestFailures(report.cases);
    if (!report.recognizedOutput && !output.trimmed().isEmpty()) {
        report.notes.append(QStringLiteral("The output did not match a known %1 format.")
                                .arg(testFrameworkName(framework)));
    }
    return report;
}

QVector<TestFailureGroup> groupTestFailures(const QVector<TestCaseResult>& cases)
{
    QVector<TestFailureGroup> groups;
    QHash<QString, int> indexByFingerprint;
    for (const TestCaseResult& result : cases) {
        if (result.status != TestStatus::Failed && result.status != TestStatus::Error)
            continue;
        const QString normalized = normalizedMessage(result.message);
        const QString fingerprint = normalized.isEmpty()
            ? QStringLiteral("case:%1").arg(result.name)
            : normalized;
        auto it = indexByFingerprint.find(fingerprint);
        if (it == indexByFingerprint.end()) {
            TestFailureGroup group;
            group.fingerprint = fingerprint;
            group.message = result.message.isEmpty() ? result.name : result.message;
            group.count = 1;
            group.cases.append(result.name);
            indexByFingerprint.insert(fingerprint, static_cast<int>(groups.size()));
            groups.append(group);
        } else {
            TestFailureGroup& group = groups[it.value()];
            ++group.count;
            group.cases.append(result.name);
        }
    }
    std::sort(groups.begin(), groups.end(), [](const TestFailureGroup& a,
                                               const TestFailureGroup& b) {
        if (a.count != b.count)
            return a.count > b.count;
        return a.message < b.message;
    });
    return groups;
}

QJsonObject TestRunRecord::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("projectPath"), projectPath);
    object.insert(QStringLiteral("framework"), framework);
    object.insert(QStringLiteral("command"), command);
    object.insert(QStringLiteral("startedAt"), startedAt.toString(Qt::ISODate));
    object.insert(QStringLiteral("passed"), passed);
    object.insert(QStringLiteral("failed"), failed);
    object.insert(QStringLiteral("skipped"), skipped);
    object.insert(QStringLiteral("exitCode"), exitCode);
    object.insert(QStringLiteral("cancelled"), cancelled);
    object.insert(QStringLiteral("summary"), summary);
    QJsonArray fingerprints;
    for (const QString& fingerprint : failureFingerprints)
        fingerprints.append(fingerprint);
    object.insert(QStringLiteral("failureFingerprints"), fingerprints);
    return object;
}

TestRunRecord TestRunRecord::fromJson(const QJsonObject& object)
{
    TestRunRecord record;
    record.id = object.value(QStringLiteral("id")).toString();
    record.projectPath = object.value(QStringLiteral("projectPath")).toString();
    record.framework = object.value(QStringLiteral("framework")).toString();
    record.command = object.value(QStringLiteral("command")).toString();
    record.startedAt = QDateTime::fromString(object.value(QStringLiteral("startedAt")).toString(),
                                            Qt::ISODate);
    record.passed = object.value(QStringLiteral("passed")).toInt();
    record.failed = object.value(QStringLiteral("failed")).toInt();
    record.skipped = object.value(QStringLiteral("skipped")).toInt();
    record.exitCode = object.value(QStringLiteral("exitCode")).toInt();
    record.cancelled = object.value(QStringLiteral("cancelled")).toBool();
    record.summary = object.value(QStringLiteral("summary")).toString();
    const QJsonArray fingerprints = object.value(QStringLiteral("failureFingerprints")).toArray();
    for (const QJsonValue& value : fingerprints)
        record.failureFingerprints.append(value.toString());
    return record;
}

QStringList regressionSummary(const TestRunRecord& previous, const TestRunRecord& current)
{
    QStringList result;
    if (previous.id.isEmpty() || previous.framework != current.framework)
        return result;
    const QSet<QString> before(previous.failureFingerprints.begin(),
                               previous.failureFingerprints.end());
    const QSet<QString> after(current.failureFingerprints.begin(),
                              current.failureFingerprints.end());
    for (const QString& fingerprint : after) {
        if (!before.contains(fingerprint))
            result.append(QStringLiteral("New failure: %1").arg(fingerprint.left(160)));
    }
    for (const QString& fingerprint : before) {
        if (!after.contains(fingerprint))
            result.append(QStringLiteral("No longer failing: %1").arg(fingerprint.left(160)));
    }
    return result;
}

QString testReportMarkdown(const TestRunRecord& record, const TestOutputReport& report,
                           const QString& rawOutput)
{
    QString text;
    text += QStringLiteral("# Test run report\n\n");
    text += QStringLiteral("- Project: `%1`\n").arg(record.projectPath);
    text += QStringLiteral("- Framework: %1\n").arg(record.framework);
    text += QStringLiteral("- Command: `%1`\n").arg(record.command);
    text += QStringLiteral("- Started: %1\n").arg(record.startedAt.toString(Qt::ISODate));
    text += QStringLiteral("- Result: %1 passed, %2 failed, %3 skipped, exit code %4%5\n\n")
                .arg(record.passed)
                .arg(record.failed)
                .arg(record.skipped)
                .arg(record.exitCode)
                .arg(record.cancelled ? QStringLiteral(" (cancelled)") : QString());
    text += QStringLiteral("## Failures\n\n");
    if (report.failureGroups.isEmpty()) {
        text += QStringLiteral("No failure was reported in the parsed output.\n\n");
    } else {
        for (const TestFailureGroup& group : report.failureGroups) {
            text += QStringLiteral("### %1 (%2 case(s))\n\n%3\n\n")
                        .arg(group.message.left(200))
                        .arg(group.count)
                        .arg(group.cases.join(QStringLiteral(", ")));
        }
    }
    text += QStringLiteral("## Output\n\n```\n");
    text += rawOutput.left(20000);
    text += QStringLiteral("\n```\n");
    if (!report.notes.isEmpty()) {
        text += QStringLiteral("\n## Notes\n\n");
        for (const QString& note : report.notes)
            text += QStringLiteral("- %1\n").arg(note);
    }
    return text;
}

TestRunner::TestRunner(QObject* parent) : QObject(parent) {}

TestRunner::~TestRunner()
{
    if (m_process != nullptr) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

bool TestRunner::isRunning() const
{
    return m_process != nullptr
        && m_process->state() != QProcess::NotRunning;
}

bool TestRunner::start(const TestFrameworkInfo& info, QString* error)
{
    if (isRunning()) {
        if (error != nullptr)
            *error = QStringLiteral("A test run is already in progress.");
        return false;
    }
    if (!info.detected || !info.runnable || info.program.isEmpty()) {
        if (error != nullptr) {
            *error = info.unavailableReason.isEmpty()
                ? QStringLiteral("This framework cannot be run in this project.")
                : info.unavailableReason;
        }
        return false;
    }
    if (!QFileInfo(info.workingDirectory).isDir()) {
        if (error != nullptr)
            *error = QStringLiteral("The working directory does not exist: %1")
                         .arg(info.workingDirectory);
        return false;
    }

    m_cancelled = false;
    m_process = new QProcess(this);
    m_process->setProgram(info.program);
    m_process->setArguments(info.arguments);
    m_process->setWorkingDirectory(info.workingDirectory);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("CTEST_OUTPUT_ON_FAILURE"), QStringLiteral("1"));
    m_process->setProcessEnvironment(environment);
    m_display = info.displayCommand();

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        const QByteArray data = m_process->readAllStandardOutput();
        if (!data.isEmpty())
            emit outputReceived(QString::fromLocal8Bit(data));
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &TestRunner::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, &TestRunner::onError);

    emit started(m_display);
    m_process->start();
    return true;
}

void TestRunner::cancel()
{
    if (m_process == nullptr || m_process->state() == QProcess::NotRunning)
        return;
    m_cancelled = true;
    m_process->terminate();
    if (m_killTimer == nullptr) {
        m_killTimer = new QTimer(this);
        m_killTimer->setSingleShot(true);
        connect(m_killTimer, &QTimer::timeout, this, [this]() {
            if (m_process != nullptr && m_process->state() != QProcess::NotRunning)
                m_process->kill();
        });
    }
    m_killTimer->start(4000);
}

void TestRunner::onFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_killTimer != nullptr)
        m_killTimer->stop();
    const QString command = m_display;
    const bool cancelled = m_cancelled;
    if (m_process != nullptr) {
        const QByteArray data = m_process->readAllStandardOutput();
        if (!data.isEmpty())
            emit outputReceived(QString::fromLocal8Bit(data));
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_display.clear();
    m_cancelled = false;
    emit finished(exitCode, status, command, cancelled);
}

void TestRunner::onError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart && m_process != nullptr) {
        const QString command = m_display;
        m_process->deleteLater();
        m_process = nullptr;
        m_display.clear();
        emit finished(-1, QProcess::CrashExit, command, m_cancelled);
    }
}

}  // namespace pkg::devflow
