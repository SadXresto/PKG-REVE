// GameForge workspace: project inventory, build-config findings, dependency
// graph and portability intelligence for a local game project folder. Nothing
// here modifies the project.
#pragma once

#include "core/CancelToken.h"
#include "core/gameforge/BuildConfig.h"
#include "core/gameforge/DependencyGraph.h"
#include "core/gameforge/GameProject.h"
#include "core/gameforge/PortingIntelligence.h"

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

class DependencyGraphView;

/// Everything one background pass produces for a scanned project.
struct ProjectAnalysis {
    gameforge::BuildConfigReport config;
    gameforge::DependencyGraph graph;
    gameforge::PortingReport porting;
    gameforge::PortingIntelligence intel;
};

class GameForgePanel : public QWidget {
    Q_OBJECT

public:
    explicit GameForgePanel(QWidget* parent = nullptr);
    ~GameForgePanel() override;

    /// Sets the project root and scans it. The same path is not rescanned when
    /// it is already loaded.
    void setProjectRoot(const QString& path);
    void rescan();

    [[nodiscard]] QString projectRoot() const { return m_root; }
    [[nodiscard]] bool isScanning() const;
    [[nodiscard]] bool hasScan() const { return m_scan.readable(); }
    [[nodiscard]] int fileCount() const { return m_scan.fileCount(); }
    [[nodiscard]] QString detectedKind() const;
    [[nodiscard]] bool hasAnalysis() const;
    [[nodiscard]] const gameforge::DependencyGraph& dependencyGraph() const
    {
        return m_analysis.graph;
    }
    [[nodiscard]] const gameforge::BuildConfigReport& buildConfig() const
    {
        return m_analysis.config;
    }
    [[nodiscard]] const gameforge::PortingIntelligence& portingIntelligence() const
    {
        return m_analysis.intel;
    }
    [[nodiscard]] const gameforge::PortingReport& portabilityReport() const
    {
        return m_analysis.porting;
    }

signals:
    void openFileRequested(const QString& path);
    void openProjectRequested();
    void statusMessage(const QString& message);

private:
    void buildUi();
    void startScan();
    void onScanFinished();
    void startAnalysis();
    void onAnalysisFinished();
    void populateTree();
    void applyFilter(const QString& text);
    void updateSummary();
    void clearProject();

    void populateGraphTab();
    void populateBuildTab();
    void populatePortabilityTab();
    void populateIntelligenceTab();
    void showNode(const QString& id);
    void openSelectedNodeFile();
    void setBusy(bool busy, const QString& message);
    [[nodiscard]] QString absolutePath(const QString& relative) const;

    QString m_root;
    gameforge::ProjectScan m_scan;
    ProjectAnalysis m_analysis;
    CancelToken m_scanToken;
    CancelToken m_workToken;
    QString m_selectedNode;
    /// Incremented per scan so a late worker from an older project cannot
    /// repopulate the panel.
    int m_generation = 0;

    std::shared_ptr<QFutureWatcher<gameforge::ProjectScan>> m_scanWatcher;
    std::shared_ptr<QFutureWatcher<ProjectAnalysis>> m_analysisWatcher;

    QLabel* m_summary = nullptr;
    QLabel* m_warnings = nullptr;
    QLineEdit* m_filter = nullptr;
    QTreeWidget* m_tree = nullptr;
    QTabWidget* m_tabs = nullptr;

    DependencyGraphView* m_graphView = nullptr;
    QLineEdit* m_graphFilter = nullptr;
    QLabel* m_graphSummary = nullptr;
    QLabel* m_graphTitle = nullptr;
    QLabel* m_graphKind = nullptr;
    QLabel* m_graphWhere = nullptr;
    QLabel* m_graphDetail = nullptr;
    QPushButton* m_graphOpen = nullptr;

    QTreeWidget* m_configTree = nullptr;
    QLabel* m_configNotes = nullptr;

    QTreeWidget* m_findings = nullptr;
    QTextBrowser* m_findingsNotes = nullptr;

    QTextBrowser* m_intelBrowser = nullptr;

    QPushButton* m_openButton = nullptr;
    QPushButton* m_analyzeButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_fitButton = nullptr;
};

} // namespace pkg::ui
