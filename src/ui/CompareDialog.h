// Binary comparison dialog used by the hex editor.
//
// The comparison itself runs on a worker thread and is bounded; the dialog
// reports truncation instead of pretending a partial compare covered the file.
#pragma once

#include "core/BinaryCompare.h"

#include <QDialog>
#include <QFutureWatcher>

#include <memory>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace pkg::ui {

class CompareDialog : public QDialog {
    Q_OBJECT

public:
    explicit CompareDialog(const QString& basePath, QWidget* parent = nullptr);

signals:
    /// Emitted when the user activates a differing run.
    void offsetActivated(quint64 offset);

private:
    void chooseFile();
    void startCompare();
    void cancelCompare();
    void onFinished();
    void populate(const pkg::CompareResult& result);

    QString m_basePath;
    QLineEdit* m_path = nullptr;
    QPushButton* m_compare = nullptr;
    QLabel* m_summary = nullptr;
    QTableWidget* m_runs = nullptr;
    std::shared_ptr<QFutureWatcher<pkg::Result<pkg::CompareResult>>> m_watcher;
    pkg::CancelToken m_token;
    bool m_running = false;
};

}  // namespace pkg::ui
