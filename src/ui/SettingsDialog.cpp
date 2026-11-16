#include "ui/SettingsDialog.h"

#include "data/ThemeManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace pkg::ui {

SettingsDialog::SettingsDialog(const AppSettings& values, QList<ActionInfo> actions,
                               QWidget* parent)
    : QDialog(parent), m_values(values), m_actions(std::move(actions))
{
    setWindowTitle(QStringLiteral("Settings"));
    setModal(true);
    resize(720, 540);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);

    auto* tabs = new QTabWidget(this);
    layout->addWidget(tabs, 1);

    auto* generalTab = new QWidget(tabs);
    buildGeneralTab(generalTab);
    tabs->addTab(generalTab, QStringLiteral("General"));

    auto* hexTab = new QWidget(tabs);
    buildHexTab(hexTab);
    tabs->addTab(hexTab, QStringLiteral("Hex editor"));

    auto* performanceTab = new QWidget(tabs);
    buildPerformanceTab(performanceTab);
    tabs->addTab(performanceTab, QStringLiteral("Performance"));

    auto* loggingTab = new QWidget(tabs);
    buildLoggingTab(loggingTab);
    tabs->addTab(loggingTab, QStringLiteral("Logging"));

    auto* shortcutsTab = new QWidget(tabs);
    buildShortcutsTab(shortcutsTab);
    tabs->addTab(shortcutsTab, QStringLiteral("Shortcuts"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::buildGeneralTab(QWidget* tab)
{
    auto* form = new QFormLayout(tab);
    form->setContentsMargins(14, 14, 14, 14);
    form->setSpacing(10);

    m_theme = new QComboBox(tab);
    for (const Theme& theme : ThemeManager::instance().themes())
        m_theme->addItem(theme.name, theme.id);
    const int themeIndex = m_theme->findData(m_values.themeId);
    if (themeIndex >= 0)
        m_theme->setCurrentIndex(themeIndex);
    form->addRow(QStringLiteral("Theme"), m_theme);

    m_fontFamily = new QComboBox(tab);
    m_fontFamily->addItem(QStringLiteral("System fixed font"), QString());
    for (const QString& family : QFontDatabase::families())
        m_fontFamily->addItem(family, family);
    const int familyIndex = m_fontFamily->findData(m_values.fontFamily);
    m_fontFamily->setCurrentIndex(familyIndex >= 0 ? familyIndex : 0);
    form->addRow(QStringLiteral("Editor font"), m_fontFamily);

    m_fontSize = new QSpinBox(tab);
    m_fontSize->setRange(7, 24);
    m_fontSize->setValue(m_values.fontSize);
    m_fontSize->setSuffix(QStringLiteral(" pt"));
    form->addRow(QStringLiteral("Font size"), m_fontSize);

    m_lightweight = new QCheckBox(QStringLiteral("Lightweight mode"), tab);
    m_lightweight->setChecked(m_values.lightweight);
    m_lightweight->setToolTip(QStringLiteral(
        "Disables animations and optional visual effects. Recommended on low-end hardware."));
    form->addRow(QString(), m_lightweight);

    m_restoreSession = new QCheckBox(QStringLiteral("Restore open files on startup"), tab);
    m_restoreSession->setChecked(m_values.restoreSession);
    form->addRow(QString(), m_restoreSession);

    m_confirmWrites = new QCheckBox(
        QStringLiteral("Ask for confirmation before writing to any package"), tab);
    m_confirmWrites->setChecked(m_values.confirmWrites);
    m_confirmWrites->setToolTip(
        QStringLiteral("Saving an existing file always writes a timestamped backup first. "
                       "This option controls the additional overwrite confirmation."));
    form->addRow(QString(), m_confirmWrites);

    m_recentLimit = new QSpinBox(tab);
    m_recentLimit->setRange(1, 100);
    m_recentLimit->setValue(m_values.recentLimit);
    form->addRow(QStringLiteral("Recent entries"), m_recentLimit);

    auto* note = new QLabel(
        QStringLiteral("Settings are stored in the local SQLite database. No settings or "
                       "usage data leave this computer."),
        tab);
    note->setObjectName(QStringLiteral("hintLabel"));
    note->setWordWrap(true);
    form->addRow(note);
}

void SettingsDialog::buildHexTab(QWidget* tab)
{
    auto* form = new QFormLayout(tab);
    form->setContentsMargins(14, 14, 14, 14);
    form->setSpacing(10);

    m_bytesPerRow = new QComboBox(tab);
    for (const int value : {8, 16, 24, 32, 48, 64})
        m_bytesPerRow->addItem(QString::number(value), value);
    const int rowIndex = m_bytesPerRow->findData(m_values.hexBytesPerRow);
    if (rowIndex >= 0)
        m_bytesPerRow->setCurrentIndex(rowIndex);
    form->addRow(QStringLiteral("Bytes per row"), m_bytesPerRow);

    m_groupSize = new QSpinBox(tab);
    m_groupSize->setRange(1, 8);
    m_groupSize->setValue(m_values.hexGroupSize);
    form->addRow(QStringLiteral("Group size"), m_groupSize);

    auto* note = new QLabel(
        QStringLiteral("The hex view is read-only in this release. Byte editing with automatic "
                       "backups and undo is planned for the modding workspace."),
        tab);
    note->setObjectName(QStringLiteral("hintLabel"));
    note->setWordWrap(true);
    form->addRow(note);
}

void SettingsDialog::buildPerformanceTab(QWidget* tab)
{
    auto* form = new QFormLayout(tab);
    form->setContentsMargins(14, 14, 14, 14);
    form->setSpacing(10);

    m_workerThreads = new QSpinBox(tab);
    m_workerThreads->setRange(0, 64);
    m_workerThreads->setSpecialValueText(QStringLiteral("Automatic"));
    m_workerThreads->setValue(m_values.workerThreads);
    m_workerThreads->setToolTip(
        QStringLiteral("0 selects one worker per available core, capped for low-end systems."));
    form->addRow(QStringLiteral("Worker threads"), m_workerThreads);

    m_cacheLimit = new QSpinBox(tab);
    m_cacheLimit->setRange(8, 4096);
    m_cacheLimit->setSuffix(QStringLiteral(" MB"));
    m_cacheLimit->setValue(m_values.cacheLimitMB);
    form->addRow(QStringLiteral("Analysis cache budget"), m_cacheLimit);

    auto* note = new QLabel(
        QStringLiteral("Files are memory mapped and streamed; opening a large package does not "
                       "copy it into RAM. Worker count and cache budget are applied on restart."),
        tab);
    note->setObjectName(QStringLiteral("hintLabel"));
    note->setWordWrap(true);
    form->addRow(note);
}

void SettingsDialog::buildLoggingTab(QWidget* tab)
{
    auto* form = new QFormLayout(tab);
    form->setContentsMargins(14, 14, 14, 14);
    form->setSpacing(10);

    m_logLevel = new QComboBox(tab);
    m_logLevel->addItem(QStringLiteral("Trace"), static_cast<int>(LogLevel::Trace));
    m_logLevel->addItem(QStringLiteral("Debug"), static_cast<int>(LogLevel::Debug));
    m_logLevel->addItem(QStringLiteral("Info"), static_cast<int>(LogLevel::Info));
    m_logLevel->addItem(QStringLiteral("Warning"), static_cast<int>(LogLevel::Warning));
    m_logLevel->addItem(QStringLiteral("Error"), static_cast<int>(LogLevel::Error));
    const int levelIndex = m_logLevel->findData(static_cast<int>(m_values.logLevel));
    m_logLevel->setCurrentIndex(levelIndex >= 0 ? levelIndex : 2);
    form->addRow(QStringLiteral("Log level"), m_logLevel);

    m_fileLogging = new QCheckBox(QStringLiteral("Write logs to disk"), tab);
    m_fileLogging->setChecked(m_values.fileLogging);
    form->addRow(QString(), m_fileLogging);

    auto* openFolder = new QPushButton(QStringLiteral("Open log folder"), tab);
    connect(openFolder, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(Settings::logsDirectory()));
    });
    form->addRow(QString(), openFolder);

    auto* note = new QLabel(
        QStringLiteral("Crash reports contain version, signal and raw addresses only; file "
                       "contents and paths are never included."),
        tab);
    note->setObjectName(QStringLiteral("hintLabel"));
    note->setWordWrap(true);
    form->addRow(note);
}

void SettingsDialog::buildShortcutsTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* tree = new QTreeWidget(tab);
    tree->setColumnCount(2);
    tree->setHeaderLabels({QStringLiteral("Command"), QStringLiteral("Shortcut")});
    tree->setUniformRowHeights(false);
    tree->setRootIsDecorated(true);
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(tree, 1);

    QHash<QString, QTreeWidgetItem*> categories;
    for (const ActionInfo& action : m_actions) {
        QTreeWidgetItem* parent = categories.value(action.category);
        if (parent == nullptr) {
            parent = new QTreeWidgetItem(tree);
            parent->setText(0, action.category);
            parent->setFirstColumnSpanned(false);
            parent->setExpanded(true);
            categories.insert(action.category, parent);
        }
        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, action.title);

        auto* editor = new QKeySequenceEdit(tree);
        editor->setKeySequence(
            Settings::instance().shortcut(action.id, action.defaultShortcut));
        editor->setClearButtonEnabled(true);
        tree->setItemWidget(item, 1, editor);
        m_shortcutEdits.insert(action.id, editor);
    }

    auto* row = new QHBoxLayout();
    auto* resetButton = new QPushButton(QStringLiteral("Reset to defaults"), tab);
    auto* note = new QLabel(
        QStringLiteral("Shortcut changes apply immediately after saving."), tab);
    note->setObjectName(QStringLiteral("hintLabel"));
    row->addWidget(resetButton);
    row->addWidget(note, 1);
    layout->addLayout(row);

    connect(resetButton, &QPushButton::clicked, this, &SettingsDialog::resetShortcutsToDefaults);
}

void SettingsDialog::resetShortcutsToDefaults()
{
    m_shortcutsReset = true;
    for (const ActionInfo& action : m_actions) {
        QKeySequenceEdit* editor = m_shortcutEdits.value(action.id);
        if (editor != nullptr)
            editor->setKeySequence(action.defaultShortcut);
    }
}

AppSettings SettingsDialog::values() const
{
    AppSettings result = m_values;
    if (m_theme != nullptr)
        result.themeId = m_theme->currentData().toString();
    if (m_fontFamily != nullptr)
        result.fontFamily = m_fontFamily->currentData().toString();
    if (m_fontSize != nullptr)
        result.fontSize = m_fontSize->value();
    if (m_lightweight != nullptr)
        result.lightweight = m_lightweight->isChecked();
    if (m_restoreSession != nullptr)
        result.restoreSession = m_restoreSession->isChecked();
    if (m_confirmWrites != nullptr)
        result.confirmWrites = m_confirmWrites->isChecked();
    if (m_recentLimit != nullptr)
        result.recentLimit = m_recentLimit->value();
    if (m_bytesPerRow != nullptr)
        result.hexBytesPerRow = m_bytesPerRow->currentData().toInt();
    if (m_groupSize != nullptr)
        result.hexGroupSize = m_groupSize->value();
    if (m_workerThreads != nullptr)
        result.workerThreads = m_workerThreads->value();
    if (m_cacheLimit != nullptr)
        result.cacheLimitMB = m_cacheLimit->value();
    if (m_logLevel != nullptr)
        result.logLevel = static_cast<LogLevel>(m_logLevel->currentData().toInt());
    if (m_fileLogging != nullptr)
        result.fileLogging = m_fileLogging->isChecked();
    return result;
}

QHash<QString, QKeySequence> SettingsDialog::shortcutOverrides() const
{
    QHash<QString, QKeySequence> overrides;
    for (const ActionInfo& action : m_actions) {
        const QKeySequenceEdit* editor = m_shortcutEdits.value(action.id);
        if (editor == nullptr)
            continue;
        if (editor->keySequence() != action.defaultShortcut)
            overrides.insert(action.id, editor->keySequence());
    }
    return overrides;
}

}  // namespace pkg::ui
