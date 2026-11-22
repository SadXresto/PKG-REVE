// GameForge workspace: project inventory and portability evidence for a local
// game project folder. Nothing here modifies the project.
#pragma once

#include "core/CancelToken.h"
#include "core/gameforge/GameProject.h"

#include <QFutureWatcher>
#include <QWidget>

#include <memory>

class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;

namespace pkg::ui {

class GameForgePanel : public QWidget {
    Q_OBJECT

public:
    explicit GameForgePanel(QWidget* parent = nullptr);
    ~GameForgePanel() override;

    /// Sets the project root and scans it. The same path is not rescanned when
    /// it is already loaded and not empty.
    void setProjectRoot(const QString& path);
    void rescan();

    [[nodiscard]] QString projectRoot() const { return m_root; }
    [[nodiscard]] bool isScanning() const;
    [[nodiscard]] bool hasScan() const { return m_scan.readable(); }
    [[nodiscard]] int fileCount() const { return m_scan.fileCount(); }
    [[nodiscard]] QString detectedKind() const;

signals:
    void openFileRequested(const QString& path);
    void openProjectRequested();
    void statusMessage(const QString& message);

private:
    void buildUi();
    void startScan();
    void onScanFinished();
    void startPortabilityAnalysis();
    void onPortabilityFinished();
    void populateTree();
    void applyFilter(const QString& text);
    void updateSummary();
    void clearProject();

    QString m_root;
    gameforge::ProjectScan m_scan;
    gameforge::PortingReport m_report;
    CancelToken m_scanToken;
    CancelToken m_reportToken;
    bool m_hasReport = false;

    std::shared_ptr<QFutureWatcher<gameforge::ProjectScan>> m_scanWatcher;
    std::shared_ptr<QFutureWatcher<gameforge::PortingReport>> m_reportWatcher;

    QLabel* m_summary = nullptr;
    QLabel* m_warnings = nullptr;
    QLineEdit* m_filter = nullptr;
    QTreeWidget* m_tree = nullptr;
    QPushButton* m_openButton = nullptr;
    QPushButton* m_rescanButton = nullptr;
    QPushButton* m_analyzeButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTreeWidget* m_findings = nullptr;
    QTextBrowser* m_reportNotes = nullptr;
    QLabel* m_reportHeader = nullptr;
};

} // namespace pkg::ui
