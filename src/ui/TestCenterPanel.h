// Test & Debug Center: detected frameworks, real execution with cancellation,
// parsed results, failure grouping, regression history and report export.
#pragma once

#include "core/gameforge/GameProject.h"
#include "devflow/TestCenter.h"

#include <QFutureWatcher>
#include <QWidget>

#include <functional>
#include <memory>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTreeWidget;

namespace pkg {
class Database;
}

namespace pkg::ui {

struct TestCenterHooks {
    std::function<QString()> projectPath;
    std::function<void(const QString&, int)> openFile;
    std::function<void(const QString&)> statusMessage;
};

class TestCenterPanel : public QWidget {
    Q_OBJECT

public:
    TestCenterPanel(Database& database, TestCenterHooks hooks, QWidget* parent = nullptr);
    ~TestCenterPanel() override;

    void refresh();
    [[nodiscard]] bool isReady() const { return m_ready; }
    [[nodiscard]] bool isRunning() const;
    /// Runs the selected framework; returns false with a reason when it cannot.
    bool runSelected(QString* error = nullptr);
    void cancel();

private:
    struct DetectResult {
        gameforge::ProjectScan scan;
        QVector<devflow::TestFrameworkInfo> frameworks;
    };

    void buildUi();
    void startDetection();
    void onDetectionFinished();
    void updateFrameworkDetails();
    void onRunFinished(int exitCode, QProcess::ExitStatus status, const QString& command,
                       bool cancelled);
    void fillFailures();
    void fillHistory();
    void saveRun(const devflow::TestRunRecord& record);
    [[nodiscard]] QList<devflow::TestRunRecord> loadHistory() const;
    void exportReport();
    void showEmptyState(const QString& title, const QString& detail);
    [[nodiscard]] QString projectPath() const;
    [[nodiscard]] const devflow::TestFrameworkInfo* currentFramework() const;

    Database& m_database;
    TestCenterHooks m_hooks;
    devflow::TestRunner* m_runner = nullptr;

    QStackedWidget* m_pages = nullptr;
    QLabel* m_emptyTitle = nullptr;
    QLabel* m_emptyDetail = nullptr;
    QComboBox* m_frameworks = nullptr;
    QPushButton* m_redetect = nullptr;
    QPushButton* m_run = nullptr;
    QPushButton* m_cancel = nullptr;
    QPushButton* m_export = nullptr;
    QLabel* m_detectedDetail = nullptr;
    QLabel* m_status = nullptr;
    QPlainTextEdit* m_output = nullptr;
    QTreeWidget* m_failures = nullptr;
    QTableWidget* m_history = nullptr;

    std::shared_ptr<QFutureWatcher<DetectResult>> m_watcher;
    pkg::CancelToken m_token;
    QVector<devflow::TestFrameworkInfo> m_detected;
    devflow::TestOutputReport m_report;
    devflow::TestRunRecord m_lastRecord;
    QString m_rawOutput;
    QString m_scannedRoot;
    bool m_ready = false;
    bool m_detecting = false;
};

}  // namespace pkg::ui
