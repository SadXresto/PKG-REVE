#include "ui/PluginStudioPanel.h"

#include "data/Settings.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace pkg::ui {
namespace {

QString dependenciesText(const QVector<plugins::PluginDependency>& dependencies)
{
    QStringList lines;
    for (const plugins::PluginDependency& dependency : dependencies)
        lines.append(QStringLiteral("%1 %2").arg(dependency.id, dependency.minimumVersion));
    return lines.join(QLatin1Char('\n'));
}

}  // namespace

PluginStudioPanel::PluginStudioPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("pluginStudioPanel"));
    buildUi();
    refresh();
}

QString PluginStudioPanel::pluginsDirectory()
{
    return QDir(Settings::dataDirectory()).filePath(QStringLiteral("plugins"));
}

void PluginStudioPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(this);
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(12, 8, 12, 8);
    headerLayout->setSpacing(2);
    auto* title = new QLabel(QStringLiteral("EXTENSION STUDIO"), header);
    title->setObjectName(QStringLiteral("sectionLabel"));
    headerLayout->addWidget(title);
    auto* subtitle = new QLabel(QStringLiteral("Manifest SDK \u00B7 API version 1 \u00B7 plugins "
                                               "are validated and packaged, never executed"),
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
    auto* emptyTitle = new QLabel(QStringLiteral("No plugin workspace yet"), emptyPage);
    emptyTitle->setObjectName(QStringLiteral("emptyStateTitle"));
    emptyLayout->addWidget(emptyTitle);
    m_emptyDetail = new QLabel(
        QStringLiteral("Create the example plugin to get a valid manifest, a declarative action "
                       "file and the SDK README. Plugins live under %1.")
            .arg(pluginsDirectory()),
        emptyPage);
    m_emptyDetail->setObjectName(QStringLiteral("hintLabel"));
    m_emptyDetail->setWordWrap(true);
    emptyLayout->addWidget(m_emptyDetail);
    auto* exampleButton = new QPushButton(QStringLiteral("Create example plugin"), emptyPage);
    connect(exampleButton, &QPushButton::clicked, this, &PluginStudioPanel::createExample);
    emptyLayout->addWidget(exampleButton);
    emptyLayout->addStretch(1);
    m_pages->addWidget(emptyPage);

    auto* contentPage = new QWidget(m_pages);
    auto* contentLayout = new QVBoxLayout(contentPage);
    contentLayout->setContentsMargins(8, 0, 8, 8);
    contentLayout->setSpacing(5);

    m_pluginList = new QListWidget(contentPage);
    m_pluginList->setMaximumHeight(84);
    m_pluginList->setToolTip(QStringLiteral("Manifests found in the user plugin folder"));
    contentLayout->addWidget(m_pluginList);

    m_tabs = new QTabWidget(contentPage);
    m_tabs->setDocumentMode(true);

    auto* formScroll = new QScrollArea(m_tabs);
    formScroll->setWidgetResizable(true);
    auto* formHost = new QWidget(formScroll);
    auto* formLayout = new QVBoxLayout(formHost);
    formLayout->setContentsMargins(6, 6, 6, 6);
    formLayout->setSpacing(5);
    auto* form = new QFormLayout();
    form->setSpacing(4);
    m_id = new QLineEdit(formHost);
    m_id->setPlaceholderText(QStringLiteral("com.example.plugin"));
    m_name = new QLineEdit(formHost);
    m_version = new QLineEdit(formHost);
    m_version->setPlaceholderText(QStringLiteral("0.1.0"));
    m_apiVersion = new QLineEdit(formHost);
    m_apiVersion->setReadOnly(true);
    m_apiVersion->setToolTip(QStringLiteral("This build supports API version 1"));
    m_author = new QLineEdit(formHost);
    m_entry = new QLineEdit(formHost);
    m_entry->setPlaceholderText(QStringLiteral("actions.json"));
    m_description = new QLineEdit(formHost);
    m_capabilities = new QLineEdit(formHost);
    m_capabilities->setPlaceholderText(QStringLiteral("declarative-commands, inspector"));
    form->addRow(QStringLiteral("Id"), m_id);
    form->addRow(QStringLiteral("Name"), m_name);
    form->addRow(QStringLiteral("Version"), m_version);
    form->addRow(QStringLiteral("API version"), m_apiVersion);
    form->addRow(QStringLiteral("Author"), m_author);
    form->addRow(QStringLiteral("Entry"), m_entry);
    form->addRow(QStringLiteral("Description"), m_description);
    form->addRow(QStringLiteral("Capabilities"), m_capabilities);
    formLayout->addLayout(form);

    auto* permissionGroup = new QGroupBox(QStringLiteral("Declared permissions"), formHost);
    auto* permissionLayout = new QVBoxLayout(permissionGroup);
    permissionLayout->setSpacing(2);
    for (const QString& permission : plugins::PluginManifest::knownPermissions()) {
        auto* box = new QCheckBox(QStringLiteral("%1 \u2014 %2")
                                      .arg(permission,
                                           plugins::PluginManifest::permissionDescription(
                                               permission)),
                                  permissionGroup);
        box->setProperty("permissionId", permission);
        permissionLayout->addWidget(box);
        m_permissionBoxes.append(box);
    }
    formLayout->addWidget(permissionGroup);

    auto* dependencyGroup = new QGroupBox(QStringLiteral("Dependencies (id minimumVersion)"),
                                          formHost);
    auto* dependencyLayout = new QVBoxLayout(dependencyGroup);
    m_dependencies = new QPlainTextEdit(dependencyGroup);
    m_dependencies->setMaximumHeight(64);
    m_dependencies->setPlaceholderText(QStringLiteral("com.example.base 1.0.0"));
    dependencyLayout->addWidget(m_dependencies);
    formLayout->addWidget(dependencyGroup);

    auto* buttons = new QHBoxLayout();
    auto* newButton = new QPushButton(QStringLiteral("New manifest"), formHost);
    m_save = new QPushButton(QStringLiteral("Save"), formHost);
    m_validate = new QPushButton(QStringLiteral("Validate"), formHost);
    m_export = new QPushButton(QStringLiteral("Export bundle\u2026"), formHost);
    buttons->addWidget(newButton);
    buttons->addWidget(m_save);
    buttons->addWidget(m_validate);
    buttons->addStretch(1);
    buttons->addWidget(m_export);
    formLayout->addLayout(buttons);
    formLayout->addStretch(1);
    formScroll->setWidget(formHost);
    m_tabs->addTab(formScroll, QStringLiteral("Manifest"));

    m_validation = new QPlainTextEdit(m_tabs);
    m_validation->setReadOnly(true);
    m_validation->setPlaceholderText(QStringLiteral("Validation results appear here."));
    m_tabs->addTab(m_validation, QStringLiteral("Validation"));

    auto* docs = new QPlainTextEdit(m_tabs);
    docs->setReadOnly(true);
    docs->setPlainText(plugins::sdkReadme());
    m_tabs->addTab(docs, QStringLiteral("SDK"));
    contentLayout->addWidget(m_tabs, 1);

    m_status = new QLabel(contentPage);
    m_status->setObjectName(QStringLiteral("hintLabel"));
    m_status->setWordWrap(true);
    contentLayout->addWidget(m_status);
    m_pages->addWidget(contentPage);

    connect(m_pluginList, &QListWidget::currentRowChanged, this, [this](int) { loadSelected(); });
    connect(newButton, &QPushButton::clicked, this, [this]() {
        m_currentDirectory.clear();
        clearForm();
        m_tabs->setCurrentIndex(0);
        setStatus(QStringLiteral("New manifest: fill in the fields and press Save."));
    });
    connect(m_save, &QPushButton::clicked, this, &PluginStudioPanel::saveCurrent);
    connect(m_validate, &QPushButton::clicked, this, &PluginStudioPanel::validateCurrent);
    connect(m_export, &QPushButton::clicked, this, &PluginStudioPanel::exportCurrent);
    m_apiVersion->setText(plugins::PluginManifest::supportedApiVersion());
    showEmptyState(true);
}

void PluginStudioPanel::refresh()
{
    loadInstalled();
}

void PluginStudioPanel::loadInstalled()
{
    m_installed.clear();
    m_pluginList->clear();
    const QDir root(pluginsDirectory());
    if (!root.exists()) {
        showEmptyState(true);
        return;
    }
    const QFileInfoList entries = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : entries) {
        const QString manifestPath = QDir(entry.absoluteFilePath())
                                         .filePath(QStringLiteral("plugin.json"));
        if (!QFileInfo::exists(manifestPath))
            continue;
        const auto manifest = plugins::loadManifest(manifestPath);
        if (!manifest)
            continue;
        m_installed.append(manifest.value());
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  %2").arg(manifest.value().name, manifest.value().version),
            m_pluginList);
        item->setToolTip(entry.absoluteFilePath());
        item->setData(Qt::UserRole, entry.absoluteFilePath());
    }
    if (m_installed.isEmpty()) {
        showEmptyState(true);
        return;
    }
    showEmptyState(false);
    m_pluginList->setCurrentRow(0);
}

void PluginStudioPanel::showEmptyState(bool empty)
{
    m_pages->setCurrentIndex(empty ? 0 : 1);
}

QString PluginStudioPanel::selectedDirectory() const
{
    QListWidgetItem* item = m_pluginList->currentItem();
    return item != nullptr ? item->data(Qt::UserRole).toString() : QString();
}

void PluginStudioPanel::loadSelected()
{
    const QString directory = selectedDirectory();
    if (directory.isEmpty())
        return;
    m_currentDirectory = directory;
    const auto manifest = plugins::loadManifest(
        QDir(directory).filePath(QStringLiteral("plugin.json")));
    if (!manifest) {
        setStatus(QStringLiteral("Cannot read this manifest: %1")
                      .arg(manifest.error().detail.isEmpty() ? manifest.error().message
                                                             : manifest.error().detail));
        clearForm();
        return;
    }
    fillForm(manifest.value());
    setStatus(QStringLiteral("Editing %1").arg(directory));
}

void PluginStudioPanel::clearForm()
{
    m_id->clear();
    m_name->clear();
    m_version->setText(QStringLiteral("0.1.0"));
    m_apiVersion->setText(plugins::PluginManifest::supportedApiVersion());
    m_author->clear();
    m_entry->setText(QStringLiteral("plugin.json"));
    m_description->clear();
    m_capabilities->clear();
    m_dependencies->clear();
    for (QCheckBox* box : m_permissionBoxes)
        box->setChecked(false);
    m_validation->clear();
}

void PluginStudioPanel::fillForm(const plugins::PluginManifest& manifest)
{
    m_id->setText(manifest.id);
    m_name->setText(manifest.name);
    m_version->setText(manifest.version);
    m_apiVersion->setText(manifest.apiVersion);
    m_author->setText(manifest.author);
    m_entry->setText(manifest.entry);
    m_description->setText(manifest.description);
    m_capabilities->setText(manifest.capabilities.join(QStringLiteral(", ")));
    m_dependencies->setPlainText(dependenciesText(manifest.dependencies));
    for (QCheckBox* box : m_permissionBoxes) {
        box->setChecked(manifest.permissions.contains(box->property("permissionId").toString()));
    }
}

plugins::PluginManifest PluginStudioPanel::manifestFromForm() const
{
    plugins::PluginManifest manifest;
    manifest.id = m_id->text().trimmed();
    manifest.name = m_name->text().trimmed();
    manifest.version = m_version->text().trimmed();
    manifest.apiVersion = m_apiVersion->text().trimmed();
    manifest.author = m_author->text().trimmed();
    manifest.entry = m_entry->text().trimmed();
    manifest.description = m_description->text().trimmed();
    for (const QString& capability : m_capabilities->text().split(QLatin1Char(','))) {
        const QString trimmed = capability.trimmed();
        if (!trimmed.isEmpty())
            manifest.capabilities.append(trimmed);
    }
    for (QCheckBox* box : m_permissionBoxes) {
        if (box->isChecked())
            manifest.permissions.append(box->property("permissionId").toString());
    }
    for (const QString& line : m_dependencies->toPlainText().split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        const QStringList parts = trimmed.split(QRegularExpression(QStringLiteral("\\s+")),
                                               Qt::SkipEmptyParts);
        plugins::PluginDependency dependency;
        dependency.id = parts.value(0);
        dependency.minimumVersion = parts.value(1);
        manifest.dependencies.append(dependency);
    }
    return manifest;
}

void PluginStudioPanel::saveCurrent()
{
    const plugins::PluginManifest manifest = manifestFromForm();
    if (!plugins::isValidPluginId(manifest.id)) {
        showValidation(plugins::validateManifest(manifest, m_currentDirectory, m_installed));
        setStatus(QStringLiteral("Set a valid id before saving: lowercase letters, digits, dots, "
                                 "hyphens or underscores."));
        return;
    }
    QString directory = m_currentDirectory;
    if (directory.isEmpty())
        directory = QDir(pluginsDirectory()).filePath(manifest.id);
    if (!QDir().mkpath(directory)) {
        setStatus(QStringLiteral("Cannot create %1").arg(directory));
        return;
    }
    const plugins::ValidationReport report =
        plugins::validateManifest(manifest, directory, m_installed);
    showValidation(report);
    const Status saved = plugins::saveManifest(
        QDir(directory).filePath(QStringLiteral("plugin.json")), manifest);
    if (!saved) {
        setStatus(saved.error().detail.isEmpty() ? saved.error().message : saved.error().detail);
        return;
    }
    m_currentDirectory = directory;
    loadInstalled();
    for (int i = 0; i < m_pluginList->count(); ++i) {
        if (m_pluginList->item(i)->data(Qt::UserRole).toString() == directory) {
            m_pluginList->setCurrentRow(i);
            break;
        }
    }
    setStatus(report.valid()
                  ? QStringLiteral("Saved %1. %2").arg(manifest.id, report.summary())
                  : QStringLiteral("Saved %1, but validation still reports problems. See the "
                                   "Validation tab.")
                        .arg(manifest.id));
}

void PluginStudioPanel::validateCurrent()
{
    const plugins::PluginManifest manifest = manifestFromForm();
    const plugins::ValidationReport report =
        plugins::validateManifest(manifest, m_currentDirectory, m_installed);
    showValidation(report);
    setStatus(report.summary());
    m_tabs->setCurrentIndex(0);
}

void PluginStudioPanel::showValidation(const plugins::ValidationReport& report)
{
    QStringList lines;
    lines << report.summary();
    if (!report.errors().isEmpty()) {
        lines << QString();
        lines << QStringLiteral("Errors:");
        for (const QString& error : report.errors())
            lines << QStringLiteral("  \u2717 %1").arg(error);
    }
    if (!report.warnings().isEmpty()) {
        lines << QString();
        lines << QStringLiteral("Warnings:");
        for (const QString& warning : report.warnings())
            lines << QStringLiteral("  ! %1").arg(warning);
    }
    if (report.valid() && report.warnings().isEmpty())
        lines << QStringLiteral("Manifest structure, entry file, permissions and dependencies "
                                "all pass.");
    m_validation->setPlainText(lines.join(QLatin1Char('\n')));
}

void PluginStudioPanel::exportCurrent()
{
    const plugins::PluginManifest manifest = manifestFromForm();
    if (m_currentDirectory.isEmpty()) {
        setStatus(QStringLiteral("Save the manifest before exporting."));
        return;
    }
    const plugins::ValidationReport report =
        plugins::validateManifest(manifest, m_currentDirectory, m_installed);
    showValidation(report);
    if (!report.valid()) {
        setStatus(QStringLiteral("Fix the validation errors before exporting."));
        return;
    }
    const QString destination = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose the export folder"));
    if (destination.isEmpty())
        return;
    const auto exported = plugins::exportBundle(manifest, m_currentDirectory, destination);
    if (!exported) {
        const QString detail = exported.error().detail;
        setStatus(detail.isEmpty() ? exported.error().message : detail);
        return;
    }
    setStatus(QStringLiteral("Exported %1 file(s), %2 bytes to %3. Checksums: %4")
                  .arg(exported.value().files.size())
                  .arg(exported.value().totalBytes)
                  .arg(exported.value().directory, exported.value().checksumFile));
    QMessageBox::information(
        this, QStringLiteral("Bundle exported"),
        QStringLiteral("%1 file(s) copied to:\n%2\n\nChecksums: %3\n\nNo plugin code was "
                       "executed.")
            .arg(exported.value().files.size())
            .arg(exported.value().directory, exported.value().checksumFile));
}

void PluginStudioPanel::createExample()
{
    const QString root = pluginsDirectory();
    const QString directory = QDir(root).filePath(QStringLiteral("com.example.hello-toolkit"));
    if (!QDir().mkpath(directory)) {
        setStatus(QStringLiteral("Cannot create %1").arg(directory));
        return;
    }
    const QMap<QString, QString> files = plugins::examplePluginFiles();
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QString path = QDir(directory).filePath(it.key());
        if (QFileInfo::exists(path))
            continue;
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(it.value().toUtf8());
    }
    loadInstalled();
    for (int i = 0; i < m_pluginList->count(); ++i) {
        if (m_pluginList->item(i)->data(Qt::UserRole).toString() == directory) {
            m_pluginList->setCurrentRow(i);
            break;
        }
    }
    setStatus(QStringLiteral("Example plugin created at %1").arg(directory));
}

void PluginStudioPanel::setStatus(const QString& message)
{
    m_status->setText(message);
}

}  // namespace pkg::ui
