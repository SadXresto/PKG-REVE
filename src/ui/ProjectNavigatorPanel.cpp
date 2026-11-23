#include "ui/ProjectNavigatorPanel.h"

#include "core/gameforge/PortingIntelligence.h"
#include "ui/Icons.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>

namespace pkg::ui {
namespace {

QString kindOrderKey(projectnav::EntityKind kind)
{
    switch (kind) {
    case projectnav::EntityKind::File:
        return QStringLiteral("1");
    case projectnav::EntityKind::Target:
        return QStringLiteral("2");
    case projectnav::EntityKind::Dependency:
        return QStringLiteral("3");
    case projectnav::EntityKind::BuildFile:
        return QStringLiteral("4");
    case projectnav::EntityKind::SourceReference:
        return QStringLiteral("5");
    case projectnav::EntityKind::Portability:
        return QStringLiteral("6");
    case projectnav::EntityKind::BuildError:
        return QStringLiteral("7");
    }
    return QStringLiteral("8");
}

QString confidenceLabel(projectnav::Confidence confidence)
{
    switch (confidence) {
    case projectnav::Confidence::Confirmed:
        return QStringLiteral("confirmed");
    case projectnav::Confidence::Inferred:
        return QStringLiteral("inferred");
    case projectnav::Confidence::Weak:
        return QStringLiteral("weak");
    }
    return QStringLiteral("unknown");
}

const QVector<projectnav::EntityKind>& allKinds()
{
    static const QVector<projectnav::EntityKind> kinds = {
        projectnav::EntityKind::File,       projectnav::EntityKind::Target,
        projectnav::EntityKind::Dependency, projectnav::EntityKind::BuildFile,
        projectnav::EntityKind::SourceReference, projectnav::EntityKind::Portability,
        projectnav::EntityKind::BuildError,
    };
    return kinds;
}

}  // namespace

ProjectNavigatorPanel::ProjectNavigatorPanel(NavigatorHooks hooks, QWidget* parent)
    : QWidget(parent), m_hooks(std::move(hooks))
{
    setObjectName(QStringLiteral("projectNavigatorPanel"));
    buildUi();
    refresh();
}

ProjectNavigatorPanel::~ProjectNavigatorPanel()
{
    m_token.cancel();
}

QString ProjectNavigatorPanel::projectPath() const
{
    return m_hooks.projectPath ? m_hooks.projectPath() : QString();
}

void ProjectNavigatorPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(this);
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(12, 8, 12, 8);
    headerLayout->setSpacing(2);
    auto* title = new QLabel(QStringLiteral("PROJECT NAVIGATOR"), header);
    title->setObjectName(QStringLiteral("sectionLabel"));
    headerLayout->addWidget(title);
    auto* subtitle = new QLabel(QStringLiteral("Files, targets, dependencies and findings "
                                               "\u2014 every link with its evidence"),
                                header);
    subtitle->setObjectName(QStringLiteral("hintLabel"));
    subtitle->setWordWrap(true);
    headerLayout->addWidget(subtitle);
    layout->addWidget(header);

    auto* toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(8, 0, 8, 6);
    toolbar->setSpacing(6);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("Search the project index\u2026"));
    m_search->setClearButtonEnabled(true);
    toolbar->addWidget(m_search, 1);
    m_refresh = new QPushButton(QStringLiteral("Refresh"), this);
    m_refresh->setToolTip(QStringLiteral("Rescan the project and rebuild the index"));
    toolbar->addWidget(m_refresh);
    layout->addLayout(toolbar);

    auto* filterRow = new QHBoxLayout();
    filterRow->setContentsMargins(8, 0, 8, 6);
    filterRow->setSpacing(6);
    m_filter = new QComboBox(this);
    m_filter->addItem(QStringLiteral("All entities"), QString());
    for (projectnav::EntityKind kind : allKinds())
        m_filter->addItem(projectnav::entityKindName(kind), projectnav::entityKindId(kind));
    filterRow->addWidget(m_filter, 1);
    m_readyLabel = new QLabel(this);
    m_readyLabel->setObjectName(QStringLiteral("hintLabel"));
    filterRow->addWidget(m_readyLabel);
    layout->addLayout(filterRow);

    m_pages = new QStackedWidget(this);
    layout->addWidget(m_pages, 1);

    auto* emptyPage = new QWidget(m_pages);
    auto* emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->setContentsMargins(16, 24, 16, 16);
    emptyLayout->setSpacing(8);
    m_empty = new QLabel(QStringLiteral("No project index yet"), emptyPage);
    m_empty->setObjectName(QStringLiteral("emptyStateTitle"));
    m_empty->setWordWrap(true);
    emptyLayout->addWidget(m_empty);
    m_emptyDetail = new QLabel(emptyPage);
    m_emptyDetail->setObjectName(QStringLiteral("hintLabel"));
    m_emptyDetail->setWordWrap(true);
    emptyLayout->addWidget(m_emptyDetail);
    auto* emptyButton = new QPushButton(QStringLiteral("Open project folder\u2026"), emptyPage);
    connect(emptyButton, &QPushButton::clicked, this, [this]() {
        if (m_hooks.openProject)
            m_hooks.openProject();
    });
    emptyLayout->addWidget(emptyButton);
    emptyLayout->addStretch(1);
    m_pages->addWidget(emptyPage);

    auto* contentPage = new QWidget(m_pages);
    auto* contentLayout = new QVBoxLayout(contentPage);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(4);
    m_tree = new QTreeWidget(contentPage);
    m_tree->setHeaderLabels({QStringLiteral("Entity"), QStringLiteral("Confidence")});
    m_tree->setColumnWidth(0, 230);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    contentLayout->addWidget(m_tree, 3);

    auto* detailSplit = new QSplitter(Qt::Vertical, contentPage);
    m_details = new QTextBrowser(detailSplit);
    m_details->setOpenExternalLinks(false);
    m_related = new QListWidget(detailSplit);
    m_related->setToolTip(QStringLiteral("Double-click a related entity to navigate to it"));
    detailSplit->addWidget(m_details);
    detailSplit->addWidget(m_related);
    detailSplit->setSizes({130, 90});
    contentLayout->addWidget(detailSplit, 2);

    m_summary = new QLabel(contentPage);
    m_summary->setObjectName(QStringLiteral("hintLabel"));
    m_summary->setWordWrap(true);
    contentLayout->addWidget(m_summary);
    m_pages->addWidget(contentPage);

    connect(m_refresh, &QPushButton::clicked, this, &ProjectNavigatorPanel::refresh);
    connect(m_search, &QLineEdit::textChanged, this, &ProjectNavigatorPanel::updateFilter);
    connect(m_filter, &QComboBox::currentIndexChanged, this,
            [this](int) { updateFilter(); });
    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem*, QTreeWidgetItem*) { showSelectedEntity(); });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem*, int) { openSelected(); });
    connect(m_related, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item == nullptr)
            return;
        const QString id = item->data(Qt::UserRole).toString();
        const projectnav::Entity* entity = m_index.find(id);
        if (entity == nullptr)
            return;
        if (entity->file.isEmpty() || !m_hooks.openFile) {
            if (m_hooks.statusMessage)
                m_hooks.statusMessage(QStringLiteral("%1 has no file to open.").arg(entity->label));
            return;
        }
        m_hooks.openFile(entity->file, entity->line);
    });
    m_pages->setCurrentIndex(0);
}

void ProjectNavigatorPanel::refresh()
{
    const QString root = projectPath();
    if (root.isEmpty()) {
        m_token.cancel();
        m_ready = false;
        m_scanning = false;
        m_scan = ScanResult{};
        m_index = projectnav::Index{};
        m_errors.clear();
        m_tree->clear();
        showEmptyState(QStringLiteral("No project folder is open. Open a project to index its "
                                      "files, build targets, dependencies, source references, "
                                      "portability findings and build errors."));
        return;
    }
    if (m_scanning && m_scannedRoot == root)
        return;
    startScan();
}

void ProjectNavigatorPanel::startScan()
{
    m_token.cancel();
    m_token = pkg::CancelToken();
    m_scanning = true;
    m_ready = false;
    m_scannedRoot = projectPath();
    m_readyLabel->setText(QStringLiteral("Indexing\u2026"));
    m_refresh->setEnabled(false);

    const QString root = m_scannedRoot;
    const pkg::CancelToken token = m_token;
    m_watcher = std::make_shared<QFutureWatcher<ScanResult>>();
    connect(m_watcher.get(), &QFutureWatcher<ScanResult>::finished, this,
            &ProjectNavigatorPanel::onScanFinished);
    m_watcher->setFuture(QtConcurrent::run([root, token]() {
        ScanResult result;
        result.scan = gameforge::scanProject(root, token);
        if (result.scan.cancelled || !result.scan.readable())
            return result;
        result.config = gameforge::analyzeBuildConfig(result.scan, token);
        result.graph = gameforge::buildDependencyGraph(result.config, result.scan);
        result.porting = gameforge::analyzePortability(result.scan, token);
        return result;
    }));
}

void ProjectNavigatorPanel::onScanFinished()
{
    if (!m_watcher)
        return;
    const ScanResult result = m_watcher->result();
    m_watcher.reset();
    m_scanning = false;
    m_refresh->setEnabled(true);

    if (result.scan.cancelled) {
        m_readyLabel->setText(QStringLiteral("Indexing cancelled"));
        return;
    }
    if (result.scan.rootPath != m_scannedRoot)
        return;
    if (!result.scan.readable()) {
        showEmptyState(QStringLiteral("The project folder could not be read. Check that it "
                                      "still exists and that you can access it."));
        return;
    }
    m_scan = result;
    m_ready = true;
    rebuildIndex();
    m_pages->setCurrentIndex(1);
}

void ProjectNavigatorPanel::setBuildErrors(const QVector<projectnav::ErrorInput>& errors)
{
    m_errors = errors;
    if (!m_ready)
        return;
    rebuildIndex();
}

void ProjectNavigatorPanel::rebuildIndex()
{
    m_index = projectnav::buildIndex(m_scannedRoot, m_scan.scan, m_scan.config, m_scan.graph,
                                     m_scan.porting, m_errors);
    // Keep the current search but rebuild the tree from the new index.
    rebuildTree();
    m_readyLabel->setText(m_index.truncated ? QStringLiteral("Index limited")
                                            : QStringLiteral("Indexed"));
    const int confirmed = static_cast<int>(std::count_if(
        m_index.relations.cbegin(), m_index.relations.cend(),
        [](const projectnav::Relation& relation) {
            return relation.confidence == projectnav::Confidence::Confirmed;
        }));
    m_summary->setText(
        QStringLiteral("%1 entities \u00B7 %2 relationships (%3 confirmed, %4 inferred or weak) "
                       "\u00B7 %5 file(s), %6 target(s), %7 dependency(ies), %8 error(s)")
            .arg(m_index.entities.size())
            .arg(m_index.relations.size())
            .arg(confirmed)
            .arg(m_index.relations.size() - confirmed)
            .arg(m_index.count(projectnav::EntityKind::File))
            .arg(m_index.count(projectnav::EntityKind::Target))
            .arg(m_index.count(projectnav::EntityKind::Dependency))
            .arg(m_index.count(projectnav::EntityKind::BuildError)));
}

void ProjectNavigatorPanel::rebuildTree()
{
    m_tree->clear();
    m_details->clear();
    m_related->clear();
    if (m_index.isEmpty()) {
        showEmptyState(QStringLiteral("The index is empty. The project may contain no readable "
                                      "files, or the scan reached its limit."));
        return;
    }

    const QString query = m_search->text().trimmed();
    QVector<int> matching;
    if (query.isEmpty()) {
        matching.reserve(m_index.entities.size());
        for (int i = 0; i < m_index.entities.size(); ++i)
            matching.append(i);
    } else {
        matching = m_index.search(query, 500);
    }
    const QString filterId = m_filter->currentData().toString();

    QHash<QString, QTreeWidgetItem*> groups;
    for (int index : matching) {
        const projectnav::Entity& entity = m_index.entities.at(index);
        if (!filterId.isEmpty() && projectnav::entityKindId(entity.kind) != filterId)
            continue;
        const QString groupKey = projectnav::entityKindId(entity.kind);
        QTreeWidgetItem* group = groups.value(groupKey, nullptr);
        if (group == nullptr) {
            group = new QTreeWidgetItem(m_tree);
            group->setText(0, projectnav::entityKindName(entity.kind));
            group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
            group->setExpanded(true);
            groups.insert(groupKey, group);
        }
        auto* item = new QTreeWidgetItem(group);
        item->setText(0, entity.label);
        item->setText(1, confidenceLabel(entity.confidence));
        item->setToolTip(0, entity.detail);
        item->setData(0, Qt::UserRole, entity.id);
        if (!entity.file.isEmpty()) {
            item->setToolTip(1, QStringLiteral("%1:%2").arg(entity.file).arg(entity.line));
        }
    }

    // Keep groups in a stable order regardless of insertion order.
    QList<QTreeWidgetItem*> topLevel;
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it)
        topLevel.append(it.value());
    std::sort(topLevel.begin(), topLevel.end(), [](QTreeWidgetItem* a, QTreeWidgetItem* b) {
        const QString left = a->text(0);
        const QString right = b->text(0);
        return left < right;
    });
    for (QTreeWidgetItem* item : topLevel) {
        m_tree->takeTopLevelItem(m_tree->indexOfTopLevelItem(item));
        m_tree->addTopLevelItem(item);
    }
    const int count = m_tree->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        QTreeWidgetItem* group = m_tree->topLevelItem(i);
        group->setText(0, QStringLiteral("%1 (%2)").arg(group->text(0))
                                .arg(group->childCount()));
        // Expansion must happen after the children are attached.
        group->setExpanded(true);
    }
    if (count == 0) {
        showEmptyState(query.isEmpty()
                           ? QStringLiteral("No entity matches this filter.")
                           : QStringLiteral("No entity matches \"%1\".").arg(query));
        return;
    }
    m_pages->setCurrentIndex(1);
    // Show the first entity's evidence immediately instead of an empty pane.
    if (m_tree->topLevelItemCount() > 0 && m_tree->topLevelItem(0)->childCount() > 0)
        m_tree->setCurrentItem(m_tree->topLevelItem(0)->child(0));
}

void ProjectNavigatorPanel::updateFilter()
{
    if (!m_ready)
        return;
    rebuildTree();
}

void ProjectNavigatorPanel::showSelectedEntity()
{
    m_related->clear();
    QTreeWidgetItem* item = m_tree->currentItem();
    if (item == nullptr || item->parent() == nullptr) {
        m_details->clear();
        return;
    }
    const QString id = item->data(0, Qt::UserRole).toString();
    const projectnav::Entity* entity = m_index.find(id);
    if (entity == nullptr)
        return;

    QString html;
    html += QStringLiteral("<p><b>%1</b><br><span style='color:gray;'>%2 \u00B7 confidence: %3</span></p>")
                .arg(entity->label.toHtmlEscaped(), projectnav::entityKindName(entity->kind),
                     confidenceLabel(entity->confidence));
    html += QStringLiteral("<p>%1</p>").arg(entity->detail.toHtmlEscaped());
    if (!entity->file.isEmpty()) {
        html += QStringLiteral("<p>File: <code>%1</code>%2</p>")
                    .arg(entity->file.toHtmlEscaped(),
                         entity->line > 0 ? QStringLiteral(" line %1").arg(entity->line)
                                          : QString());
    }
    html += QStringLiteral("<p>Evidence: %1</p>").arg(entity->evidence.toHtmlEscaped());
    m_details->setHtml(html);

    const QVector<projectnav::Relation> relations = m_index.relationsOf(id);
    if (relations.isEmpty()) {
        auto* empty = new QListWidgetItem(QStringLiteral("No relationships were found for this "
                                                         "entity."),
                                          m_related);
        empty->setFlags(empty->flags() & ~Qt::ItemIsEnabled);
        return;
    }
    for (const projectnav::Relation& relation : relations) {
        const bool outgoing = relation.from == id;
        const QString otherId = outgoing ? relation.to : relation.from;
        const projectnav::Entity* other = m_index.find(otherId);
        if (other == nullptr)
            continue;
        auto* row = new QListWidgetItem(
            QStringLiteral("%1 %2  \u00B7  %3  \u00B7  %4")
                .arg(outgoing ? QStringLiteral("\u2192") : QStringLiteral("\u2190"),
                     other->label, relation.label, confidenceLabel(relation.confidence)),
            m_related);
        row->setToolTip(relation.evidence);
        row->setData(Qt::UserRole, otherId);
    }
}

void ProjectNavigatorPanel::openSelected()
{
    QTreeWidgetItem* item = m_tree->currentItem();
    if (item == nullptr || item->parent() == nullptr)
        return;
    const QString id = item->data(0, Qt::UserRole).toString();
    const projectnav::Entity* entity = m_index.find(id);
    if (entity == nullptr || entity->file.isEmpty())
        return;
    if (m_hooks.openFile)
        m_hooks.openFile(entity->file, entity->line);
}

void ProjectNavigatorPanel::showEmptyState(const QString& message)
{
    m_empty->setText(QStringLiteral("Nothing to show yet"));
    m_emptyDetail->setText(message);
    m_pages->setCurrentIndex(0);
    if (m_summary->text().isEmpty())
        m_summary->setText(QStringLiteral("Refresh after opening a project folder."));
}

QString ProjectNavigatorPanel::indexSummary() const
{
    if (!m_ready)
        return {};
    return QStringLiteral("%1 entities, %2 relationships")
        .arg(m_index.entities.size())
        .arg(m_index.relations.size());
}

}  // namespace pkg::ui
