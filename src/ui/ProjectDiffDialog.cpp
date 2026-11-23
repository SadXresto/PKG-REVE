#include "ui/ProjectDiffDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>

namespace pkg::ui {

ProjectDiffDialog::ProjectDiffDialog(QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("projectDiffDialog"));
    setWindowTitle(QStringLiteral("Smart project diff"));
    resize(980, 680);
    buildUi();
}

void ProjectDiffDialog::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* intro = new QLabel(
        QStringLiteral("Compares two folder states without modifying either. Files are read "
                       "with bounded hashing; configuration, dependencies and portability "
                       "findings are compared separately. A sampled match is labelled as such."),
        this);
    intro->setObjectName(QStringLiteral("hintLabel"));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto addFolderRow = [this, layout](const QString& label, QLineEdit** edit,
                                       void (ProjectDiffDialog::*browse)()) {
        auto* row = new QHBoxLayout();
        auto* caption = new QLabel(label, this);
        caption->setMinimumWidth(70);
        row->addWidget(caption);
        *edit = new QLineEdit(this);
        (*edit)->setPlaceholderText(QStringLiteral("Choose a project folder\u2026"));
        row->addWidget(*edit, 1);
        auto* button = new QPushButton(QStringLiteral("Browse\u2026"), this);
        connect(button, &QPushButton::clicked, this, browse);
        row->addWidget(button);
        layout->addLayout(row);
    };
    addFolderRow(QStringLiteral("Left"), &m_left, &ProjectDiffDialog::browseLeft);
    addFolderRow(QStringLiteral("Right"), &m_right, &ProjectDiffDialog::browseRight);

    auto* actions = new QHBoxLayout();
    m_compare = new QPushButton(QStringLiteral("Compare"), this);
    m_compare->setDefault(true);
    m_cancel = new QPushButton(QStringLiteral("Cancel"), this);
    m_cancel->setEnabled(false);
    m_export = new QPushButton(QStringLiteral("Export report\u2026"), this);
    m_export->setEnabled(false);
    actions->addWidget(m_compare);
    actions->addWidget(m_cancel);
    actions->addStretch(1);
    actions->addWidget(m_export);
    layout->addLayout(actions);

    m_summary = new QLabel(QStringLiteral("Choose two folders, then press Compare."), this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    auto* tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    m_files = new QTreeWidget(tabs);
    m_files->setHeaderLabels({QStringLiteral("Change"), QStringLiteral("Path"),
                              QStringLiteral("Sizes"), QStringLiteral("Note")});
    m_files->setColumnWidth(0, 90);
    m_files->setColumnWidth(1, 340);
    tabs->addTab(m_files, QStringLiteral("Files"));

    m_config = new QTreeWidget(tabs);
    m_config->setHeaderLabels({QStringLiteral("Change"), QStringLiteral("File"),
                               QStringLiteral("Summary")});
    m_config->setColumnWidth(0, 90);
    tabs->addTab(m_config, QStringLiteral("Configuration"));

    m_dependencies = new QTableWidget(tabs);
    m_dependencies->setColumnCount(4);
    m_dependencies->setHorizontalHeaderLabels({QStringLiteral("Change"),
                                               QStringLiteral("Dependency"),
                                               QStringLiteral("Left"), QStringLiteral("Right")});
    m_dependencies->horizontalHeader()->setStretchLastSection(true);
    m_dependencies->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dependencies->verticalHeader()->setVisible(false);
    tabs->addTab(m_dependencies, QStringLiteral("Dependencies"));

    m_portability = new QTableWidget(tabs);
    m_portability->setColumnCount(5);
    m_portability->setHorizontalHeaderLabels({QStringLiteral("Change"), QStringLiteral("Finding"),
                                              QStringLiteral("Area"), QStringLiteral("Left"),
                                              QStringLiteral("Right")});
    m_portability->horizontalHeader()->setStretchLastSection(true);
    m_portability->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_portability->verticalHeader()->setVisible(false);
    tabs->addTab(m_portability, QStringLiteral("Portability"));

    m_notes = new QTextBrowser(tabs);
    m_notes->setOpenExternalLinks(false);
    tabs->addTab(m_notes, QStringLiteral("Notes"));
    layout->addWidget(tabs, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::clicked, this, &QDialog::accept);
    layout->addWidget(buttons);

    connect(m_compare, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!startComparison() && !error.isEmpty())
            m_summary->setText(error);
    });
    connect(m_cancel, &QPushButton::clicked, this, [this]() {
        m_token.cancel();
        m_summary->setText(QStringLiteral("Cancelling\u2026"));
    });
    connect(m_export, &QPushButton::clicked, this, &ProjectDiffDialog::exportReport);
}

void ProjectDiffDialog::setFolders(const QString& left, const QString& right)
{
    m_left->setText(left);
    m_right->setText(right);
}

void ProjectDiffDialog::browseLeft()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose the left project folder"), m_left->text());
    if (!path.isEmpty())
        m_left->setText(path);
}

void ProjectDiffDialog::browseRight()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose the right project folder"), m_right->text());
    if (!path.isEmpty())
        m_right->setText(path);
}

bool ProjectDiffDialog::startComparison()
{
    if (m_comparing)
        return false;
    const QString left = m_left->text().trimmed();
    const QString right = m_right->text().trimmed();
    if (left.isEmpty() || right.isEmpty()) {
        m_summary->setText(QStringLiteral("Choose both folders first."));
        return false;
    }
    if (!QFileInfo(left).isDir() || !QFileInfo(right).isDir()) {
        m_summary->setText(QStringLiteral("Both paths must be readable folders."));
        return false;
    }
    if (QFileInfo(left).absoluteFilePath() == QFileInfo(right).absoluteFilePath()) {
        m_summary->setText(QStringLiteral("The two folders are the same; nothing to compare."));
        return false;
    }

    m_comparing = true;
    m_compare->setEnabled(false);
    m_cancel->setEnabled(true);
    m_export->setEnabled(false);
    m_summary->setText(QStringLiteral("Comparing\u2026"));
    m_token = pkg::CancelToken();
    const pkg::CancelToken token = m_token;
    m_watcher = std::make_shared<QFutureWatcher<projectdiff::DiffReport>>();
    connect(m_watcher.get(), &QFutureWatcher<projectdiff::DiffReport>::finished, this,
            &ProjectDiffDialog::onFinished);
    m_watcher->setFuture(
        QtConcurrent::run([left, right, token]() {
            return projectdiff::compareProjects(left, right, token);
        }));
    return true;
}

void ProjectDiffDialog::onFinished()
{
    if (!m_watcher)
        return;
    m_report = m_watcher->result();
    m_watcher.reset();
    m_comparing = false;
    m_compare->setEnabled(true);
    m_cancel->setEnabled(false);
    fillTables();
}

void ProjectDiffDialog::fillTables()
{
    m_files->clear();
    for (const projectdiff::FileChange& change : m_report.files) {
        if (change.kind == projectdiff::ChangeKind::Unchanged)
            continue;
        auto* item = new QTreeWidgetItem(m_files);
        item->setText(0, projectdiff::changeKindName(change.kind));
        item->setText(1, change.path);
        item->setText(2, QStringLiteral("%1 \u2192 %2").arg(change.leftSize).arg(change.rightSize));
        QString note = change.note;
        if (change.kind == projectdiff::ChangeKind::Modified && change.binary
            && change.firstDifference > 0 && note.isEmpty()) {
            note = QStringLiteral("binary; first differing byte at 0x%1")
                       .arg(change.firstDifference, 0, 16);
        }
        item->setText(3, note);
        item->setToolTip(3, note);
    }
    m_files->resizeColumnToContents(0);

    m_config->clear();
    for (const projectdiff::ConfigChange& change : m_report.configChanges) {
        auto* item = new QTreeWidgetItem(m_config);
        item->setText(0, projectdiff::changeKindName(change.kind));
        item->setText(1, change.file);
        item->setText(2, change.summary);
        for (const QString& line : change.sampleLines) {
            auto* child = new QTreeWidgetItem(item);
            child->setText(2, line);
        }
    }
    m_config->resizeColumnToContents(1);

    m_dependencies->setRowCount(0);
    int row = 0;
    for (const projectdiff::DependencyChange& change : m_report.dependencyChanges) {
        m_dependencies->insertRow(row);
        m_dependencies->setItem(row, 0, new QTableWidgetItem(change.change));
        m_dependencies->setItem(row, 1, new QTableWidgetItem(change.name));
        m_dependencies->setItem(row, 2, new QTableWidgetItem(change.leftVersion));
        m_dependencies->setItem(row, 3, new QTableWidgetItem(change.rightVersion));
        ++row;
    }
    m_dependencies->resizeColumnsToContents();

    m_portability->setRowCount(0);
    row = 0;
    for (const projectdiff::PortabilityDelta& delta : m_report.portabilityChanges) {
        m_portability->insertRow(row);
        m_portability->setItem(row, 0, new QTableWidgetItem(delta.change));
        m_portability->setItem(row, 1, new QTableWidgetItem(delta.label));
        m_portability->setItem(row, 2, new QTableWidgetItem(delta.area));
        m_portability->setItem(row, 3, new QTableWidgetItem(QString::number(delta.leftOccurrences)));
        m_portability->setItem(row, 4,
                               new QTableWidgetItem(QString::number(delta.rightOccurrences)));
        ++row;
    }
    m_portability->resizeColumnsToContents();

    QString notes;
    for (const QString& note : m_report.notes)
        notes += QStringLiteral("<p>\u2022 %1</p>").arg(note.toHtmlEscaped());
    m_notes->setHtml(notes);

    QString summary = QStringLiteral("%1 added \u00B7 %2 removed \u00B7 %3 modified \u00B7 "
                                     "%4 unchanged")
                          .arg(m_report.added)
                          .arg(m_report.removed)
                          .arg(m_report.modified)
                          .arg(m_report.unchanged);
    if (m_report.uncertain > 0)
        summary += QStringLiteral(" \u00B7 %1 sampled match(es)").arg(m_report.uncertain);
    if (m_report.truncated)
        summary += QStringLiteral(" \u00B7 limited");
    if (m_report.cancelled)
        summary += QStringLiteral(" \u00B7 cancelled");
    m_summary->setText(summary);
    m_export->setEnabled(!m_report.isEmpty() || !m_report.notes.isEmpty());
}

void ProjectDiffDialog::exportReport()
{
    const QString suggested = QDir(QDir::homePath())
                                  .filePath(QStringLiteral("pkgstudio-project-diff.md"));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export the diff report"), suggested,
        QStringLiteral("Markdown (*.md);;Text (*.txt)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("Cannot write the report"), path);
        return;
    }
    file.write(projectdiff::toMarkdown(m_report).toUtf8());
    file.close();
    m_summary->setText(QStringLiteral("Report written to %1").arg(path));
}

}  // namespace pkg::ui
