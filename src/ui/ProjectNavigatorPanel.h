// Project Navigator: a navigable index over a project's files, build targets,
// dependencies, configuration files, source references, portability findings
// and build errors. Relationships always state their confidence.
#pragma once

#include "core/gameforge/BuildConfig.h"
#include "core/gameforge/DependencyGraph.h"
#include "core/gameforge/GameProject.h"
#include "core/projectnav/ProjectNavigator.h"

#include <QFutureWatcher>
#include <QVector>
#include <QWidget>

#include <functional>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;

namespace pkg::ui {

struct NavigatorHooks {
    std::function<QString()> projectPath;
    /// Opens a file and (when line > 0) reports the line in the status bar.
    std::function<void(const QString&, int)> openFile;
    std::function<void(const QString&)> statusMessage;
    std::function<void()> openProject;
};

class ProjectNavigatorPanel : public QWidget {
    Q_OBJECT

public:
    explicit ProjectNavigatorPanel(NavigatorHooks hooks, QWidget* parent = nullptr);
    ~ProjectNavigatorPanel() override;

    /// Rescans the current project and rebuilds the index.
    void refresh();
    /// True when a scan has produced an index.
    [[nodiscard]] bool isReady() const { return m_ready; }
    [[nodiscard]] bool isScanning() const { return m_scanning; }
    [[nodiscard]] QString indexSummary() const;

    /// Records build diagnostics for the index. Rebuilds the index from the
    /// cached scan without rescanning the project.
    void setBuildErrors(const QVector<projectnav::ErrorInput>& errors);

private:
    struct ScanResult {
        gameforge::ProjectScan scan;
        gameforge::BuildConfigReport config;
        gameforge::DependencyGraph graph;
        gameforge::PortingReport porting;
    };

    void buildUi();
    void startScan();
    void onScanFinished();
    void rebuildIndex();
    void rebuildTree();
    void updateFilter();
    void showSelectedEntity();
    void openSelected();
    void showEmptyState(const QString& message);
    [[nodiscard]] QString projectPath() const;

    NavigatorHooks m_hooks;
    QStackedWidget* m_pages = nullptr;
    QLabel* m_empty = nullptr;
    QLabel* m_emptyDetail = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_filter = nullptr;
    QPushButton* m_refresh = nullptr;
    QLabel* m_readyLabel = nullptr;
    QLabel* m_summary = nullptr;
    QTreeWidget* m_tree = nullptr;
    QTextBrowser* m_details = nullptr;
    QListWidget* m_related = nullptr;

    std::shared_ptr<QFutureWatcher<ScanResult>> m_watcher;
    pkg::CancelToken m_token;
    ScanResult m_scan;
    projectnav::Index m_index;
    QVector<projectnav::ErrorInput> m_errors;
    QString m_scannedRoot;
    bool m_scanning = false;
    bool m_ready = false;
};

}  // namespace pkg::ui
