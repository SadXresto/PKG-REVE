#pragma once

#include "core/Logging.h"

#include <QList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTimer;
class QTreeWidget;

namespace pkg::ui {

/// Bottom dock: application log stream plus parser diagnostics for the active
/// file. Log records are batched before being painted so that a noisy worker
/// cannot stall the UI thread. Secrets are masked before display.
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

    /// The fixed-width text of one record as the panel presents it. Exposed so
    /// redaction and formatting are unit tested without a widget.
    [[nodiscard]] static QString formatRecord(const pkg::LogRecord& record, bool timestamps);
    /// Masks values that look like credentials in free-form log text.
    [[nodiscard]] static QString redactSecrets(const QString& text);

signals:
    void exportLogRequested();
    /// A line containing a file reference was activated; `line` is 0 when the
    /// reference has no line number.
    void sourceRequested(const QString& file, int line);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void flushPending();
    void appendLine(const pkg::LogRecord& record);
    [[nodiscard]] bool passesFilter(const pkg::LogRecord& record) const;
    void rebuild();
    void copySelection();
    void confirmClear();
    void togglePause(bool paused);
    void activateLine();

    QTabWidget* m_tabs = nullptr;
    QPlainTextEdit* m_output = nullptr;
    QComboBox* m_filter = nullptr;
    QLineEdit* m_search = nullptr;
    QCheckBox* m_timestamps = nullptr;
    QCheckBox* m_autoScroll = nullptr;
    QPushButton* m_pause = nullptr;
    QTreeWidget* m_diagnostics = nullptr;
    QLabel* m_diagnosticContext = nullptr;
    QTimer* m_flushTimer = nullptr;
    QList<LogRecord> m_pending;
    QList<LogRecord> m_records;
    int m_maxRecords = 4000;
    bool m_paused = false;
};

}  // namespace pkg::ui
