#include "ui/SettingsDialog.h"

#include "data/ThemeManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace pkg::ui {
namespace {

QString styleChip(const QColor& colour)
{
    return QStringLiteral("background-color: %1; border: 1px solid rgba(127,127,127,0.45); "
                          "border-radius: 3px;")
        .arg(colour.name(QColor::HexRgb));
}

}  // namespace

SettingsDialog::SettingsDialog(const AppSettings& values, QList<ActionInfo> actions,
                               QWidget* parent)
    : QDialog(parent), m_values(values), m_actions(std::move(actions))
{
    setWindowTitle(QStringLiteral("Settings"));
    setModal(true);
    resize(920, 640);
    m_originalThemeId = ThemeManager::instance().current().id;
    buildCenter();
}

SettingsDialog::~SettingsDialog() = default;

QWidget* SettingsDialog::createPage(const QString& title)
{
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    layout->setAlignment(Qt::AlignTop);
    content->setObjectName(QStringLiteral("settingsPage"));
    scroll->setWidget(content);

    Page page;
    page.title = title;
    page.widget = scroll;
    m_pages.append(page);

    auto* item = new QListWidgetItem(title, m_categories);
    item->setData(Qt::UserRole, static_cast<int>(m_pages.size() - 1));
    m_stack->addWidget(scroll);
    return content;
}

void SettingsDialog::addRow(QWidget* page, QWidget* row, const QString& searchText)
{
    for (Page& entry : m_pages) {
        auto* scroll = qobject_cast<QScrollArea*>(entry.widget);
        if (scroll != nullptr && scroll->widget() == page) {
            entry.rows.append({row, searchText.toLower()});
            return;
        }
    }
}

QWidget* SettingsDialog::makeSettingRow(const QString& title, const QString& description,
                                        QWidget* control, QWidget* parent,
                                        const std::function<void()>& reset)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(10);

    auto* text = new QVBoxLayout();
    text->setSpacing(1);
    auto* titleLabel = new QLabel(title, row);
    titleLabel->setObjectName(QStringLiteral("sectionLabel"));
    text->addWidget(titleLabel);
    if (!description.isEmpty()) {
        auto* hint = new QLabel(description, row);
        hint->setObjectName(QStringLiteral("hintLabel"));
        hint->setWordWrap(true);
        text->addWidget(hint);
    }
    layout->addLayout(text, 1);
    if (control != nullptr) {
        control->setParent(row);
        layout->addWidget(control);
    }
    if (reset) {
        auto* resetButton = new QToolButton(row);
        resetButton->setObjectName(QStringLiteral("hintLabel"));
        resetButton->setAutoRaise(true);
        resetButton->setText(QStringLiteral("Reset"));
        resetButton->setToolTip(QStringLiteral("Reset this setting to its default"));
        layout->addWidget(resetButton);
        connect(resetButton, &QToolButton::clicked, this, [reset]() { reset(); });
    }
    return row;
}

QWidget* SettingsDialog::makeInfoCard(const QString& title, const QString& body, QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("settingsInfoCard"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(3);
    auto* titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("sectionLabel"));
    layout->addWidget(titleLabel);
    auto* bodyLabel = new QLabel(body, card);
    bodyLabel->setObjectName(QStringLiteral("hintLabel"));
    bodyLabel->setWordWrap(true);
    bodyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(bodyLabel);
    return card;
}

void SettingsDialog::buildCenter()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* header = new QHBoxLayout();
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(
        QStringLiteral("Search settings, sections and explanations\\u2026"));
    m_search->setClearButtonEnabled(true);
    header->addWidget(m_search, 1);
    auto* hint = new QLabel(QStringLiteral("Changes are previewed live, then saved."), this);
    hint->setObjectName(QStringLiteral("hintLabel"));
    header->addWidget(hint);
    layout->addLayout(header);

    auto* body = new QHBoxLayout();
    body->setSpacing(10);

    m_categories = new QListWidget(this);
    m_categories->setObjectName(QStringLiteral("settingsCategories"));
    m_categories->setFixedWidth(180);
    m_categories->setFrameShape(QFrame::NoFrame);
    body->addWidget(m_categories);

    m_stack = new QStackedWidget(this);
    body->addWidget(m_stack, 1);
    layout->addLayout(body, 1);

    // Appearance ---------------------------------------------------------
    auto* appearance = createPage(QStringLiteral("Appearance"));
    buildAppearancePage(appearance);

    // Editor -------------------------------------------------------------
    auto* editor = createPage(QStringLiteral("Editor"));
    buildEditorPage(editor);

    // Hex editor ---------------------------------------------------------
    auto* hex = createPage(QStringLiteral("Hex Editor"));
    buildHexPage(hex);

    // GameForge ----------------------------------------------------------
    auto* gameforge = createPage(QStringLiteral("GameForge"));
    buildGameForgePage(gameforge);

    // AI providers -------------------------------------------------------
    auto* ai = createPage(QStringLiteral("AI Providers"));
    buildAiPage(ai);

    // Performance --------------------------------------------------------
    auto* performance = createPage(QStringLiteral("Performance"));
    buildPerformancePage(performance);

    // Shortcuts ----------------------------------------------------------
    auto* shortcuts = createPage(QStringLiteral("Keyboard Shortcuts"));
    buildShortcutsPage(shortcuts);

    // Plugins ------------------------------------------------------------
    auto* plugins = createPage(QStringLiteral("Plugins"));
    buildPluginsPage(plugins);

    // Privacy ------------------------------------------------------------
    auto* privacy = createPage(QStringLiteral("Privacy and Security"));
    buildPrivacyPage(privacy);

    // Updates ------------------------------------------------------------
    auto* updates = createPage(QStringLiteral("Updates"));
    buildUpdatesPage(updates);

    auto* footer = new QHBoxLayout();
    auto* restore = new QPushButton(QStringLiteral("Restore defaults\\u2026"), this);
    auto* resetCategory = new QPushButton(QStringLiteral("Reset this category"), this);
    footer->addWidget(restore);
    footer->addWidget(resetCategory);
    footer->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    footer->addWidget(buttons);
    layout->addLayout(footer);

    connect(m_search, &QLineEdit::textChanged, this, &SettingsDialog::refilter);
    connect(m_categories, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0)
            return;
        const int page = m_categories->item(row)->data(Qt::UserRole).toInt();
        m_currentPage = page;
        m_stack->setCurrentIndex(page);
    });
    connect(restore, &QPushButton::clicked, this, &SettingsDialog::resetAll);
    connect(resetCategory, &QPushButton::clicked, this, &SettingsDialog::resetCategory);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (m_categories->count() > 0)
        m_categories->setCurrentRow(0);
}

void SettingsDialog::buildAppearancePage(QWidget* page)
{
    auto* layout = qobject_cast<QVBoxLayout*>(page->layout());
    m_theme = new QComboBox(page);
    for (const Theme& theme : ThemeManager::instance().themes())
        m_theme->addItem(theme.name, theme.id);
    const int index = m_theme->findData(m_values.themeId);
    m_theme->setCurrentIndex(index >= 0 ? index : 0);

    m_themeDescription = new QLabel(page);
    m_themeDescription->setObjectName(QStringLiteral("hintLabel"));
    m_themeDescription->setWordWrap(true);

    m_themeSwatches = new QWidget(page);
    auto* swatchLayout = new QHBoxLayout(m_themeSwatches);
    swatchLayout->setContentsMargins(0, 0, 0, 0);
    swatchLayout->setSpacing(4);

    auto* themeRow = makeSettingRow(
        QStringLiteral("Theme"),
        QStringLiteral("Applies to every window, dialog, table, graph and the hex editor."),
        m_theme, page, [this]() {
            const int defaultIndex = m_theme->findData(QStringLiteral("pkgstudio.voidblack"));
            m_theme->setCurrentIndex(std::max(0, defaultIndex));
        });
    addRow(page, themeRow, QStringLiteral("theme colour palette appearance dark light"));
    layout->addWidget(themeRow);
    addRow(page, m_themeDescription, QStringLiteral("theme description"));
    layout->addWidget(m_themeDescription);
    addRow(page, m_themeSwatches, QStringLiteral("theme preview colours"));
    layout->addWidget(m_themeSwatches);

    m_lightweight = new QCheckBox(QStringLiteral("Enabled"), page);
    m_lightweight->setChecked(m_values.lightweight);
    auto* lightweightRow = makeSettingRow(
        QStringLiteral("Lightweight mode"),
        QStringLiteral("Disables optional visual effects. Recommended on low-end hardware."),
        m_lightweight, page, [this]() { m_lightweight->setChecked(false); });
    addRow(page, lightweightRow, QStringLiteral("lightweight performance animation effects"));
    layout->addWidget(lightweightRow);

    const auto updateDescription = [this]() {
        const QString id = m_theme->currentData().toString();
        ThemeManager& manager = ThemeManager::instance();
        Theme theme = manager.current();
        if (theme.id != id) {
            const QList<Theme> themes = manager.themes();
            for (const Theme& candidate : themes) {
                if (candidate.id == id) {
                    theme = candidate;
                    break;
                }
            }
        }
        if (m_themeDescription != nullptr) {
            m_themeDescription->setText(
                QStringLiteral("%1 \\u00B7 %2")
                    .arg(theme.description.isEmpty() ? QStringLiteral("No description")
                                                    : theme.description,
                         theme.dark ? QStringLiteral("dark theme")
                                    : QStringLiteral("light theme")));
        }
        if (m_themeSwatches == nullptr)
            return;
        auto* layout = qobject_cast<QHBoxLayout*>(m_themeSwatches->layout());
        while (QLayoutItem* item = layout->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        const QStringList roles = {QStringLiteral("window"), QStringLiteral("panel"),
                                   QStringLiteral("base"), QStringLiteral("accent"),
                                   QStringLiteral("link"), QStringLiteral("hexAscii"),
                                   QStringLiteral("statusBar"), QStringLiteral("hexSearch")};
        for (const QString& role : roles) {
            auto* chip = new QLabel(m_themeSwatches);
            chip->setFixedSize(34, 20);
            chip->setToolTip(role);
            chip->setStyleSheet(styleChip(theme.color(role)));
            layout->addWidget(chip);
        }
        layout->addStretch(1);
    };

    connect(m_theme, &QComboBox::currentIndexChanged, this, [this, updateDescription](int) {
        applyThemePreview();
        updateDescription();
    });
    updateDescription();
}

void SettingsDialog::buildEditorPage(QWidget* page)
{
    auto* layout = qobject_cast<QVBoxLayout*>(page->layout());
    m_fontFamily = new QComboBox(page);
    m_fontFamily->addItem(QStringLiteral("System fixed font"), QString());
    for (const QString& family : QFontDatabase::families())
        m_fontFamily->addItem(family, family);
    const int familyIndex = m_fontFamily->findData(m_values.fontFamily);
    m_fontFamily->setCurrentIndex(familyIndex >= 0 ? familyIndex : 0);
    auto* familyRow = makeSettingRow(QStringLiteral("Editor font"),
                                     QStringLiteral("Used by the hex view, listings and code surfaces."),
                                     m_fontFamily, page, [this]() { m_fontFamily->setCurrentIndex(0); });
    addRow(page, familyRow, QStringLiteral("font family typeface editor hex"));
    layout->addWidget(familyRow);

    m_fontSize = new QSpinBox(page);
    m_fontSize->setRange(7, 24);
    m_fontSize->setValue(m_values.fontSize);
    m_fontSize->setSuffix(QStringLiteral(" pt"));
    auto* sizeRow = makeSettingRow(QStringLiteral("Font size"),
                                   QStringLiteral("Point size for monospaced surfaces."), m_fontSize,
                                   page, [this]() { m_fontSize->setValue(10); });
    addRow(page, sizeRow, QStringLiteral("font size point text zoom"));
    layout->addWidget(sizeRow);

    m_restoreSession = new QCheckBox(QStringLiteral("Enabled"), page);
    m_restoreSession->setChecked(m_values.restoreSession);
    auto* restoreRow = makeSettingRow(
        QStringLiteral("Restore open files on startup"),
        QStringLiteral("Reopens the tabs from the previous session. Files are never modified."),
        m_restoreSession, page, [this]() { m_restoreSession->setChecked(true); });
    addRow(page, restoreRow, QStringLiteral("restore session tabs startup workspace"));
    layout->addWidget(restoreRow);

    m_recentLimit = new QSpinBox(page);
    m_recentLimit->setRange(1, 100);
    m_recentLimit->setValue(m_values.recentLimit);
    auto* recentRow = makeSettingRow(QStringLiteral("Recent entries"),
                                     QStringLiteral("How many files and projects the recent list keeps."),
                                     m_recentLimit, page, [this]() { m_recentLimit->setValue(15); });
    addRow(page, recentRow, QStringLiteral("recent files projects history list"));
    layout->addWidget(recentRow);
}

void SettingsDialog::buildHexPage(QWidget* page)
{
    auto* layout = qobject_cast<QVBoxLayout*>(page->layout());
    m_bytesPerRow = new QComboBox(page);
    for (const int value : {8, 16, 24, 32, 48, 64})
        m_bytesPerRow->addItem(QString::number(value), value);
    const int rowIndex = m_bytesPerRow->findData(m_values.hexBytesPerRow);
    if (rowIndex >= 0)
        m_bytesPerRow->setCurrentIndex(rowIndex);
    auto* rowRow = makeSettingRow(QStringLiteral("Bytes per row"),
                                  QStringLiteral("Column count of the hex grid."), m_bytesPerRow,
                                  page, [this]() { m_bytesPerRow->setCurrentIndex(1); });
    addRow(page, rowRow, QStringLiteral("hex bytes per row columns grid"));
    layout->addWidget(rowRow);

    m_groupSize = new QSpinBox(page);
    m_groupSize->setRange(1, 8);
    m_groupSize->setValue(m_values.hexGroupSize);
    auto* groupRow = makeSettingRow(QStringLiteral("Group size"),
                                    QStringLiteral("Visual grouping of hex byte pairs."), m_groupSize,
                                    page, [this]() { m_groupSize->setValue(1); });
    addRow(page, groupRow, QStringLiteral("hex grouping bytes cells"));
    layout->addWidget(groupRow);

    auto* note = makeInfoCard(
        QStringLiteral("Editing stays local and reversible"),
        QStringLiteral("Edits are held in memory with per-byte undo until you save. Saving writes "
                       "a timestamped backup next to the original and asks before overwriting. "
                       "Bookmarks and the comparison view never modify the files."),
        page);
    addRow(page, note, QStringLiteral("editing backup undo safety bookmark compare"));
    layout->addWidget(note);
}

void SettingsDialog::buildGameForgePage(QWidget* page)
{
    auto* layout = qobject_cast<QVBoxLayout*>(page->layout());
    auto* card = makeInfoCard(
        QStringLiteral("Analysis is evidence based"),
        QStringLiteral("GameForge reads CMake, Meson, Makefile, Godot and Unreal project files. "
                       "Variables and conditions are reported with file and line, never "
                       "evaluated. Projects are scanned on a background worker with file and "
                       "node limits; cancellation keeps the partial result."),
        page);
    addRow(page, card, QStringLiteral("gameforge analysis dependencies build config porting"));
    layout->addWidget(card);

    auto* limits = makeInfoCard(
        QStringLiteral("Resource limits"),
        QStringLiteral("Build files: up to 2000 files, 4 MiB per build file. Graph: up to 2000 "
                       "edges. A report that reaches a limit says so instead of pretending to be "
                       "complete."),
        page);
    addRow(page, limits, QStringLiteral("limits performance files graph"));
    layout->addWidget(limits);
}

void SettingsDialog::buildAiPage(QWidget* page)
{
    auto* layout = qobject_cast<QVBoxLayout*>(page->layout());
    auto* card = makeInfoCard(
        QStringLiteral("Providers are configured in the AI panel"),
        QStringLiteral("The AI assistant owns its provider list and API keys. Keys are stored "
                       "locally, are never written to logs and are never sent with project "
                       "context. Ask before sharing project content with a cloud provider."),
        page);
    addRow(page, card, QStringLiteral("ai providers api key openai local model privacy"));
    layout->addWidget(card);
}

void SettingsDialog::buildPerformancePage(QWidget* page)
{
    auto* layout = qobject_cast<QVBoxLayout*>(page->layout());
    m_workerThreads = new QSpinBox(page);
    m_workerThreads->setRange(0, 64);
    m_workerThreads->setSpecialValueText(QStringLiteral("Automatic"));
    m_workerThreads->setValue(m_values.workerThreads);
    auto* threadsRow = makeSettingRow(
        QStringLiteral("Worker threads"),
        QStringLiteral("0 uses one worker per core, capped. Applied on restart."), m_workerThreads,
        page, [this]() { m_workerThreads->setValue(0); });
    addRow(page, threadsRow, QStringLiteral("worker threads cpu performance background"));
    layout->addWidget(threadsRow);

    m_cacheLimit = new QSpinBox(page);
    m_cacheLimit->setRange(8, 4096);
    m_cacheLimit->setSuffix(QStringLiteral(" MB"));
    m_cacheLimit->setValue(m_values.cacheLimitMB);
    auto* cacheRow = makeSettingRow(
        QStringLiteral("Analysis cache budget"),
        QStringLiteral("Parser results cached in the local database. Files are memory mapped, "
                       "never copied into RAM."),
        m_cacheLimit, page, [this]() { m_cacheLimit->setValue(64); });
    addRow(page, cacheRow, QStringLiteral("cache memory budget performance"));
    layout->addWidget(cacheRow);
}

void SettingsDialog::buildShortcutsPage(QWidget* page)
{
    auto* layout = qobject_cast<QVBoxLayout*>(page->layout());
    m_shortcutTree = new QTreeWidget(page);
    m_shortcutTree->setColumnCount(2);
    m_shortcutTree->setHeaderLabels({QStringLiteral("Command"), QStringLiteral("Shortcut")});
    m_shortcutTree->setUniformRowHeights(true);
    m_shortcutTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_shortcutTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(m_shortcutTree, 1);

    QHash<QString, QTreeWidgetItem*> categories;
    for (const ActionInfo& action : m_actions) {
        QTreeWidgetItem* parent = categories.value(action.category);
        if (parent == nullptr) {
            parent = new QTreeWidgetItem(m_shortcutTree);
            parent->setText(0, action.category);
            parent->setExpanded(true);
            categories.insert(action.category, parent);
        }
        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, action.title);
        auto* editor = new QKeySequenceEdit(m_shortcutTree);
        editor->setKeySequence(Settings::instance().shortcut(action.id, action.defaultShortcut));
        editor->setClearButtonEnabled(true);
        m_shortcutTree->setItemWidget(item, 1, editor);
        m_shortcutEdits.insert(action.id, editor);
        m_shortcutItems.append(item);
    }

    auto* reset = new QPushButton(QStringLiteral("Reset all shortcuts"), page);
    connect(reset, &QPushButton::clicked, this, &SettingsDialog::resetShortcutsToDefaults);
    addRow(page, reset, QStringLiteral("shortcuts reset defaults keys"));
    layout->addWidget(reset);
    auto* note = makeInfoCard(
        QStringLiteral("Shortcut scope"),
        QStringLiteral("Overrides apply after saving. Search above filters commands by name."),
        page);
    addRow(page, note, QStringLiteral("shortcut search keys"));
    layout->addWidget(note);
}

void SettingsDialog::buildPluginsPage(QWidget* page)
{
    auto* layout = qobject_cast<QVBoxLayout*>(page->layout());
    auto* empty = makeInfoCard(
        QStringLiteral("No plugins installed"),
        QStringLiteral("PKG Studio does not load third-party plugins in this release. Parser and "
                       "theme formats are documented, and user themes can already be added as "
                       "JSON files in the theme folder. A plugin API is not offered yet; nothing "
                       "on this page pretends otherwise."),
        page);
    addRow(page, empty, QStringLiteral("plugins extensions add-ons marketplace"));
    layout->addWidget(empty);
}

void SettingsDialog::buildPrivacyPage(QWidget* page)
{
    auto* layout = qobject_cast<QVBoxLayout*>(page->layout());
    m_confirmWrites = new QCheckBox(QStringLiteral("Enabled"), page);
    m_confirmWrites->setChecked(m_values.confirmWrites);
    auto* confirmRow = makeSettingRow(
        QStringLiteral("Confirm before writing to a package"),
        QStringLiteral("A timestamped backup is always written first; this adds an explicit "
                       "confirmation."),
        m_confirmWrites, page, [this]() { m_confirmWrites->setChecked(true); });
    addRow(page, confirmRow, QStringLiteral("confirm overwrite backup writing safety"));
    layout->addWidget(confirmRow);

    m_logLevel = new QComboBox(page);
    m_logLevel->addItem(QStringLiteral("Trace"), static_cast<int>(LogLevel::Trace));
    m_logLevel->addItem(QStringLiteral("Debug"), static_cast<int>(LogLevel::Debug));
    m_logLevel->addItem(QStringLiteral("Info"), static_cast<int>(LogLevel::Info));
    m_logLevel->addItem(QStringLiteral("Warning"), static_cast<int>(LogLevel::Warning));
    m_logLevel->addItem(QStringLiteral("Error"), static_cast<int>(LogLevel::Error));
    const int levelIndex = m_logLevel->findData(static_cast<int>(m_values.logLevel));
    m_logLevel->setCurrentIndex(levelIndex >= 0 ? levelIndex : 2);
    auto* levelRow = makeSettingRow(
        QStringLiteral("Log level"),
        QStringLiteral("Console verbosity. Credential-shaped values are masked in the output."),
        m_logLevel, page, [this]() { m_logLevel->setCurrentIndex(2); });
    addRow(page, levelRow, QStringLiteral("log level diagnostics privacy"));
    layout->addWidget(levelRow);

    m_fileLogging = new QCheckBox(QStringLiteral("Enabled"), page);
    m_fileLogging->setChecked(m_values.fileLogging);
    auto* fileRow = makeSettingRow(QStringLiteral("Write logs to disk"),
                                   QStringLiteral("Rotating local log file. Nothing is uploaded."),
                                   m_fileLogging, page, [this]() { m_fileLogging->setChecked(true); });
    addRow(page, fileRow, QStringLiteral("log file disk privacy local"));
    layout->addWidget(fileRow);

    auto* paths = makeInfoCard(
        QStringLiteral("Local data"),
        QStringLiteral("Database: %1\\nLogs: %2\\nThemes: %3")
            .arg(Settings::databasePath(), Settings::logsDirectory(),
                 Settings::themesDirectory()),
        page);
    addRow(page, paths, QStringLiteral("data folder database logs themes privacy local"));
    layout->addWidget(paths);
}

void SettingsDialog::buildUpdatesPage(QWidget* page)
{
    auto* layout = qobject_cast<QVBoxLayout*>(page->layout());
    const QString version = QCoreApplication::applicationVersion().isEmpty()
        ? QStringLiteral("development build")
        : QCoreApplication::applicationVersion();
    auto* card = makeInfoCard(
        QStringLiteral("PKG Studio %1").arg(version),
        QStringLiteral("Updates are published on the project's GitHub releases page. PKG Studio "
                       "never checks for updates in the background and never downloads anything "
                       "on its own."),
        page);
    addRow(page, card, QStringLiteral("updates version releases github"));
    layout->addWidget(card);

    auto* open = new QPushButton(QStringLiteral("Open releases page"), page);
    connect(open, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://github.com/SadXresto/PKG-REVE/releases")));
    });
    addRow(page, open, QStringLiteral("updates releases website"));
    layout->addWidget(open);
}

void SettingsDialog::refilter(const QString& text)
{
    const QString needle = text.trimmed().toLower();
    for (int pageIndex = 0; pageIndex < static_cast<int>(m_pages.size()); ++pageIndex) {
        Page& page = m_pages[pageIndex];
        bool anyVisible = false;
        for (const Row& row : page.rows) {
            const bool matches = needle.isEmpty() || row.searchText.contains(needle);
            row.widget->setVisible(matches);
            anyVisible = anyVisible || matches;
        }
        // Shortcut items are filtered through the tree, not through rows.
        if (page.title == QLatin1String("Keyboard Shortcuts") && m_shortcutTree != nullptr) {
            for (QTreeWidgetItem* item : m_shortcutItems) {
                const bool matches = needle.isEmpty() ||
                    item->text(0).toLower().contains(needle) ||
                    (item->parent() != nullptr &&
                     item->parent()->text(0).toLower().contains(needle));
                item->setHidden(!matches);
                anyVisible = anyVisible || matches;
            }
        }
        m_categories->item(pageIndex)->setHidden(!anyVisible);
    }

    for (int row = 0; row < m_categories->count(); ++row) {
        if (!m_categories->item(row)->isHidden()) {
            m_categories->setCurrentRow(row);
            break;
        }
    }
}

void SettingsDialog::applyThemePreview()
{
    const QString id = m_theme->currentData().toString();
    ThemeManager& manager = ThemeManager::instance();
    if (!manager.setCurrent(id))
        return;
    ThemeManager::apply(manager.current(), *qApp);
}

void SettingsDialog::restoreThemePreview()
{
    ThemeManager& manager = ThemeManager::instance();
    if (manager.setCurrent(m_originalThemeId))
        ThemeManager::apply(manager.current(), *qApp);
}

void SettingsDialog::resetCategory()
{
    if (m_currentPage < 0 || m_currentPage >= static_cast<int>(m_pages.size()))
        return;
    const QString title = m_pages.at(m_currentPage).title;
    const AppSettings defaults;
    if (title == QLatin1String("Appearance")) {
        m_theme->setCurrentIndex(std::max(0, m_theme->findData(defaults.themeId)));
        m_lightweight->setChecked(defaults.lightweight);
    } else if (title == QLatin1String("Editor")) {
        m_fontFamily->setCurrentIndex(0);
        m_fontSize->setValue(defaults.fontSize);
        m_restoreSession->setChecked(defaults.restoreSession);
        m_recentLimit->setValue(defaults.recentLimit);
    } else if (title == QLatin1String("Hex Editor")) {
        m_bytesPerRow->setCurrentIndex(std::max(0, m_bytesPerRow->findData(defaults.hexBytesPerRow)));
        m_groupSize->setValue(defaults.hexGroupSize);
    } else if (title == QLatin1String("Performance")) {
        m_workerThreads->setValue(defaults.workerThreads);
        m_cacheLimit->setValue(defaults.cacheLimitMB);
    } else if (title == QLatin1String("Keyboard Shortcuts")) {
        resetShortcutsToDefaults();
    } else if (title == QLatin1String("Privacy and Security")) {
        m_confirmWrites->setChecked(defaults.confirmWrites);
        m_logLevel->setCurrentIndex(std::max(0, m_logLevel->findData(static_cast<int>(defaults.logLevel))));
        m_fileLogging->setChecked(defaults.fileLogging);
    }
}

void SettingsDialog::resetAll()
{
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Restore defaults"),
        QStringLiteral("Restore every setting and shortcut to its default value? Custom themes "
                       "and plugins are not affected."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    const AppSettings defaults;
    m_theme->setCurrentIndex(std::max(0, m_theme->findData(defaults.themeId)));
    m_fontFamily->setCurrentIndex(0);
    m_fontSize->setValue(defaults.fontSize);
    m_lightweight->setChecked(defaults.lightweight);
    m_restoreSession->setChecked(defaults.restoreSession);
    m_recentLimit->setValue(defaults.recentLimit);
    m_bytesPerRow->setCurrentIndex(std::max(0, m_bytesPerRow->findData(defaults.hexBytesPerRow)));
    m_groupSize->setValue(defaults.hexGroupSize);
    m_workerThreads->setValue(defaults.workerThreads);
    m_cacheLimit->setValue(defaults.cacheLimitMB);
    m_confirmWrites->setChecked(defaults.confirmWrites);
    m_logLevel->setCurrentIndex(std::max(0, m_logLevel->findData(static_cast<int>(defaults.logLevel))));
    m_fileLogging->setChecked(defaults.fileLogging);
    resetShortcutsToDefaults();
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

void SettingsDialog::reject()
{
    restoreThemePreview();
    QDialog::reject();
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
