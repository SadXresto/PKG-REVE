#include "ui/OutputPanel.h"

#include "core/FormatUtil.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace pkg::ui {
namespace {

/// Matches absolute and relative "path:line" or "path:line:column" references
/// in compiler and tool output.
const QRegularExpression& sourcePattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(([A-Za-z]:[\\/][^\s:]+|(?:\.{0,2}[\\/])?[^\s:]+\.(?:cpp|cxx|cc|c|h|hpp|cmake|txt|godot|meson|py|json|md|rs|go|java|kt|cs|ts|tsx|js|jsx)):(\d+)(?::(\d+))?)"));
    return pattern;
}

const QRegularExpression& secretPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(((?:api[_-]?key|apikey|token|secret|password|passwd|authorization)\s*[:=]\s*)((?:Bearer\s+)?[^\s,;]+))"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

}  // namespace

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


    m_filter = new QComboBox(toolbar);
    m_filter->addItems({QStringLiteral("Debug"), QStringLiteral("Info"),
                        QStringLiteral("Warning"), QStringLiteral("Error")});
    m_filter->setCurrentIndex(1);
    m_filter->setToolTip(QStringLiteral("Minimum level shown in the output"));
    toolbarLayout->addWidget(m_filter);

    m_search = new QLineEdit(toolbar);
    m_search->setPlaceholderText(QStringLiteral("Filter output…"));
    m_search->setClearButtonEnabled(true);
    m_search->setMaximumWidth(240);
    toolbarLayout->addWidget(m_search);

    m_timestamps = new QCheckBox(QStringLiteral("Time"), toolbar);
    m_timestamps->setToolTip(QStringLiteral("Show timestamps"));
    m_timestamps->setChecked(true);
    toolbarLayout->addWidget(m_timestamps);

    m_autoScroll = new QCheckBox(QStringLiteral("Follow"), toolbar);
    m_autoScroll->setChecked(true);
    m_autoScroll->setToolTip(QStringLiteral("Scroll to the newest record automatically"));
    toolbarLayout->addWidget(m_autoScroll);

    m_pause = new QPushButton(QStringLiteral("Pause"), toolbar);
    m_pause->setCheckable(true);
    m_pause->setToolTip(
        QStringLiteral("Pause automatic scrolling. New output is still recorded."));
    toolbarLayout->addWidget(m_pause);

    toolbarLayout->addStretch(1);

    auto* copyButton = new QPushButton(QStringLiteral("Copy"), toolbar);
    copyButton->setToolTip(QStringLiteral("Copy selected output, or everything visible"));
    auto* clearButton = new QPushButton(QStringLiteral("Clear"), toolbar);
    auto* exportButton = new QPushButton(QStringLiteral("Export…"), toolbar);
    toolbarLayout->addWidget(copyButton);
    toolbarLayout->addWidget(clearButton);
    toolbarLayout->addWidget(exportButton);
    outputLayout->addWidget(toolbar);

    m_output = new QPlainTextEdit(outputTab);
    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_output->setMaximumBlockCount(6000);
    m_output->setContextMenuPolicy(Qt::CustomContextMenu);
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

    connect(clearButton, &QPushButton::clicked, this, &OutputPanel::confirmClear);
    connect(exportButton, &QPushButton::clicked, this, &OutputPanel::exportLogRequested);
    connect(copyButton, &QPushButton::clicked, this, &OutputPanel::copySelection);
    connect(m_filter, &QComboBox::currentIndexChanged, this, [this](int) { rebuild(); });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString&) { rebuild(); });
    connect(m_timestamps, &QCheckBox::toggled, this, [this](bool) { rebuild(); });
    connect(m_pause, &QPushButton::toggled, this, &OutputPanel::togglePause);
    connect(m_output, &QPlainTextEdit::customContextMenuRequested, this,
            [this](const QPoint& position) {
                QMenu menu(this);
                QAction* copy = menu.addAction(QStringLiteral("Copy selection"));
                copy->setEnabled(m_output->textCursor().hasSelection());
                QAction* copyAll = menu.addAction(QStringLiteral("Copy visible output"));
                menu.addSeparator();
                QAction* clear = menu.addAction(QStringLiteral("Clear output…"));
                QAction* chosen = menu.exec(m_output->mapToGlobal(position));
                if (chosen == copy)
                    copySelection();
                else if (chosen == copyAll) {
                    QApplication::clipboard()->setText(m_output->toPlainText());
                } else if (chosen == clear)
                    confirmClear();
            });
    m_output->viewport()->installEventFilter(this);

    m_flushTimer = new QTimer(this);
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(120);
    connect(m_flushTimer, &QTimer::timeout, this, &OutputPanel::flushPending);

    setDiagnostics({}, QStringLiteral("No file analyzed yet."));
}

QString OutputPanel::redactSecrets(const QString& text)
{
    QString result = text;
    QRegularExpressionMatch match;
    int offset = 0;
    while ((match = secretPattern().match(result, offset)).hasMatch()) {
        const QString replacement = match.captured(1) + QStringLiteral("***redacted***");
        result.replace(match.capturedStart(0), match.capturedLength(0), replacement);
        offset = match.capturedStart(0) + replacement.size();
    }
    return result;
}

QString OutputPanel::formatRecord(const pkg::LogRecord& record, bool timestamps)
{
    const QString message = redactSecrets(record.message);
    if (!timestamps)
        return QStringLiteral("[%1] [%2] %3")
            .arg(Logger::levelName(record.level).leftJustified(5), record.category, message);
    return QStringLiteral("%1 [%2] [%3] %4")
        .arg(record.timestamp.toString(QStringLiteral("HH:mm:ss.zzz")),
             Logger::levelName(record.level).leftJustified(5), record.category, message);
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
    bool levelAccepted = true;
    switch (record.level) {
    case LogLevel::Trace:
    case LogLevel::Debug:
        levelAccepted = minimum <= 0;
        break;
    case LogLevel::Info:
        levelAccepted = minimum <= 1;
        break;
    case LogLevel::Warning:
        levelAccepted = minimum <= 2;
        break;
    case LogLevel::Error:
        levelAccepted = minimum <= 3;
        break;
    }
    if (!levelAccepted)
        return false;

    const QString needle = m_search != nullptr ? m_search->text().trimmed() : QString();
    if (needle.isEmpty())
        return true;
    return record.message.contains(needle, Qt::CaseInsensitive) ||
           record.category.contains(needle, Qt::CaseInsensitive);
}

void OutputPanel::appendLine(const LogRecord& record)
{
    m_output->appendPlainText(formatRecord(record, m_timestamps->isChecked()));
    if (!m_paused && m_autoScroll->isChecked()) {
        auto* bar = m_output->verticalScrollBar();
        bar->setValue(bar->maximum());
    }
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

void OutputPanel::confirmClear()
{
    if (m_records.isEmpty()) {
        clearLog();
        return;
    }
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Clear output"),
        QStringLiteral("Clear %1 output record(s)? This only clears the panel; the log file "
                       "is kept.")
            .arg(static_cast<int>(m_records.size())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes)
        clearLog();
}

void OutputPanel::copySelection()
{
    const QString text = m_output->textCursor().hasSelection()
        ? m_output->textCursor().selectedText().replace(QChar(0x2029), QLatin1Char('\n'))
        : m_output->toPlainText();
    QApplication::clipboard()->setText(text);
}

void OutputPanel::togglePause(bool paused)
{
    m_paused = paused;
    m_pause->setText(paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
    if (!paused && m_autoScroll->isChecked()) {
        auto* bar = m_output->verticalScrollBar();
        bar->setValue(bar->maximum());
    }
}

void OutputPanel::activateLine()
{
    const QString line = m_output->textCursor().block().text();
    const QRegularExpressionMatch match = sourcePattern().match(line);
    if (!match.hasMatch())
        return;
    emit sourceRequested(match.captured(1), match.captured(2).toInt());
}

bool OutputPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_output->viewport() && event->type() == QEvent::MouseButtonDblClick) {
        activateLine();
        return true;
    }
    return QWidget::eventFilter(watched, event);
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
