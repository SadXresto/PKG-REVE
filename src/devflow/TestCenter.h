// Test & Debug Center: framework detection, real test execution, output
// parsing and failure grouping.
//
// Only frameworks that are actually detected in the project are offered. A
// detected framework without a runnable command stays visible with the reason,
// so the UI never shows a button that cannot work.
#pragma once

#include "core/CancelToken.h"
#include "core/gameforge/GameProject.h"

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>

class QTimer;

namespace pkg::devflow {

enum class TestFramework { CTest, GoogleTest, Catch2, PyTest, NpmTest, CargoTest, Unknown };

[[nodiscard]] QString testFrameworkName(TestFramework framework);
[[nodiscard]] QString testFrameworkId(TestFramework framework);

struct TestFrameworkInfo {
    TestFramework framework = TestFramework::Unknown;
    QString name;
    bool detected = false;
    /// Bounded evidence: the file (and line) that made detection true.
    QString evidence;
    QString program;
    QStringList arguments;
    QString workingDirectory;
    bool runnable = false;
    /// Why the framework cannot be run, empty when it can.
    QString unavailableReason;
    QString detail;

    [[nodiscard]] QString displayCommand() const;
};

/// Inspects the project tree for test support. Reads only bounded text files.
[[nodiscard]] QVector<TestFrameworkInfo> detectTestFrameworks(
    const QString& root, const gameforge::ProjectScan& scan);

enum class TestStatus { Passed, Failed, Skipped, Error, Unknown };

[[nodiscard]] QString testStatusName(TestStatus status);

struct TestCaseResult {
    QString name;
    TestStatus status = TestStatus::Unknown;
    QString message;
    QString file;
    int line = 0;
    QString framework;
};

struct TestFailureGroup {
    QString fingerprint;
    QString message;
    int count = 0;
    QStringList cases;
};

struct TestOutputReport {
    QVector<TestCaseResult> cases;
    QVector<TestFailureGroup> failureGroups;
    QStringList notes;
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    int unknown = 0;
    bool recognizedOutput = false;

    [[nodiscard]] int total() const { return static_cast<int>(cases.size()); }
};

/// Parses framework output. Unrecognised output yields no invented cases.
[[nodiscard]] TestOutputReport parseTestOutput(TestFramework framework, const QString& output,
                                               int exitCode = 0);

/// Grouping helper (also used for saved runs).
[[nodiscard]] QVector<TestFailureGroup> groupTestFailures(
    const QVector<TestCaseResult>& cases);

struct TestRunRecord {
    QString id;
    QString projectPath;
    QString framework;
    QString command;
    QDateTime startedAt;
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    int exitCode = 0;
    bool cancelled = false;
    QString summary;
    /// Failure fingerprints, for regression comparison between runs.
    QStringList failureFingerprints;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static TestRunRecord fromJson(const QJsonObject& object);
};

/// Compares a run with the previous run of the same project/framework and
/// names problems that are new or fixed. Empty sentences when nothing changed.
[[nodiscard]] QStringList regressionSummary(const TestRunRecord& previous,
                                            const TestRunRecord& current);

[[nodiscard]] QString testReportMarkdown(const TestRunRecord& record,
                                         const TestOutputReport& report,
                                         const QString& rawOutput);

class TestRunner : public QObject {
    Q_OBJECT

public:
    explicit TestRunner(QObject* parent = nullptr);
    ~TestRunner() override;

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] QString currentCommand() const { return m_display; }

    /// Starts the detected command. Returns false (with `error`) when the
    /// framework is not runnable.
    bool start(const TestFrameworkInfo& info, QString* error = nullptr);
    void cancel();

signals:
    void started(const QString& command);
    void outputReceived(const QString& text);
    void finished(int exitCode, QProcess::ExitStatus status, const QString& command,
                  bool cancelled);

private:
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onError(QProcess::ProcessError error);

    QProcess* m_process = nullptr;
    QTimer* m_killTimer = nullptr;
    QString m_display;
    bool m_cancelled = false;
};

}  // namespace pkg::devflow
