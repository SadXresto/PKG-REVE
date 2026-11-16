#pragma once

#include "core/Logging.h"

#include <QList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QTabWidget;
class QTimer;
class QTreeWidget;

namespace pkg::ui {

/// Bottom dock: application log stream plus parser diagnostics for the active
/// file. Log records are batched before being painted so that a noisy worker
/// cannot stall the UI thread.
class OutputPanel : public QWidget {
    Q_OBJECT

public:
    explicit OutputPanel(QWidget* parent = nullptr);

    void queueRecord(const pkg::LogRecord& record);
    void setRecords(const QList<pkg::LogRecord>& records);
    void clearLog();

    void setDiagnostics(const QStringList& warnings, const QString& context);
    void showOutputTab();
    void showDiagnosticsTab();

signals:
    void exportLogRequested();

private:
    void flushPending();
    void appendLine(const pkg::LogRecord& record);
    [[nodiscard]] bool passesFilter(const pkg::LogRecord& record) const;
    void rebuild();

    QTabWidget* m_tabs = nullptr;
    QPlainTextEdit* m_output = nullptr;
    QComboBox* m_filter = nullptr;
    QCheckBox* m_autoScroll = nullptr;
    QTreeWidget* m_diagnostics = nullptr;
    QLabel* m_diagnosticContext = nullptr;
    QTimer* m_flushTimer = nullptr;
    QList<LogRecord> m_pending;
    QList<LogRecord> m_records;
    int m_maxRecords = 4000;
};

}  // namespace pkg::ui
