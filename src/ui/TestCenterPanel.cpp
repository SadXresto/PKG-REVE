#include "ui/TestCenterPanel.h"

#include "data/Database.h"
#include "devflow/DevFlowModels.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QTextCursor>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>

namespace pkg::ui {
namespace {

constexpr auto kRunKind = "testrun";

QString resultText(const devflow::TestRunRecord& record)
{
    if (record.cancelled)
        return QStringLiteral("cancelled");
    return QStringLiteral("%1 passed, %2 failed, %3 skipped")
        .arg(record.passed)
        .arg(record.failed)
        .arg(record.skipped);
}

}  // namespace

TestCenterPanel::TestCenterPanel(Database& database, TestCenterHooks hooks, QWidget* parent)
    : QWidget(parent), m_database(database), m_hooks(std::move(hooks))
{
    setObjectName(QStringLiteral("testCenterPanel"));
    m_runner = new devflow::TestRunner(this);
    buildUi();

    connect(m_runner, &devflow::TestRunner::started, this, [this](const QString& command) {
        m_rawOutput.clear();
        m_output->clear();
        m_output->appendPlainText(QStringLiteral("$ %1").arg(command));
        m_run->setEnabled(false);
        m_cancel->setEnabled(true);
        m_frameworks->setEnabled(false);
        m_status->setText(QStringLiteral("Running %1\u2026").arg(command));
    });
    connect(m_runner, &devflow::TestRunner::outputReceived, this, [this](const QString& text) {
        m_rawOutput += text;
        m_output->moveCursor(QTextCursor::End);
        m_output->insertPlainText(text);
        m_output->moveCursor(QTextCursor::End);
    });
    connect(m_runner, &devflow::TestRunner::finished, this, &TestCenterPanel::onRunFinished);
    refresh();
}

TestCenterPanel::~TestCenterPanel()
{
    m_token.cancel();
    if (m_runner->isRunning())
        m_runner->cancel();
}

QString TestCenterPanel::projectPath() const
{
    return m_hooks.projectPath ? m_hooks.projectPath() : QString();
}

bool TestCenterPanel::isRunning() const
{
    return m_runner->isRunning();
}

void TestCenterPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(this);
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(12, 8, 12, 8);
    headerLayout->setSpacing(2);
    auto* title = new QLabel(QStringLiteral("TEST & DEBUG CENTER"), header);
    title->setObjectName(QStringLiteral("sectionLabel"));
    headerLayout->addWidget(title);
    auto* subtitle = new QLabel(QStringLiteral("Detected frameworks only \u00B7 real runs \u00B7 "
                                               "cancellable"),
                                header);
    subtitle->setObjectName(QStringLiteral("hintLabel"));
    subtitle->setWordWrap(true);
    headerLayout->addWidget(subtitle);
    layout->addWidget(header);

    m_pages = new QStackedWidget(this);
    layout->addWidget(m_pages, 1);

    auto* emptyPage = new QWidget(m_pages);
    auto* emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->setContentsMargins(16, 24, 16, 16);
    emptyLayout->setSpacing(8);
    m_emptyTitle = new QLabel(emptyPage);
    m_emptyTitle->setObjectName(QStringLiteral("emptyStateTitle"));
    m_emptyTitle->setWordWrap(true);
    emptyLayout->addWidget(m_emptyTitle);
    m_emptyDetail = new QLabel(emptyPage);
    m_emptyDetail->setObjectName(QStringLiteral("hintLabel"));
    m_emptyDetail->setWordWrap(true);
    emptyLayout->addWidget(m_emptyDetail);
    auto* detectButton = new QPushButton(QStringLiteral("Detect frameworks"), emptyPage);
    connect(detectButton, &QPushButton::clicked, this, &TestCenterPanel::refresh);
    emptyLayout->addWidget(detectButton);
    emptyLayout->addStretch(1);
    m_pages->addWidget(emptyPage);

    auto* contentPage = new QWidget(m_pages);
    auto* contentLayout = new QVBoxLayout(contentPage);
    contentLayout->setContentsMargins(8, 0, 8, 8);
    contentLayout->setSpacing(5);

    auto* frameworkRow = new QHBoxLayout();
    frameworkRow->setSpacing(5);
    m_frameworks = new QComboBox(contentPage);
    frameworkRow->addWidget(m_frameworks, 1);
    m_redetect = new QPushButton(QStringLiteral("Detect"), contentPage);
    frameworkRow->addWidget(m_redetect);
    contentLayout->addLayout(frameworkRow);

    m_detectedDetail = new QLabel(contentPage);
    m_detectedDetail->setObjectName(QStringLiteral("hintLabel"));
    m_detectedDetail->setWordWrap(true);
    contentLayout->addWidget(m_detectedDetail);

    auto* actionRow = new QHBoxLayout();
    actionRow->setSpacing(5);
    m_run = new QPushButton(QStringLiteral("Run tests"), contentPage);
    m_cancel = new QPushButton(QStringLiteral("Cancel"), contentPage);
    m_cancel->setEnabled(false);
    m_export = new QPushButton(QStringLiteral("Export report\u2026"), contentPage);
    m_export->setEnabled(false);
    actionRow->addWidget(m_run);
    actionRow->addWidget(m_cancel);
    actionRow->addStretch(1);
    actionRow->addWidget(m_export);
    contentLayout->addLayout(actionRow);

    m_status = new QLabel(contentPage);
    m_status->setObjectName(QStringLiteral("hintLabel"));
    m_status->setWordWrap(true);
    contentLayout->addWidget(m_status);

    m_output = new QPlainTextEdit(contentPage);
    m_output->setReadOnly(true);
    m_output->setObjectName(QStringLiteral("testOutput"));
    m_output->setPlaceholderText(QStringLiteral("Test output appears here during a run."));
    contentLayout->addWidget(m_output, 2);

    m_failures = new QTreeWidget(contentPage);
    m_failures->setHeaderLabels({QStringLiteral("Failure group"), QStringLiteral("Count")});
    m_failures->setColumnWidth(0, 250);
    contentLayout->addWidget(m_failures, 2);

    m_history = new QTableWidget(contentPage);
    m_history->setColumnCount(4);
    m_history->setHorizontalHeaderLabels({QStringLiteral("When"), QStringLiteral("Framework"),
                                          QStringLiteral("Result"), QStringLiteral("Change")});
    m_history->horizontalHeader()->setStretchLastSection(true);
    m_history->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_history->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_history->verticalHeader()->setVisible(false);
    m_history->setMaximumHeight(130);
    contentLayout->addWidget(m_history, 1);

    m_pages->addWidget(contentPage);

    connect(m_redetect, &QPushButton::clicked, this, &TestCenterPanel::refresh);
    connect(m_run, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!runSelected(&error) && !error.isEmpty() && m_hooks.statusMessage)
            m_hooks.statusMessage(error);
    });
    connect(m_cancel, &QPushButton::clicked, this, &TestCenterPanel::cancel);
    connect(m_export, &QPushButton::clicked, this, &TestCenterPanel::exportReport);
    connect(m_frameworks, &QComboBox::currentIndexChanged, this,
            [this](int) { updateFrameworkDetails(); });
    connect(m_failures, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (item == nullptr || m_hooks.openFile == nullptr)
                    return;
                const QString file = item->data(0, Qt::UserRole).toString();
                if (file.isEmpty())
                    return;
                m_hooks.openFile(file, item->data(0, Qt::UserRole + 1).toInt());
            });
    m_pages->setCurrentIndex(0);
    showEmptyState(QStringLiteral("No project folder is open"),
                   QStringLiteral("Open a project to detect CTest, GoogleTest, Catch2, pytest, "
                                  "npm test or cargo test."));
}

void TestCenterPanel::refresh()
{
    const QString root = projectPath();
    if (root.isEmpty()) {
        m_token.cancel();
        m_ready = false;
        m_detected.clear();
        m_frameworks->clear();
        showEmptyState(QStringLiteral("No project folder is open"),
                       QStringLiteral("Open a project to detect CTest, GoogleTest, Catch2, "
                                      "pytest, npm test or cargo test."));
        return;
    }
    if (m_detecting && root == m_scannedRoot)
        return;
    m_token.cancel();
    m_token = pkg::CancelToken();
    m_scannedRoot = root;
    m_detecting = true;
    m_status->setText(QStringLiteral("Detecting test frameworks\u2026"));
    const pkg::CancelToken token = m_token;
    m_watcher = std::make_shared<QFutureWatcher<DetectResult>>();
    connect(m_watcher.get(), &QFutureWatcher<DetectResult>::finished, this,
            &TestCenterPanel::onDetectionFinished);
    m_watcher->setFuture(QtConcurrent::run([root, token]() {
        DetectResult result;
        result.scan = gameforge::scanProject(root, token);
        if (!result.scan.cancelled && result.scan.readable())
            result.frameworks = devflow::detectTestFrameworks(root, result.scan);
        return result;
    }));
}

void TestCenterPanel::onDetectionFinished()
{
    if (!m_watcher)
        return;
    const DetectResult result = m_watcher->result();
    m_watcher.reset();
    m_detecting = false;
    if (result.scan.cancelled || result.scan.rootPath != m_scannedRoot)
        return;

    m_detected = result.frameworks;
    m_frameworks->clear();
    for (const devflow::TestFrameworkInfo& info : m_detected) {
        m_frameworks->addItem(info.runnable ? info.name
                                            : QStringLiteral("%1 (not runnable)").arg(info.name));
    }
    fillHistory();
    // Detection finished. An empty result is a completed detection, not a
    // pending one; the empty state explains what was checked.
    m_ready = true;
    if (m_detected.isEmpty()) {
        showEmptyState(QStringLiteral("No supported test framework was detected"),
                       QStringLiteral("Checked: CTest/CMake test registration, GoogleTest, "
                                      "Catch2, pytest, npm test and cargo test. Nothing is "
                                      "offered that was not found in the project."));
        return;
    }
    m_pages->setCurrentIndex(1);
    updateFrameworkDetails();
    m_status->setText(QStringLiteral("%1 framework(s) detected. Only detected frameworks are "
                                     "listed.")
                          .arg(m_detected.size()));
}

void TestCenterPanel::updateFrameworkDetails()
{
    const devflow::TestFrameworkInfo* info = currentFramework();
    m_run->setEnabled(info != nullptr && info->runnable && !m_runner->isRunning());
    m_cancel->setEnabled(m_runner->isRunning());
    if (info == nullptr) {
        m_detectedDetail->clear();
        return;
    }
    QStringList lines;
    lines << QStringLiteral("Evidence: %1").arg(info->evidence);
    if (!info->detail.isEmpty())
        lines << info->detail;
    if (info->runnable)
        lines << QStringLiteral("Command: %1  (in %2)").arg(info->displayCommand(),
                                                            info->workingDirectory);
    else
        lines << QStringLiteral("Cannot run: %1").arg(info->unavailableReason);
    m_detectedDetail->setText(lines.join(QLatin1Char('\n')));
}

const devflow::TestFrameworkInfo* TestCenterPanel::currentFramework() const
{
    const int index = m_frameworks->currentIndex();
    if (index < 0 || index >= m_detected.size())
        return nullptr;
    return &m_detected.at(index);
}

bool TestCenterPanel::runSelected(QString* error)
{
    const devflow::TestFrameworkInfo* info = currentFramework();
    if (info == nullptr) {
        if (error != nullptr)
            *error = QStringLiteral("No detected framework is selected.");
        return false;
    }
    return m_runner->start(*info, error);
}

void TestCenterPanel::cancel()
{
    if (m_runner->isRunning()) {
        m_runner->cancel();
        m_status->setText(QStringLiteral("Stopping the test run\u2026"));
    }
}

void TestCenterPanel::onRunFinished(int exitCode, QProcess::ExitStatus status,
                                    const QString& command, bool cancelled)
{
    const devflow::TestFrameworkInfo* info = currentFramework();
    m_run->setEnabled(info != nullptr && info->runnable);
    m_cancel->setEnabled(false);
    m_frameworks->setEnabled(true);
    if (info == nullptr)
        return;

    m_report = devflow::parseTestOutput(info->framework, m_rawOutput, exitCode);
    fillFailures();

    devflow::TestRunRecord record;
    record.id = devflow::makeId(QStringLiteral("testrun"));
    record.projectPath = m_scannedRoot;
    record.framework = info->name;
    record.command = command;
    record.startedAt = QDateTime::currentDateTime();
    record.passed = m_report.passed;
    record.failed = m_report.failed;
    record.skipped = m_report.skipped;
    record.exitCode = exitCode;
    record.cancelled = cancelled;
    record.summary = QStringLiteral("%1 (%2)")
                         .arg(resultText(record),
                              status == QProcess::NormalExit ? QStringLiteral("finished")
                                                             : QStringLiteral("aborted"));
    for (const devflow::TestFailureGroup& group : m_report.failureGroups)
        record.failureFingerprints.append(group.fingerprint);

    QList<devflow::TestRunRecord> previous = loadHistory();
    const devflow::TestRunRecord* prior = nullptr;
    for (const devflow::TestRunRecord& candidate : previous) {
        if (candidate.framework == record.framework) {
            prior = &candidate;
            break;
        }
    }
    QStringList regression;
    if (prior != nullptr)
        regression = devflow::regressionSummary(*prior, record);
    m_lastRecord = record;
    m_report.notes.append(regression);
    saveRun(record);
    fillHistory();
    m_export->setEnabled(true);

    QString message = record.summary;
    if (cancelled)
        message += QStringLiteral(" \u2014 the partial output was parsed");
    if (!regression.isEmpty())
        message += QStringLiteral(" \u00B7 %1").arg(regression.join(QStringLiteral("; ")));
    m_status->setText(message);
    if (m_hooks.statusMessage)
        m_hooks.statusMessage(message);
}

void TestCenterPanel::fillFailures()
{
    m_failures->clear();
    if (m_report.failureGroups.isEmpty()) {
        auto* item = new QTreeWidgetItem(m_failures);
        item->setText(0, m_report.recognizedOutput
                             ? QStringLiteral("No failures were reported in the parsed output.")
                             : QStringLiteral("The output did not name individual failures."));
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        return;
    }
    for (const devflow::TestFailureGroup& group : m_report.failureGroups) {
        auto* parent = new QTreeWidgetItem(m_failures);
        parent->setText(0, group.message.left(160));
        parent->setText(1, QString::number(group.count));
        parent->setExpanded(true);
        for (const QString& name : group.cases) {
            auto* child = new QTreeWidgetItem(parent);
            child->setText(0, name);
            for (const devflow::TestCaseResult& result : m_report.cases) {
                if (result.name != name || result.file.isEmpty())
                    continue;
                child->setData(0, Qt::UserRole, result.file);
                child->setData(0, Qt::UserRole + 1, result.line);
                child->setToolTip(0, QStringLiteral("%1:%2").arg(result.file).arg(result.line));
                break;
            }
        }
    }
}

QList<devflow::TestRunRecord> TestCenterPanel::loadHistory() const
{
    QList<devflow::TestRunRecord> records;
    for (const auto& item : m_database.devflowItems(QString::fromLatin1(kRunKind))) {
        const QJsonDocument document = QJsonDocument::fromJson(item.second.toUtf8());
        if (!document.isObject())
            continue;
        records.append(devflow::TestRunRecord::fromJson(document.object()));
    }
    std::sort(records.begin(), records.end(),
              [](const devflow::TestRunRecord& a, const devflow::TestRunRecord& b) {
                  return a.startedAt > b.startedAt;
              });
    return records;
}

void TestCenterPanel::saveRun(const devflow::TestRunRecord& record)
{
    m_database.saveDevflowItem(QString::fromLatin1(kRunKind), record.id,
                               QString::fromUtf8(QJsonDocument(record.toJson())
                                                     .toJson(QJsonDocument::Compact)));
    const QList<devflow::TestRunRecord> records = loadHistory();
    if (records.size() <= 60)
        return;
    for (int i = 60; i < records.size(); ++i)
        m_database.removeDevflowItem(QString::fromLatin1(kRunKind), records.at(i).id);
}

void TestCenterPanel::fillHistory()
{
    const QList<devflow::TestRunRecord> records = loadHistory();
    m_history->setRowCount(0);
    int row = 0;
    for (const devflow::TestRunRecord& record : records) {
        m_history->insertRow(row);
        m_history->setItem(row, 0, new QTableWidgetItem(
                                       record.startedAt.toString(QStringLiteral("MM-dd HH:mm"))));
        m_history->setItem(row, 1, new QTableWidgetItem(record.framework));
        m_history->setItem(row, 2, new QTableWidgetItem(record.summary));
        QString regression;
        for (const devflow::TestRunRecord& candidate : records) {
            if (candidate.id == record.id || candidate.framework != record.framework)
                continue;
            if (candidate.startedAt >= record.startedAt)
                continue;
            const QStringList changes = devflow::regressionSummary(candidate, record);
            regression = changes.isEmpty() ? QStringLiteral("no change")
                                           : changes.join(QStringLiteral("; "));
            break;
        }
        m_history->setItem(row, 3, new QTableWidgetItem(regression));
        ++row;
    }
    m_history->resizeColumnsToContents();
}

void TestCenterPanel::exportReport()
{
    if (m_lastRecord.id.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("No test run yet"),
                                 QStringLiteral("Run a test suite before exporting a report."));
        return;
    }
    const QString suggested = QDir(QDir::homePath())
                                  .filePath(QStringLiteral("pkgstudio-test-report.md"));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export test report"), suggested,
        QStringLiteral("Markdown (*.md);;Text (*.txt)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("Cannot write the report"), path);
        return;
    }
    file.write(devflow::testReportMarkdown(m_lastRecord, m_report, m_rawOutput).toUtf8());
    file.close();
    if (m_hooks.statusMessage)
        m_hooks.statusMessage(QStringLiteral("Test report written to %1").arg(path));
}

void TestCenterPanel::showEmptyState(const QString& title, const QString& detail)
{
    m_emptyTitle->setText(title);
    m_emptyDetail->setText(detail);
    m_pages->setCurrentIndex(0);
}

}  // namespace pkg::ui
