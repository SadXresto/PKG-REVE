#include "ui/GameForgePanel.h"

#include "core/FormatUtil.h"
#include "core/Logging.h"
#include "data/ThemeManager.h"
#include "ui/DependencyGraphView.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <functional>
#include <utility>

namespace pkg::ui {
namespace {
constexpr auto kLogCategory = "gameforge";

QString sizeText(qint64 bytes)
{
    return fmt::humanBytes(static_cast<quint64>(std::max<qint64>(0, bytes)));
}

QString section(const QString& title, const QVector<gameforge::IntelInsight>& insights)
{
    if (insights.isEmpty())
        return {};
    QString html = QStringLiteral("<h3>%1</h3><ul>").arg(title.toHtmlEscaped());
    for (const auto& insight : insights) {
        QString entry = QStringLiteral("<li><b>%1</b> \u2014 %2")
                            .arg(insight.title.toHtmlEscaped(),
                                 insight.detail.toHtmlEscaped());
        if (!insight.file.isEmpty()) {
            entry += QStringLiteral(" <i>(%1:%2)</i>")
                         .arg(insight.file.toHtmlEscaped())
                         .arg(insight.line);
        }
        entry += QStringLiteral("</li>");
        html += entry;
    }
    html += QStringLiteral("</ul>");
    return html;
}
} // namespace

GameForgePanel::GameForgePanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("gameForgePanel"));
    buildUi();
    updateSummary();
}

GameForgePanel::~GameForgePanel()
{
    // Workers only read the project; cancelling keeps shutdown quick.
    m_scanToken.cancel();
    m_workToken.cancel();
}

void GameForgePanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* headerRow = new QWidget(this);
    headerRow->setObjectName(QStringLiteral("sideBarHeaderRow"));
    auto* headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(8, 4, 4, 4);
    auto* header = new QLabel(QStringLiteral("GAMEFORGE"), headerRow);
    header->setObjectName(QStringLiteral("sideBarHeader"));
    headerLayout->addWidget(header, 1);
    m_fitButton = new QPushButton(QStringLiteral("Fit"), headerRow);
    m_fitButton->setToolTip(QStringLiteral("Fit the dependency graph to the panel"));
    m_fitButton->setFixedWidth(38);
    connect(m_fitButton, &QPushButton::clicked, this, [this]() {
        if (m_graphView != nullptr)
            m_graphView->resetView();
    });
    headerLayout->addWidget(m_fitButton);
    layout->addWidget(headerRow);

    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(8, 6, 8, 8);
    contentLayout->setSpacing(6);

    m_summary = new QLabel(content);
    m_summary->setObjectName(QStringLiteral("hintLabel"));
    m_summary->setWordWrap(true);
    m_summary->setText(QStringLiteral("No project scanned."));
    contentLayout->addWidget(m_summary);

    m_warnings = new QLabel(content);
    m_warnings->setObjectName(QStringLiteral("hintLabel"));
    m_warnings->setWordWrap(true);
    contentLayout->addWidget(m_warnings);

    auto* actions = new QHBoxLayout();
    actions->setSpacing(6);
    m_openButton = new QPushButton(QStringLiteral("Open project\u2026"), content);
    connect(m_openButton, &QPushButton::clicked, this, &GameForgePanel::openProjectRequested);
    actions->addWidget(m_openButton);
    m_analyzeButton = new QPushButton(QStringLiteral("Re-analyze"), content);
    m_analyzeButton->setEnabled(false);
    m_analyzeButton->setToolTip(QStringLiteral(
        "Re-read the build files, source APIs and dependency graph"));
    connect(m_analyzeButton, &QPushButton::clicked, this, [this]() {
        if (m_scan.readable())
            startAnalysis();
    });
    actions->addWidget(m_analyzeButton);
    m_cancelButton = new QPushButton(QStringLiteral("Cancel"), content);
    m_cancelButton->setEnabled(false);
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        m_scanToken.cancel();
        m_workToken.cancel();
        m_cancelButton->setEnabled(false);
        emit statusMessage(QStringLiteral("GameForge: cancelling\u2026"));
    });
    actions->addWidget(m_cancelButton);
    contentLayout->addLayout(actions);

    m_tabs = new QTabWidget(content);
    m_tabs->setObjectName(QStringLiteral("gameForgeTabs"));

    // --- Inventory ---------------------------------------------------------
    auto* inventory = new QWidget(m_tabs);
    auto* inventoryLayout = new QVBoxLayout(inventory);
    inventoryLayout->setContentsMargins(0, 4, 0, 0);
    inventoryLayout->setSpacing(4);
    m_filter = new QLineEdit(inventory);
    m_filter->setPlaceholderText(QStringLiteral("Filter by name, folder or category"));
    m_filter->setClearButtonEnabled(true);
    connect(m_filter, &QLineEdit::textChanged, this, &GameForgePanel::applyFilter);
    inventoryLayout->addWidget(m_filter);

    m_tree = new QTreeWidget(inventory);
    m_tree->setObjectName(QStringLiteral("gameForgeTree"));
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels(
        {QStringLiteral("Name"), QStringLiteral("Category"), QStringLiteral("Size")});
    m_tree->setUniformRowHeights(true);
    m_tree->setSortingEnabled(false);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        const QString path = item->data(0, Qt::UserRole).toString();
        if (!path.isEmpty())
            emit openFileRequested(path);
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint& position) {
                QTreeWidgetItem* item = m_tree->itemAt(position);
                if (item == nullptr)
                    return;
                const QString path = item->data(0, Qt::UserRole).toString();
                if (path.isEmpty())
                    return;
                QMenu menu(this);
                QAction* openAction = menu.addAction(QStringLiteral("Open in hex editor"));
                QAction* copyAction = menu.addAction(QStringLiteral("Copy path"));
                QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(position));
                if (chosen == openAction)
                    emit openFileRequested(path);
                else if (chosen == copyAction)
                    QApplication::clipboard()->setText(path);
            });
    inventoryLayout->addWidget(m_tree, 1);
    m_tabs->addTab(inventory, QStringLiteral("Inventory"));

    // --- Graph -------------------------------------------------------------
    auto* graphPage = new QWidget(m_tabs);
    auto* graphLayout = new QVBoxLayout(graphPage);
    graphLayout->setContentsMargins(0, 4, 0, 0);
    graphLayout->setSpacing(4);

    auto* graphFilterRow = new QHBoxLayout();
    m_graphFilter = new QLineEdit(graphPage);
    m_graphFilter->setPlaceholderText(QStringLiteral("Search graph nodes"));
    m_graphFilter->setClearButtonEnabled(true);
    connect(m_graphFilter, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (m_graphView != nullptr)
            m_graphView->setFilter(text);
        if (m_graphSummary != nullptr) {
            m_graphSummary->setText(
                QStringLiteral("%1 of %2 nodes shown")
                    .arg(m_graphView != nullptr ? m_graphView->visibleNodeCount() : 0)
                    .arg(m_analysis.graph.nodes.size()));
        }
    });
    graphFilterRow->addWidget(m_graphFilter, 1);
    graphLayout->addLayout(graphFilterRow);

    m_graphSummary = new QLabel(graphPage);
    m_graphSummary->setObjectName(QStringLiteral("hintLabel"));
    m_graphSummary->setWordWrap(true);
    m_graphSummary->setText(QStringLiteral("Scan a project to build the dependency graph."));
    graphLayout->addWidget(m_graphSummary);

    auto* graphSplitter = new QSplitter(Qt::Vertical, graphPage);
    m_graphView = new DependencyGraphView(graphSplitter);
    graphSplitter->addWidget(m_graphView);
    connect(m_graphView, &DependencyGraphView::nodeSelected, this, &GameForgePanel::showNode);
    connect(m_graphView, &DependencyGraphView::nodeActivated, this,
            [this](const QString&) { openSelectedNodeFile(); });

    auto* details = new QFrame(graphSplitter);
    details->setObjectName(QStringLiteral("byteInspector"));
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(8, 6, 8, 6);
    detailsLayout->setSpacing(3);
    m_graphTitle = new QLabel(details);
    m_graphTitle->setObjectName(QStringLiteral("inspectorTitle"));
    m_graphTitle->setText(QStringLiteral("Node details"));
    detailsLayout->addWidget(m_graphTitle);
    m_graphKind = new QLabel(details);
    m_graphKind->setObjectName(QStringLiteral("hintLabel"));
    detailsLayout->addWidget(m_graphKind);
    m_graphWhere = new QLabel(details);
    m_graphWhere->setObjectName(QStringLiteral("hintLabel"));
    m_graphWhere->setWordWrap(true);
    detailsLayout->addWidget(m_graphWhere);
    m_graphDetail = new QLabel(details);
    m_graphDetail->setObjectName(QStringLiteral("hintLabel"));
    m_graphDetail->setWordWrap(true);
    detailsLayout->addWidget(m_graphDetail, 1);
    m_graphOpen = new QPushButton(QStringLiteral("Open evidence file"), details);
    m_graphOpen->setEnabled(false);
    connect(m_graphOpen, &QPushButton::clicked, this, &GameForgePanel::openSelectedNodeFile);
    detailsLayout->addWidget(m_graphOpen, 0, Qt::AlignLeft);
    graphSplitter->addWidget(details);
    graphSplitter->setStretchFactor(0, 4);
    graphSplitter->setStretchFactor(1, 1);
    graphSplitter->setSizes({420, 150});
    graphLayout->addWidget(graphSplitter, 1);

    auto* legend = new QLabel(
        QStringLiteral("Solid edge = stated in a build file \u00b7 dashed = inferred "
                       "(project root, or a link name with no declaration)"),
        graphPage);
    legend->setObjectName(QStringLiteral("hintLabel"));
    legend->setWordWrap(true);
    graphLayout->addWidget(legend);
    m_tabs->addTab(graphPage, QStringLiteral("Graph"));

    // --- Build configuration ----------------------------------------------
    auto* configPage = new QWidget(m_tabs);
    auto* configLayout = new QVBoxLayout(configPage);
    configLayout->setContentsMargins(0, 4, 0, 0);
    configLayout->setSpacing(4);
    m_configTree = new QTreeWidget(configPage);
    m_configTree->setObjectName(QStringLiteral("gameForgeConfigTree"));
    m_configTree->setColumnCount(4);
    m_configTree->setHeaderLabels({QStringLiteral("Kind"), QStringLiteral("Item"),
                                   QStringLiteral("Where"), QStringLiteral("Evidence")});
    m_configTree->setUniformRowHeights(true);
    m_configTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_configTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_configTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_configTree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    connect(m_configTree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        const QString path = item->data(0, Qt::UserRole).toString();
        if (!path.isEmpty())
            emit openFileRequested(path);
    });
    configLayout->addWidget(m_configTree, 1);
    m_configNotes = new QLabel(configPage);
    m_configNotes->setObjectName(QStringLiteral("hintLabel"));
    m_configNotes->setWordWrap(true);
    m_configNotes->setText(QStringLiteral("No build files parsed yet."));
    configLayout->addWidget(m_configNotes);
    m_tabs->addTab(configPage, QStringLiteral("Build config"));

    // --- Portability (API scan) -------------------------------------------
    auto* portability = new QWidget(m_tabs);
    auto* portabilityLayout = new QVBoxLayout(portability);
    portabilityLayout->setContentsMargins(0, 4, 0, 0);
    portabilityLayout->setSpacing(4);
    m_findings = new QTreeWidget(portability);
    m_findings->setObjectName(QStringLiteral("gameForgeFindings"));
    m_findings->setColumnCount(4);
    m_findings->setHeaderLabels({QStringLiteral("Area"), QStringLiteral("API family"),
                                 QStringLiteral("Matches"), QStringLiteral("Example")});
    m_findings->setUniformRowHeights(true);
    m_findings->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_findings->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_findings->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_findings->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    connect(m_findings, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        const QString path = item->data(0, Qt::UserRole).toString();
        if (!path.isEmpty())
            emit openFileRequested(path);
    });
    portabilityLayout->addWidget(m_findings, 1);
    m_findingsNotes = new QTextBrowser(portability);
    m_findingsNotes->setObjectName(QStringLiteral("gameForgeNotes"));
    m_findingsNotes->setMaximumHeight(120);
    m_findingsNotes->setOpenExternalLinks(false);
    portabilityLayout->addWidget(m_findingsNotes);
    m_tabs->addTab(portability, QStringLiteral("Portability"));

    // --- Porting intelligence ---------------------------------------------
    m_intelBrowser = new QTextBrowser(m_tabs);
    m_intelBrowser->setObjectName(QStringLiteral("gameForgeIntel"));
    m_intelBrowser->setOpenExternalLinks(false);
    m_intelBrowser->setHtml(
        QStringLiteral("<p>Scan a project to assemble Windows/Linux, architecture and "
                       "dependency findings.</p>"));
    m_tabs->addTab(m_intelBrowser, QStringLiteral("Porting intel"));

    contentLayout->addWidget(m_tabs, 1);
    layout->addWidget(content, 1);
}

void GameForgePanel::setProjectRoot(const QString& path)
{
    if (path == m_root && m_scan.readable())
        return;
    if (path.isEmpty()) {
        clearProject();
        return;
    }
    m_root = QDir(path).absolutePath();
    startScan();
}

void GameForgePanel::clearProject()
{
    m_scanToken.cancel();
    m_workToken.cancel();
    m_root.clear();
    m_scan = {};
    m_analysis = {};
    m_selectedNode.clear();
    m_tree->clear();
    m_configTree->clear();
    m_findings->clear();
    m_findingsNotes->clear();
    m_intelBrowser->setHtml(QStringLiteral("<p>No project scanned.</p>"));
    if (m_graphView != nullptr)
        m_graphView->setGraph({});
    m_graphSummary->setText(QStringLiteral("Scan a project to build the dependency graph."));
    m_configNotes->setText(QStringLiteral("No build files parsed yet."));
    m_analyzeButton->setEnabled(false);
    setBusy(false, QString());
    updateSummary();
}

void GameForgePanel::rescan()
{
    if (m_root.isEmpty()) {
        emit statusMessage(QStringLiteral("GameForge: open a project folder first."));
        return;
    }
    startScan();
}

bool GameForgePanel::isScanning() const
{
    return (m_scanWatcher && m_scanWatcher->isRunning())
        || (m_analysisWatcher && m_analysisWatcher->isRunning());
}

bool GameForgePanel::hasAnalysis() const
{
    return !m_analysis.config.analyzedFiles.isEmpty() || !m_analysis.graph.nodes.isEmpty();
}

QString GameForgePanel::detectedKind() const
{
    return gameforge::projectKindName(m_scan.kind);
}

QString GameForgePanel::absolutePath(const QString& relative) const
{
    return QDir(m_root).filePath(relative);
}

void GameForgePanel::setBusy(bool busy, const QString& message)
{
    m_cancelButton->setEnabled(busy);
    m_openButton->setEnabled(!busy);
    m_analyzeButton->setEnabled(!busy && m_scan.readable());
    m_fitButton->setEnabled(!busy);
    if (busy && !message.isEmpty())
        m_summary->setText(message);
}

void GameForgePanel::startScan()
{
    if (m_root.isEmpty() || isScanning())
        return;
    m_scanToken = CancelToken();
    m_workToken.cancel();
    m_scan = {};
    m_analysis = {};
    setBusy(true, QStringLiteral("Scanning %1\u2026").arg(m_root));
    m_warnings->clear();
    m_tree->clear();
    m_configTree->clear();
    m_findings->clear();
    m_findingsNotes->clear();
    m_intelBrowser->setHtml(QStringLiteral("<p>Waiting for the scan to finish\u2026</p>"));
    if (m_graphView != nullptr)
        m_graphView->setGraph({});
    m_graphSummary->setText(QStringLiteral("Scanning\u2026"));
    m_configNotes->setText(QStringLiteral("Scanning\u2026"));
    m_selectedNode.clear();
    m_graphTitle->setText(QStringLiteral("Node details"));
    m_graphKind->clear();
    m_graphWhere->clear();
    m_graphDetail->clear();
    m_graphOpen->setEnabled(false);

    const QString root = m_root;
    const int generation = ++m_generation;
    auto watcher = std::make_shared<QFutureWatcher<gameforge::ProjectScan>>();
    connect(watcher.get(), &QFutureWatcher<gameforge::ProjectScan>::finished, this,
            [this, generation]() {
                if (generation == m_generation)
                    onScanFinished();
            });
    watcher->setFuture(QtConcurrent::run([root, token = m_scanToken]() {
        return gameforge::scanProject(root, token);
    }));
    m_scanWatcher = watcher;
}

void GameForgePanel::onScanFinished()
{
    if (!m_scanWatcher)
        return;
    m_scan = m_scanWatcher->result();
    populateTree();
    updateSummary();

    if (!m_scan.readable() || m_scan.files.isEmpty()) {
        setBusy(false, QString());
        m_analyzeButton->setEnabled(false);
        m_graphSummary->setText(QStringLiteral("Nothing to analyze in this folder."));
        m_configNotes->setText(QStringLiteral("Nothing to analyze in this folder."));
        return;
    }
    emit statusMessage(QStringLiteral("GameForge: %1 \u2014 %2 files, %3")
                           .arg(gameforge::projectKindName(m_scan.kind))
                           .arg(m_scan.fileCount())
                           .arg(sizeText(m_scan.totalBytes)));
    startAnalysis();
}

void GameForgePanel::startAnalysis()
{
    if (!m_scan.readable() || (m_analysisWatcher && m_analysisWatcher->isRunning()))
        return;
    m_workToken = CancelToken();
    setBusy(true, QStringLiteral("Inspecting build files and source APIs\u2026"));
    m_graphSummary->setText(QStringLiteral("Reading build files\u2026"));
    m_configNotes->setText(QStringLiteral("Reading build files\u2026"));
    m_intelBrowser->setHtml(QStringLiteral("<p>Reading build files\u2026</p>"));

    const gameforge::ProjectScan scan = m_scan;
    const int generation = m_generation;
    auto watcher = std::make_shared<QFutureWatcher<ProjectAnalysis>>();
    connect(watcher.get(), &QFutureWatcher<ProjectAnalysis>::finished, this,
            [this, generation]() {
                if (generation == m_generation)
                    onAnalysisFinished();
            });
    watcher->setFuture(QtConcurrent::run([scan, token = m_workToken]() {
        ProjectAnalysis analysis;
        analysis.config = gameforge::analyzeBuildConfig(scan, token);
        analysis.graph = gameforge::buildDependencyGraph(analysis.config, scan);
        analysis.porting = gameforge::analyzePortability(scan, token);
        analysis.intel = gameforge::analyzePortingIntelligence(analysis.config, analysis.graph,
                                                               analysis.porting);
        return analysis;
    }));
    m_analysisWatcher = watcher;
}

void GameForgePanel::onAnalysisFinished()
{
    if (!m_analysisWatcher)
        return;
    m_analysis = m_analysisWatcher->result();
    setBusy(false, QString());
    populateGraphTab();
    populateBuildTab();
    populatePortabilityTab();
    populateIntelligenceTab();
    emit statusMessage(
        QStringLiteral("GameForge: %1 nodes, %2 build findings, %3 API families")
            .arg(m_analysis.graph.nodes.size())
            .arg(m_analysis.config.findings.size())
            .arg(m_analysis.porting.findings.size()));
}

void GameForgePanel::populateGraphTab()
{
    if (m_graphView != nullptr)
        m_graphView->setGraph(m_analysis.graph);
    m_selectedNode.clear();
    m_graphTitle->setText(QStringLiteral("Node details"));
    m_graphKind->clear();
    m_graphWhere->clear();
    m_graphDetail->clear();
    m_graphOpen->setEnabled(false);

    QString summary = QStringLiteral("%1 nodes \u00b7 %2 edges (%3 declared, %4 inferred)")
                          .arg(m_analysis.graph.nodes.size())
                          .arg(m_analysis.graph.edges.size())
                          .arg(m_analysis.graph.edges.size()
                               - m_analysis.graph.inferredEdgeCount())
                          .arg(m_analysis.graph.inferredEdgeCount());
    if (!m_analysis.graph.cycles.isEmpty())
        summary += QStringLiteral(" \u00b7 %1 cycle(s)").arg(m_analysis.graph.cycles.size());
    if (!m_analysis.graph.unresolved.isEmpty())
        summary += QStringLiteral(" \u00b7 %1 unresolved")
                       .arg(m_analysis.graph.unresolved.size());
    if (m_analysis.graph.truncated)
        summary += QStringLiteral(" \u00b7 limited");
    m_graphSummary->setText(summary);
}

void GameForgePanel::populateBuildTab()
{
    m_configTree->clear();
    const auto& config = m_analysis.config;
    for (const gameforge::ConfigFinding& finding : config.findings) {
        auto* item = new QTreeWidgetItem(m_configTree);
        item->setText(0, gameforge::configKindName(finding.kind));
        item->setText(1, finding.name);
        item->setText(2, QStringLiteral("%1:%2").arg(finding.file).arg(finding.line));
        item->setText(3, finding.evidence);
        item->setToolTip(3, finding.detail);
        item->setData(0, Qt::UserRole, absolutePath(finding.file));
    }

    QStringList notes;
    if (config.analyzedFiles.isEmpty()) {
        notes.append(QStringLiteral("No supported build file was parsed."));
    } else {
        notes.append(QStringLiteral("%1 build file(s), %2 targets, %3 dependencies, %4 lines.")
                         .arg(config.analyzedFiles.size())
                         .arg(config.targets.size())
                         .arg(config.dependencies.size())
                         .arg(config.linesRead));
        if (!config.projectName.isEmpty()) {
            notes.append(QStringLiteral("Project: %1%2")
                             .arg(config.projectName,
                                  config.projectVersion.isEmpty()
                                      ? QString()
                                      : QStringLiteral(" %1").arg(config.projectVersion)));
        }
        if (!config.engineName.isEmpty()) {
            notes.append(QStringLiteral("Engine: %1%2")
                             .arg(config.engineName,
                                  config.engineVersion.isEmpty()
                                      ? QString()
                                      : QStringLiteral(" %1").arg(config.engineVersion)));
        }
    }
    notes.append(QStringLiteral("Double-click a row to open its build file."));
    notes += config.warnings;
    m_configNotes->setText(notes.join(QLatin1Char('\n')));
}

void GameForgePanel::populatePortabilityTab()
{
    m_findings->clear();
    const auto& report = m_analysis.porting;
    for (const gameforge::PortingFinding& finding : report.findings) {
        auto* item = new QTreeWidgetItem(m_findings);
        item->setText(0, gameforge::signalAreaName(finding.area));
        item->setText(1, finding.label);
        item->setText(2, QString::number(finding.occurrences));
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(3, finding.example);
        item->setToolTip(1, finding.note);
        item->setToolTip(3, QStringLiteral("%1\n\nFiles: %2")
                                .arg(finding.note,
                                     finding.files.join(QStringLiteral(", "))));
        if (!finding.files.isEmpty())
            item->setData(0, Qt::UserRole, absolutePath(finding.files.first()));
    }

    QString html = QStringLiteral("<b>%1 API families in %2 files \u00b7 %3 usually need "
                                  "platform work</b><ul>")
                       .arg(report.findings.size())
                       .arg(report.scannedFiles)
                       .arg(report.blockingCount());
    for (const QString& note : report.notes)
        html += QStringLiteral("<li>%1</li>").arg(note.toHtmlEscaped());
    html += QStringLiteral("</ul><p>Double-click a row to open its first match in the hex "
                           "editor. Nothing in the project was modified.</p>");
    m_findingsNotes->setHtml(html);
}

void GameForgePanel::populateIntelligenceTab()
{
    const auto& intel = m_analysis.intel;
    QString html;
    if (intel.total() == 0) {
        html = QStringLiteral("<p>No Windows, Linux, architecture or API evidence was found in "
                              "the inspected files.</p>");
    } else {
        html = section(QStringLiteral("Windows"), intel.windows)
            + section(QStringLiteral("Linux / POSIX"), intel.linuxPlatform)
            + section(QStringLiteral("Architecture"), intel.architecture)
            + section(QStringLiteral("Graphics and input"), intel.graphicsInput)
            + section(QStringLiteral("External dependency availability"),
                      intel.externalDependencies)
            + section(QStringLiteral("Build configuration recommendations"),
                      intel.recommendations);
    }
    html += QStringLiteral("<h3>Graph health</h3><ul>");
    if (m_analysis.graph.cycles.isEmpty()) {
        html += QStringLiteral("<li>No dependency cycle was found between targets.</li>");
    } else {
        for (const QString& cycle : m_analysis.graph.cycles)
            html += QStringLiteral("<li>Cycle: %1</li>").arg(cycle.toHtmlEscaped());
    }
    if (m_analysis.graph.unresolved.isEmpty()) {
        html += QStringLiteral("<li>Every linked dependency has a declaration.</li>");
    } else {
        for (const QString& unresolved : std::as_const(m_analysis.graph.unresolved).mid(0, 20))
            html += QStringLiteral("<li>Unresolved: %1</li>").arg(unresolved.toHtmlEscaped());
    }
    html += QStringLiteral("</ul><h3>Limits</h3><ul>");
    for (const QString& note : intel.notes)
        html += QStringLiteral("<li>%1</li>").arg(note.toHtmlEscaped());
    for (const QString& warning : m_analysis.graph.warnings)
        html += QStringLiteral("<li>%1</li>").arg(warning.toHtmlEscaped());
    html += QStringLiteral("</ul>");
    m_intelBrowser->setHtml(html);
}

void GameForgePanel::showNode(const QString& id)
{
    const gameforge::GraphNode* node = m_analysis.graph.find(id);
    if (node == nullptr)
        return;
    m_selectedNode = id;
    m_graphTitle->setText(node->label);
    m_graphKind->setText(
        QStringLiteral("%1 \u00b7 %2")
            .arg(gameforge::nodeKindName(node->kind),
                 node->confirmed ? QStringLiteral("declared")
                                 : QStringLiteral("inferred")));
    m_graphWhere->setText(node->file.isEmpty()
                              ? QString()
                              : QStringLiteral("%1:%2").arg(node->file).arg(node->line));
    m_graphDetail->setText(node->detail);
    m_graphOpen->setEnabled(!node->file.isEmpty());
}

void GameForgePanel::openSelectedNodeFile()
{
    const gameforge::GraphNode* node = m_analysis.graph.find(m_selectedNode);
    if (node == nullptr || node->file.isEmpty())
        return;
    emit openFileRequested(absolutePath(node->file));
}

void GameForgePanel::populateTree()
{
    m_tree->clear();
    QHash<QString, QTreeWidgetItem*> folders;

    std::function<QTreeWidgetItem*(const QString&)> folderItem =
        [&](const QString& path) -> QTreeWidgetItem* {
        const auto existing = folders.constFind(path);
        if (existing != folders.constEnd())
            return existing.value();
        const int slash = path.lastIndexOf(QLatin1Char('/'));
        QTreeWidgetItem* parent = slash < 0 ? nullptr : folderItem(path.left(slash));
        auto* item = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, slash < 0 ? path : path.mid(slash + 1));
        item->setToolTip(0, path);
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        folders.insert(path, item);
        return item;
    };

    for (const gameforge::ProjectFile& file : m_scan.files) {
        const int slash = file.relativePath.lastIndexOf(QLatin1Char('/'));
        QTreeWidgetItem* parent =
            slash < 0 ? nullptr : folderItem(file.relativePath.left(slash));
        auto* item =
            parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, slash < 0 ? file.relativePath : file.relativePath.mid(slash + 1));
        item->setText(1, gameforge::categoryName(file.category));
        item->setText(2, sizeText(file.size));
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        item->setToolTip(0, absolutePath(file.relativePath));
        item->setData(0, Qt::UserRole, absolutePath(file.relativePath));
    }

    std::function<void(QTreeWidgetItem*)> sortBranch = [&](QTreeWidgetItem* parent) {
        parent->sortChildren(0, Qt::AscendingOrder);
        for (int i = 0; i < parent->childCount(); ++i)
            sortBranch(parent->child(i));
    };
    sortBranch(m_tree->invisibleRootItem());
    m_tree->expandToDepth(1);
    applyFilter(m_filter->text());
}

void GameForgePanel::applyFilter(const QString& text)
{
    const QString needle = text.trimmed();
    std::function<bool(QTreeWidgetItem*)> apply = [&](QTreeWidgetItem* item) -> bool {
        bool childVisible = false;
        for (int i = 0; i < item->childCount(); ++i)
            childVisible = apply(item->child(i)) || childVisible;
        const bool selfVisible =
            needle.isEmpty() || item->text(0).contains(needle, Qt::CaseInsensitive)
            || item->text(1).contains(needle, Qt::CaseInsensitive);
        const bool visible = selfVisible || childVisible;
        item->setHidden(!visible);
        if (childVisible && !needle.isEmpty())
            item->setExpanded(true);
        return visible;
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        apply(m_tree->topLevelItem(i));
}

void GameForgePanel::updateSummary()
{
    if (!m_scan.readable()) {
        m_summary->setText(QStringLiteral("No project scanned. Open a game project folder to see "
                                          "its structure, build files, dependency graph and "
                                          "portability evidence."));
        m_warnings->clear();
        return;
    }
    QString summary = QStringLiteral("%1 \u00b7 %2 \u00b7 %3 files in %4 folders \u00b7 %5")
                          .arg(m_scan.displayName,
                               gameforge::projectKindName(m_scan.kind))
                          .arg(m_scan.fileCount())
                          .arg(m_scan.directoryCount)
                          .arg(sizeText(m_scan.totalBytes));
    if (!m_scan.kindEvidence.isEmpty())
        summary += QStringLiteral(" \u00b7 marker: %1").arg(m_scan.kindEvidence);
    if (m_scan.truncated || m_scan.cancelled)
        summary += QStringLiteral(" \u00b7 partial");
    m_summary->setText(summary);
    m_warnings->setText(m_scan.warnings.join(QLatin1Char('\n')));
}

} // namespace pkg::ui
