#include "ui/OutputPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace pkg::ui {

OutputPanel::OutputPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("outputPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    layout->addWidget(m_tabs);

    // Output tab ---------------------------------------------------------
    auto* outputTab = new QWidget(m_tabs);
    auto* outputLayout = new QVBoxLayout(outputTab);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(0);

    auto* toolbar = new QWidget(outputTab);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(6, 4, 6, 4);
    toolbarLayout->setSpacing(6);

    auto* levelLabel = new QLabel(QStringLiteral("Level"), toolbar);
    levelLabel->setObjectName(QStringLiteral("hintLabel"));
    toolbarLayout->addWidget(levelLabel);

    m_filter = new QComboBox(toolbar);
    m_filter->addItems({QStringLiteral("Debug"), QStringLiteral("Info"),
                        QStringLiteral("Warning"), QStringLiteral("Error")});
    m_filter->setCurrentIndex(1);
    toolbarLayout->addWidget(m_filter);
    toolbarLayout->addStretch(1);

    m_autoScroll = new QCheckBox(QStringLiteral("Follow output"), toolbar);
    m_autoScroll->setChecked(true);
    toolbarLayout->addWidget(m_autoScroll);

    auto* clearButton = new QPushButton(QStringLiteral("Clear"), toolbar);
    auto* exportButton = new QPushButton(QStringLiteral("Export\u2026"), toolbar);
    toolbarLayout->addWidget(clearButton);
    toolbarLayout->addWidget(exportButton);
    outputLayout->addWidget(toolbar);

    m_output = new QPlainTextEdit(outputTab);
    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_output->setMaximumBlockCount(6000);
    outputLayout->addWidget(m_output, 1);
    m_tabs->addTab(outputTab, QStringLiteral("Output"));

    // Diagnostics tab ----------------------------------------------------
    auto* diagnosticsTab = new QWidget(m_tabs);
    auto* diagnosticsLayout = new QVBoxLayout(diagnosticsTab);
    diagnosticsLayout->setContentsMargins(6, 6, 6, 6);
    m_diagnosticContext = new QLabel(diagnosticsTab);
    m_diagnosticContext->setObjectName(QStringLiteral("hintLabel"));
    m_diagnosticContext->setWordWrap(true);
    diagnosticsLayout->addWidget(m_diagnosticContext);

    m_diagnostics = new QTreeWidget(diagnosticsTab);
    m_diagnostics->setHeaderHidden(true);
    m_diagnostics->setUniformRowHeights(true);
    diagnosticsLayout->addWidget(m_diagnostics, 1);
    m_tabs->addTab(diagnosticsTab, QStringLiteral("Diagnostics"));
    m_tabs->setTabToolTip(1, QStringLiteral("Parser warnings for the active file"));

    connect(clearButton, &QPushButton::clicked, this, &OutputPanel::clearLog);
    connect(exportButton, &QPushButton::clicked, this, &OutputPanel::exportLogRequested);
    connect(m_filter, &QComboBox::currentIndexChanged, this, [this](int) { rebuild(); });

    m_flushTimer = new QTimer(this);
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(120);
    connect(m_flushTimer, &QTimer::timeout, this, &OutputPanel::flushPending);

    setDiagnostics({}, QStringLiteral("No file analyzed yet."));
}

void OutputPanel::queueRecord(const pkg::LogRecord& record)
{
    m_pending.append(record);
    if (!m_flushTimer->isActive())
        m_flushTimer->start();
}

void OutputPanel::setRecords(const QList<LogRecord>& records)
{
    m_records = records;
    rebuild();
}

void OutputPanel::flushPending()
{
    if (m_pending.isEmpty())
        return;
    const QList<LogRecord> pending = m_pending;
    m_pending.clear();
    for (const LogRecord& record : pending) {
        m_records.append(record);
        if (passesFilter(record))
            appendLine(record);
    }
    while (m_records.size() > m_maxRecords)
        m_records.removeFirst();
}

bool OutputPanel::passesFilter(const LogRecord& record) const
{
    const int minimum = m_filter->currentIndex();  // 0 debug, 1 info, 2 warning, 3 error
    switch (record.level) {
    case LogLevel::Trace:
    case LogLevel::Debug:
        return minimum <= 0;
    case LogLevel::Info:
        return minimum <= 1;
    case LogLevel::Warning:
        return minimum <= 2;
    case LogLevel::Error:
        return minimum <= 3;
    }
    return true;
}

void OutputPanel::appendLine(const LogRecord& record)
{
    const QString line = QStringLiteral("%1 [%2] [%3] %4")
                             .arg(record.timestamp.toString(QStringLiteral("HH:mm:ss.zzz")),
                                  Logger::levelName(record.level).leftJustified(5),
                                  record.category, record.message);
    m_output->appendPlainText(line);
}

void OutputPanel::rebuild()
{
    m_output->clear();
    for (const LogRecord& record : m_records) {
        if (passesFilter(record))
            appendLine(record);
    }
}

void OutputPanel::clearLog()
{
    m_records.clear();
    m_pending.clear();
    m_output->clear();
}

void OutputPanel::setDiagnostics(const QStringList& warnings, const QString& context)
{
    m_diagnosticContext->setText(context);
    m_diagnostics->clear();
    for (const QString& warning : warnings) {
        auto* item = new QTreeWidgetItem(m_diagnostics);
        item->setText(0, warning);
        item->setToolTip(0, warning);
    }
    if (warnings.isEmpty()) {
        auto* item = new QTreeWidgetItem(m_diagnostics);
        item->setText(0, QStringLiteral("No diagnostics for the active file."));
        item->setDisabled(true);
    }
    m_tabs->setTabText(1, warnings.isEmpty()
                              ? QStringLiteral("Diagnostics")
                              : QStringLiteral("Diagnostics (%1)").arg(warnings.size()));
}

void OutputPanel::showOutputTab()
{
    m_tabs->setCurrentIndex(0);
}

void OutputPanel::showDiagnosticsTab()
{
    m_tabs->setCurrentIndex(1);
}

}  // namespace pkg::ui
