// Smart project diff dialog: compares two folder states and shows file,
// configuration, dependency and portability differences with export.
#pragma once

#include "core/ProjectDiff.h"

#include <QDialog>
#include <QFutureWatcher>

#include <memory>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextBrowser;
class QTreeWidget;

namespace pkg::ui {

class ProjectDiffDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProjectDiffDialog(QWidget* parent = nullptr);

    /// Preselects the folders (used by the active project and diagnostics).
    void setFolders(const QString& left, const QString& right);
    /// Starts a comparison if both folders are set.
    bool startComparison();
    [[nodiscard]] bool isComparing() const { return m_comparing; }
    [[nodiscard]] const projectdiff::DiffReport& report() const { return m_report; }

private:
    void buildUi();
    void browseLeft();
    void browseRight();
    void onFinished();
    void fillTables();
    void exportReport();

    QLineEdit* m_left = nullptr;
    QLineEdit* m_right = nullptr;
    QPushButton* m_compare = nullptr;
    QPushButton* m_cancel = nullptr;
    QPushButton* m_export = nullptr;
    QLabel* m_summary = nullptr;
    QTreeWidget* m_files = nullptr;
    QTreeWidget* m_config = nullptr;
    QTableWidget* m_dependencies = nullptr;
    QTableWidget* m_portability = nullptr;
    QTextBrowser* m_notes = nullptr;

    std::shared_ptr<QFutureWatcher<projectdiff::DiffReport>> m_watcher;
    pkg::CancelToken m_token;
    projectdiff::DiffReport m_report;
    bool m_comparing = false;
};

}  // namespace pkg::ui
