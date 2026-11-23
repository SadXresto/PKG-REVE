// DevFlow: the development workflow command center.
//
// The panel owns no project data of its own. It reads the detected project,
// stores profiles/tasks/snapshots/workflows in the local SQLite database and
// asks the host window to open files, restore layouts or run builds.
#pragma once

#include "core/gameforge/BuildConfig.h"
#include "core/gameforge/DependencyGraph.h"
#include "core/gameforge/GameProject.h"
#include "devflow/BuildRunner.h"
#include "devflow/DevFlowModels.h"
#include "devflow/DevFlowStore.h"
#include "devflow/ErrorIntelligence.h"
#include "devflow/ProjectHealth.h"
#include "devflow/WorkflowRunner.h"

#include <QFutureWatcher>
#include <QWidget>

#include <functional>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;

namespace pkg {
class Database;
}

namespace pkg::ui {

struct DevFlowHooks {
    std::function<QString()> projectPath;
    std::function<void(const QString&)> openFile;
    std::function<void(const QString&)> openProject;
    std::function<void(const QString&)> statusMessage;
    /// Captures the live workspace; the panel never guesses tab state.
    std::function<devflow::WorkspaceSnapshot()> captureSnapshot;
    std::function<void(const devflow::WorkspaceSnapshot&)> restoreSnapshot;
};

class DevFlowPanel : public QWidget {
    Q_OBJECT

public:
    explicit DevFlowPanel(Database& database, DevFlowHooks hooks, QWidget* parent = nullptr);
    ~DevFlowPanel() override;

    /// Selects a tab by id (dashboard, build, errors, tasks, snapshots, health,
    /// workflows). Returns the tab title or an empty string.
    QString selectTab(const QString& id);
    /// Re-reads the current project, dashboard counters and stored documents.
    void refresh();

    [[nodiscard]] bool isBuildRunning() const;
    [[nodiscard]] bool isWorkflowRunning() const;
    [[nodiscard]] bool isHealthRunning() const;
    /// Starts a health check; returns false when one is already running.
    bool runHealthCheck();
    /// The raw output of the most recent build, for error explanation and the
    /// project index. Empty before a build runs.
    [[nodiscard]] QString lastBuildOutput() const;
    /// Creates and persists a task through the task dialog (used by tests and
    /// the command center). Returns the task id, or empty when cancelled.
    QString createTaskInteractive();

signals:
    void statusMessage(const QString& message);
    void taskCountChanged(int openTasks);
    /// Emitted after a build finishes, with its complete output.
    void buildFinished(const QString& output, bool success);

private:
    struct HealthResult {
        gameforge::ProjectScan scan;
        gameforge::BuildConfigReport config;
        gameforge::DependencyGraph graph;
        devflow::HealthReport health;
    };

    void buildUi();
    QWidget* buildDashboardTab();
    QWidget* buildBuildTab();
    QWidget* buildErrorsTab();
    QWidget* buildTasksTab();
    QWidget* buildSnapshotsTab();
    QWidget* buildHealthTab();
    QWidget* buildWorkflowsTab();

    // Dashboard ----------------------------------------------------------
    void refreshDashboard();
    void addActivity(const QString& text);
    void loadActivity();

    // Build profiles -----------------------------------------------------
    void refreshProfileList();
    void loadProfile(const QString& id);
    void newProfile();
    void saveProfileFromForm();
    void duplicateProfile();
    void deleteProfile();
    void importProfile();
    void exportProfile();
    void showProfileOnForm(const devflow::BuildProfile& profile);
    [[nodiscard]] devflow::BuildProfile profileFromForm() const;
    void runBuild();
    void cancelBuild();
    void onBuildOutput(const QString& text);
    void onBuildFinished(int exitCode, QProcess::ExitStatus status, const QString& command);

    // Errors -------------------------------------------------------------
    void analyzeText(const QString& text, const QString& source);
    void collectErrorsFromLastBuild();
    void refreshErrorTree();
    void showSelectedError();
    void toggleResolvedError();
    void openSelectedErrorSource();

    // Tasks --------------------------------------------------------------
    void refreshTasks();
    void editTask(const QString& id);
    void newTaskWithTemplate(const QString& title, const QString& description);
    void deleteSelectedTask();
    void openSelectedTaskFile();

    // Snapshots -----------------------------------------------------------
    void refreshSnapshots();
    void saveSnapshotInteractive();
    void restoreSelectedSnapshot();
    void deleteSelectedSnapshot();

    // Health --------------------------------------------------------------
    void onHealthFinished();
    void showSelectedHealthFinding();

    // Workflows -----------------------------------------------------------
    void refreshWorkflowList();
    void newWorkflow();
    void runWorkflow();
    void deleteSelectedWorkflow();
    void onWorkflowStepStarted(const QString& step);
    void onWorkflowStepFinished(const QString& step, bool ok, const QString& detail);
    void onWorkflowFinished(bool ok, const QString& error, const QString& reportPath);

    [[nodiscard]] QString projectPath() const;
    [[nodiscard]] QString projectName() const;

    Database& m_database;
    DevFlowHooks m_hooks;
    devflow::DevFlowStore m_store;

    QTabWidget* m_tabs = nullptr;
    QLabel* m_projectLabel = nullptr;
    QLabel* m_profileCount = nullptr;
    QLabel* m_taskCount = nullptr;
    QLabel* m_snapshotCount = nullptr;
    QLabel* m_errorCount = nullptr;
    QListWidget* m_activity = nullptr;

    QListWidget* m_profileList = nullptr;
    QLineEdit* m_profileName = nullptr;
    QComboBox* m_profileSystem = nullptr;
    QLineEdit* m_profileBuildDir = nullptr;
    QLineEdit* m_profileConfiguration = nullptr;
    QLineEdit* m_profileTarget = nullptr;
    QLineEdit* m_profileCompiler = nullptr;
    QLineEdit* m_profileArguments = nullptr;
    QPlainTextEdit* m_profileEnvironment = nullptr;
    QPushButton* m_buildRunButton = nullptr;
    QPushButton* m_buildCancelButton = nullptr;
    QLabel* m_buildStatus = nullptr;
    QPlainTextEdit* m_buildOutput = nullptr;
    QString m_currentProfileId;
    devflow::BuildRunner* m_buildRunner = nullptr;
    devflow::ErrorReport m_lastErrorReport;

    QTreeWidget* m_errorTree = nullptr;
    QTextBrowser* m_errorDetails = nullptr;
    QComboBox* m_errorFilter = nullptr;
    QLabel* m_errorSummary = nullptr;
    QVector<devflow::DiagnosticGroup> m_errorGroups;

    QTableWidget* m_taskTable = nullptr;
    QComboBox* m_taskFilter = nullptr;
    QLineEdit* m_taskSearch = nullptr;
    QList<devflow::ProjectTask> m_tasks;

    QListWidget* m_snapshotList = nullptr;
    QLabel* m_snapshotSummary = nullptr;

    QTableWidget* m_healthTable = nullptr;
    QTextBrowser* m_healthDetails = nullptr;
    QLabel* m_healthSummary = nullptr;
    QPushButton* m_healthRunButton = nullptr;
    std::shared_ptr<QFutureWatcher<HealthResult>> m_healthWatcher;
    pkg::CancelToken m_healthToken;
    bool m_healthRunning = false;
    devflow::HealthReport m_lastHealth;

    QListWidget* m_workflowList = nullptr;
    QPlainTextEdit* m_workflowLog = nullptr;
    QProgressBar* m_workflowProgress = nullptr;
    QPushButton* m_workflowRunButton = nullptr;
    QPushButton* m_workflowCancelButton = nullptr;
    devflow::WorkflowRunner* m_workflowRunner = nullptr;
    QString m_workflowReportPath;
};

}  // namespace pkg::ui
