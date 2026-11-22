#include "ui/GameForgePanel.h"

#include "core/FormatUtil.h"
#include "core/Logging.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <functional>

namespace pkg::ui {
namespace {
constexpr auto kLogCategory = "gameforge";

QString sizeText(qint64 bytes)
{
    return fmt::humanBytes(static_cast<quint64>(std::max<qint64>(0, bytes)));
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
    m_reportToken.cancel();
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
    m_rescanButton = new QPushButton(QStringLiteral("\u21BB"), headerRow);
    m_rescanButton->setToolTip(QStringLiteral("Scan the project folder again"));
    m_rescanButton->setFixedWidth(30);
    m_rescanButton->setEnabled(false);
    headerLayout->addWidget(m_rescanButton);
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
    connect(m_openButton, &QPushButton::clicked, this,
            &GameForgePanel::openProjectRequested);
    actions->addWidget(m_openButton);
    m_analyzeButton = new QPushButton(QStringLiteral("Analyze portability"), content);
    m_analyzeButton->setEnabled(false);
    m_analyzeButton->setToolTip(
        QStringLiteral("Search source files for platform-specific APIs"));
    connect(m_analyzeButton, &QPushButton::clicked, this,
            &GameForgePanel::startPortabilityAnalysis);
    actions->addWidget(m_analyzeButton);
    m_cancelButton = new QPushButton(QStringLiteral("Cancel"), content);
    m_cancelButton->setEnabled(false);
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        m_scanToken.cancel();
        m_reportToken.cancel();
        m_cancelButton->setEnabled(false);
        emit statusMessage(QStringLiteral("GameForge: cancelling\u2026"));
    });
    actions->addWidget(m_cancelButton);
    contentLayout->addLayout(actions);

    m_tabs = new QTabWidget(content);
    m_tabs->setObjectName(QStringLiteral("gameForgeTabs"));

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
    m_tree->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Category"),
                             QStringLiteral("Size")});
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(false);
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

    auto* portability = new QWidget(m_tabs);
    auto* portabilityLayout = new QVBoxLayout(portability);
    portabilityLayout->setContentsMargins(0, 4, 0, 0);
    portabilityLayout->setSpacing(4);
    m_reportHeader = new QLabel(portability);
    m_reportHeader->setObjectName(QStringLiteral("hintLabel"));
    m_reportHeader->setWordWrap(true);
    m_reportHeader->setText(QStringLiteral("Run the analysis to see platform API evidence."));
    portabilityLayout->addWidget(m_reportHeader);

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

    m_reportNotes = new QTextBrowser(portability);
    m_reportNotes->setObjectName(QStringLiteral("gameForgeNotes"));
    m_reportNotes->setMaximumHeight(120);
    m_reportNotes->setOpenExternalLinks(false);
    portabilityLayout->addWidget(m_reportNotes);
    m_tabs->addTab(portability, QStringLiteral("Portability"));

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
    m_reportToken.cancel();
    m_root.clear();
    m_scan = {};
    m_report = {};
    m_hasReport = false;
    m_tree->clear();
    m_findings->clear();
    m_reportHeader->setText(QStringLiteral("Run the analysis to see platform API evidence."));
    m_reportNotes->clear();
    m_rescanButton->setEnabled(false);
    m_analyzeButton->setEnabled(false);
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
    return m_scanWatcher
        && m_scanWatcher->isRunning();
}

QString GameForgePanel::detectedKind() const
{
    return gameforge::projectKindName(m_scan.kind);
}

void GameForgePanel::startScan()
{
    if (m_root.isEmpty() || isScanning())
        return;
    m_scanToken = CancelToken();
    m_cancelButton->setEnabled(true);
    m_rescanButton->setEnabled(false);
    m_summary->setText(QStringLiteral("Scanning %1\u2026").arg(m_root));
    m_warnings->clear();

    const QString root = m_root;
    m_scan = {};
    m_hasReport = false;
    m_findings->clear();
    m_reportHeader->setText(QStringLiteral("Run the analysis to see platform API evidence."));
    m_reportNotes->clear();
    m_analyzeButton->setEnabled(false);

    auto watcher = std::make_shared<QFutureWatcher<gameforge::ProjectScan>>();
    connect(watcher.get(), &QFutureWatcher<gameforge::ProjectScan>::finished, this,
            &GameForgePanel::onScanFinished);
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
    m_cancelButton->setEnabled(false);
    m_rescanButton->setEnabled(true);
    m_analyzeButton->setEnabled(m_scan.readable() && !m_scan.files.isEmpty());
    populateTree();
    updateSummary();
    emit statusMessage(QStringLiteral("GameForge: %1 \u2014 %2 files, %3")
                           .arg(gameforge::projectKindName(m_scan.kind))
                           .arg(m_scan.fileCount())
                           .arg(sizeText(m_scan.totalBytes)));
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
        QTreeWidgetItem* parent =
            slash < 0 ? nullptr : folderItem(path.left(slash));
        auto* item = parent != nullptr ? new QTreeWidgetItem(parent)
                                       : new QTreeWidgetItem(m_tree);
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
        auto* item = parent != nullptr ? new QTreeWidgetItem(parent)
                                       : new QTreeWidgetItem(m_tree);
        item->setText(0, slash < 0 ? file.relativePath : file.relativePath.mid(slash + 1));
        item->setText(1, gameforge::categoryName(file.category));
        item->setText(2, sizeText(file.size));
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        item->setToolTip(0, QDir(m_scan.rootPath).filePath(file.relativePath));
        item->setData(0, Qt::UserRole,
                      QDir(m_scan.rootPath).filePath(file.relativePath));
    }

    // Files before folders at each level, then alphabetical.
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
        m_summary->setText(QStringLiteral("No project scanned. Open a game project folder to "
                                          "see its structure, sizes and portability evidence."));
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

void GameForgePanel::startPortabilityAnalysis()
{
    if (!m_scan.readable() || m_scan.files.isEmpty()) {
        emit statusMessage(QStringLiteral("GameForge: scan a project first."));
        return;
    }
    if (m_reportWatcher && m_reportWatcher->isRunning())
        return;
    m_reportToken = CancelToken();
    m_cancelButton->setEnabled(true);
    m_analyzeButton->setEnabled(false);
    m_reportHeader->setText(QStringLiteral("Reading source-like files\u2026"));

    const gameforge::ProjectScan scan = m_scan;
    m_reportWatcher = std::make_shared<QFutureWatcher<gameforge::PortingReport>>();
    connect(m_reportWatcher.get(), &QFutureWatcher<gameforge::PortingReport>::finished, this,
            &GameForgePanel::onPortabilityFinished);
    m_reportWatcher->setFuture(QtConcurrent::run([scan, token = m_reportToken]() {
        return gameforge::analyzePortability(scan, token);
    }));
}

void GameForgePanel::onPortabilityFinished()
{
    if (!m_reportWatcher)
        return;
    m_report = m_reportWatcher->result();
    m_hasReport = true;
    m_cancelButton->setEnabled(false);
    m_analyzeButton->setEnabled(true);
    m_findings->clear();

    for (const gameforge::PortingFinding& finding : m_report.findings) {
        auto* item = new QTreeWidgetItem(m_findings);
        item->setText(0, gameforge::signalAreaName(finding.area));
        item->setText(1, finding.label);
        item->setText(2, QString::number(finding.occurrences));
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(3, finding.example);
        item->setToolTip(1, finding.note);
        item->setToolTip(3,
                         QStringLiteral("%1\n\nFiles: %2")
                             .arg(finding.note,
                                  finding.files.join(QStringLiteral(", "))));
        if (!finding.files.isEmpty())
            item->setData(0, Qt::UserRole,
                          QDir(m_scan.rootPath).filePath(finding.files.first()));
    }

    m_reportHeader->setText(
        QStringLiteral("%1 API families in %2 files \u00b7 %3 usually need platform work")
            .arg(m_report.findings.size())
            .arg(m_report.scannedFiles)
            .arg(m_report.blockingCount()));

    QString html = QStringLiteral("<b>Limits and evidence</b><ul>");
    for (const QString& note : m_report.notes)
        html += QStringLiteral("<li>%1</li>").arg(note.toHtmlEscaped());
    html += QStringLiteral("</ul><p>Double-click a row to open its first match in the hex "
                           "editor. Nothing in the project was modified.</p>");
    m_reportNotes->setHtml(html);

    m_tabs->setCurrentIndex(1);
    emit statusMessage(QStringLiteral("GameForge: portability analysis finished (%1 families)")
                           .arg(m_report.findings.size()));
}

} // namespace pkg::ui
