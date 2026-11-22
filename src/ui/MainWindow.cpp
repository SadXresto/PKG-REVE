#include "ui/MainWindow.h"

#include "core/AtomicFile.h"
#include "core/ByteReader.h"
#include "core/FileSource.h"
#include "core/FormatUtil.h"
#include "core/IFormatParser.h"
#include "core/Logging.h"
#include "core/ProcessInfo.h"
#include "data/ThemeManager.h"
#include "ui/AboutDialog.h"
#include "ui/ActivityBar.h"
#include "ui/AiChatPanel.h"
#include "ui/CommandPalette.h"
#include "ui/EditorArea.h"
#include "ui/FileEditor.h"
#include "ui/FileExplorer.h"
#include "ui/GameForgePanel.h"
#include "ui/HelpDialog.h"
#include "ui/HexView.h"
#include "ui/StructurePanel.h"
#include "ui/OutputPanel.h"
#include "ui/PackageInfoView.h"
#include "ui/RecentPanel.h"
#include "ui/SearchPanel.h"
#include "ui/SettingsDialog.h"
#include "ui/ThemeEditorDialog.h"
#include "ui/WelcomePage.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QDockWidget>
#include <QPainter>
#include <QRadialGradient>
#include <QPointer>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>

#ifndef PKGSTUDIO_BUILD_TYPE
#define PKGSTUDIO_BUILD_TYPE "unknown"
#endif

namespace pkg::ui {
namespace {

constexpr auto kLogCategory = "ui";

/// Blends two colours; `amount` 0 keeps `from`, 1 returns `to`.
QColor blendColours(const QColor& from, const QColor& to, double amount)
{
    const double t = std::clamp(amount, 0.0, 1.0);
    const auto channel = [t](int a, int b) {
        return static_cast<int>(std::lround(static_cast<double>(a) + (b - a) * t));
    };
    return QColor(channel(from.red(), to.red()), channel(from.green(), to.green()),
                  channel(from.blue(), to.blue()));
}

QString keySequenceText(const QKeySequence& sequence)
{
    return sequence.toString(QKeySequence::PortableText);
}

/// Preferred programming fonts, in order. Falls back to the system fixed font
/// when none of them is installed; the choice is never bundled or downloaded.
QString preferredMonospaceFamily()
{
    static const QStringList preferred = {
        QStringLiteral("JetBrains Mono"), QStringLiteral("Fira Code"),
        QStringLiteral("Cascadia Code"),   QStringLiteral("Cascadia Mono"),
        QStringLiteral("Source Code Pro"), QStringLiteral("Consolas"),
        QStringLiteral("Liberation Mono"), QStringLiteral("DejaVu Sans Mono"),
    };
    const QStringList families = QFontDatabase::families();
    for (const QString& family : preferred) {
        if (families.contains(family))
            return family;
    }
    return {};
}

/// Locates the bundled synthetic samples. The packaged build keeps them next
/// to the executable; development builds run from build/src, so the parent
/// folders are tried as well. PKGSTUDIO_SAMPLES_DIR overrides the search.
QString findSamplesDirectory()
{
    QStringList candidates;
    const QString override = qEnvironmentVariable("PKGSTUDIO_SAMPLES_DIR");
    if (!override.isEmpty())
        candidates << override;
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    candidates << applicationDirectory.filePath(QStringLiteral("samples"))
               << applicationDirectory.filePath(QStringLiteral("../samples"))
               << applicationDirectory.filePath(QStringLiteral("../../samples"))
               << QDir::current().filePath(QStringLiteral("samples"));
    for (const QString& candidate : candidates) {
        const QFileInfo marker(QDir(candidate).filePath(QStringLiteral("sample.elf")));
        if (marker.isFile())
            return QDir(candidate).absolutePath();
    }
    return {};
}

}  // namespace

MainWindow::MainWindow(Database& database, QWidget* parent)
    : QMainWindow(parent), m_database(database)
{
    m_settings = Settings::instance().values();
    // The style sheet targets this object name so a wallpaper can be painted
    // behind the whole window.
    setObjectName(QStringLiteral("pkgstudioMainWindow"));
    setWindowTitle(QStringLiteral("PKG Studio"));
    setMinimumSize(900, 600);
    resize(1280, 820);
    setAcceptDrops(true);
    setDockNestingEnabled(false);

    buildUi();
    buildCommands();
    buildMenus();
    buildStatusBar();

    connect(&Logger::instance(), &Logger::recordAdded, m_output, &OutputPanel::queueRecord);
    m_output->setRecords(Logger::instance().snapshot());

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const Theme&) { updateWindowTitle(); });
    connect(m_editors, &EditorArea::currentEditorChanged, this, [this](FileEditor* editor) {
        updateFileStatus(editor);
        updateWindowTitle();
        if (m_aiPanel != nullptr)
            m_aiPanel->refreshContext();
        if (editor != nullptr) {
            if (auto it = m_analyses.constFind(editor); it != m_analyses.constEnd()) {
                m_output->setDiagnostics({}, QStringLiteral("Analysis in progress\u2026"));
            }
        }
        refreshShortcuts();
    });
    connect(m_editors, &EditorArea::editorClosed, this, [this](FileEditor* editor) {
        m_analyses.remove(editor);
        updateFileStatus(currentEditor());
        updateWindowTitle();
    });
    connect(m_editors, &EditorArea::editorCloseRequested, this,
            &MainWindow::closeEditorWithConfirmation);

    m_memoryTimer = new QTimer(this);
    m_memoryTimer->setInterval(5000);
    connect(m_memoryTimer, &QTimer::timeout, this, &MainWindow::updateMemoryStatus);
    m_memoryTimer->start();
    updateMemoryStatus();
    applyEditorPreferences();

    logInfo(QString::fromLatin1(kLogCategory), QStringLiteral("Main window ready"));
}

MainWindow::~MainWindow()
{
    // Cancel nothing explicitly: analysis jobs are bounded header reads. The
    // watchers are owned by shared pointers and finish without a window.
    m_analyses.clear();
}

int MainWindow::openFileCount() const
{
    int count = 0;
    for (int i = 0; i < m_editors->count(); ++i) {
        if (m_editors->editorAt(i) != nullptr)
            ++count;
    }
    return count;
}

bool MainWindow::allEditorsAnalyzed() const
{
    for (int i = 0; i < m_editors->count(); ++i) {
        if (FileEditor* editor = m_editors->editorAt(i)) {
            if (!editor->hasAnalysis())
                return false;
        }
    }
    return true;
}

QStringList MainWindow::openPaths() const
{
    QStringList paths;
    for (int i = 0; i < m_editors->count(); ++i) {
        if (FileEditor* editor = m_editors->editorAt(i))
            paths << editor->path();
    }
    return paths;
}

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralHost"));
    auto* centralLayout = new QHBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    m_activity = new ActivityBar(central);
    m_activity->addItem(QStringLiteral("explorer"), QStringLiteral("Explorer"),
                        IconKind::Explorer);
    m_activity->addItem(QStringLiteral("search"), QStringLiteral("Search in project"),
                        IconKind::Search);
    m_activity->addItem(QStringLiteral("recent"), QStringLiteral("Recent files"),
                        IconKind::Recent);
    m_activity->addItem(QStringLiteral("gameforge"),
                        QStringLiteral("GameForge: project inventory, porting evidence"),
                        IconKind::GameForge);
    m_activity->addStretch();
    m_activity->addItem(QStringLiteral("themes"), QStringLiteral("Theme editor"),
                        IconKind::Palette, false);
    m_activity->addItem(QStringLiteral("settings"), QStringLiteral("Settings"),
                        IconKind::Settings, false);
    m_activity->addItem(QStringLiteral("diagnostics"), QStringLiteral("Diagnostics"),
                        IconKind::Diagnostics, false);
    m_activity->setCurrent(QStringLiteral("explorer"));
    centralLayout->addWidget(m_activity);

    m_sideStack = new QStackedWidget(central);
    m_sideStack->setMinimumWidth(200);
    m_explorer = new FileExplorer(m_sideStack);
    m_searchPanel = new SearchPanel(m_sideStack);
    m_recentPanel = new RecentPanel(m_sideStack);
    m_gameForge = new GameForgePanel(m_sideStack);
    m_sideStack->addWidget(m_explorer);
    m_sideStack->addWidget(m_searchPanel);
    m_sideStack->addWidget(m_recentPanel);
    m_sideStack->addWidget(m_gameForge);
    centralLayout->addWidget(m_sideStack);

    // Left pane: activity bar + stacked side panels. It is one child of the
    // horizontal splitter so the sidebar width stays user adjustable.
    auto* leftPane = new QWidget(central);
    auto* leftLayout = new QHBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(m_activity);
    leftLayout->addWidget(m_sideStack, 1);

    m_mainSplitter = new QSplitter(Qt::Horizontal, central);
    m_mainSplitter->addWidget(leftPane);

    m_verticalSplitter = new QSplitter(Qt::Vertical, m_mainSplitter);
    m_editors = new EditorArea(m_verticalSplitter);
    m_verticalSplitter->addWidget(m_editors);

    m_output = new OutputPanel(m_verticalSplitter);
    m_output->setMinimumHeight(120);
    m_verticalSplitter->addWidget(m_output);
    m_verticalSplitter->setStretchFactor(0, 4);
    m_verticalSplitter->setStretchFactor(1, 1);
    m_verticalSplitter->setSizes({600, 180});
    m_verticalSplitter->setChildrenCollapsible(false);

    m_mainSplitter->addWidget(m_verticalSplitter);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setSizes({290, 990});
    m_mainSplitter->setChildrenCollapsible(false);

    centralLayout->addWidget(m_mainSplitter, 1);
    setCentralWidget(central);

    // Right-hand AI chat, in the style of the chat panels in VS Code and
    // OpenCode. It follows the active editor and toggles with Ctrl+Shift+I.
    m_aiPanel = new AiChatPanel(m_database, [this]() { return currentEditor(); }, this);
    m_aiDock = new QDockWidget(QStringLiteral("AI chat"), this);
    m_aiDock->setObjectName(QStringLiteral("aiChatDock"));
    m_aiDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_aiDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                          | QDockWidget::DockWidgetClosable);
    m_aiDock->setWidget(m_aiPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_aiDock);
    m_aiDock->hide();

    m_welcome = new WelcomePage(m_editors);
    m_editors->addWelcome(m_welcome);
    m_welcome->setDevelopmentBuild(QApplication::applicationVersion());
    m_samplesPath = findSamplesDirectory();
    m_welcome->setDemoSamples(!m_samplesPath.isEmpty(), m_samplesPath);

    connect(m_activity, &ActivityBar::activated, this, &MainWindow::switchSidebar);
    connect(m_explorer, &FileExplorer::fileActivated, this, [this](const QString& path) {
        openFile(path);
    });
    connect(m_recentPanel, &RecentPanel::entryActivated, this,
            [this](const QString& path, const QString& kind) {
                if (kind == QLatin1String("project"))
                    openProject(path);
                else
                    openFile(path);
            });
    connect(m_recentPanel, &RecentPanel::removeRequested, this, [this](const QString& path) {
        m_database.removeRecent(path);
        refreshRecentPanels();
    });
    connect(m_recentPanel, &RecentPanel::clearRequested, this, [this]() {
        m_database.clearRecent();
        refreshRecentPanels();
    });
    connect(m_searchPanel, &SearchPanel::hitActivated, this,
            [this](const QString& path, quint64 offset) {
                if (!openFile(path))
                    return;
                if (auto* editor = currentEditor())
                    editor->goToOffset(offset);
            });
    connect(m_gameForge, &GameForgePanel::openFileRequested, this,
            [this](const QString& path) { openFile(path); });
    connect(m_gameForge, &GameForgePanel::openProjectRequested, this,
            [this]() { runCommand(QStringLiteral("file.openProject")); });
    connect(m_gameForge, &GameForgePanel::statusMessage, this,
            [this](const QString& message) { statusBar()->showMessage(message, 6000); });
    connect(m_welcome, &WelcomePage::openFileRequested, this, [this]() {
        runCommand(QStringLiteral("file.openFile"));
    });
    connect(m_welcome, &WelcomePage::openProjectRequested, this, [this]() {
        runCommand(QStringLiteral("file.openProject"));
    });
    connect(m_welcome, &WelcomePage::newProjectRequested, this, [this]() {
        runCommand(QStringLiteral("file.newProject"));
    });
    connect(m_welcome, &WelcomePage::sampleFileRequested, this,
            [this](const QString& fileName) { openSampleFile(fileName); });
    connect(m_welcome, &WelcomePage::sampleProjectRequested, this,
            [this]() { openSampleProject(); });
    connect(m_welcome, &WelcomePage::featureStatusRequested, this,
            [this]() { showHelpTopic(QStringLiteral("help.featureStatus")); });
    connect(m_welcome, &WelcomePage::recentActivated, this,
            [this](const QString& path, const QString& kind) {
                if (kind == QLatin1String("project"))
                    openProject(path);
                else
                    openFile(path);
            });
    connect(m_output, &OutputPanel::exportLogRequested, this, &MainWindow::exportLog);
}

void MainWindow::buildStatusBar()
{
    QStatusBar* bar = statusBar();
    bar->setObjectName(QStringLiteral("statusBar"));
    bar->setSizeGripEnabled(false);

    m_statusMode = new QLabel(QStringLiteral("read-only"), bar);
    m_statusMode->setToolTip(
        QStringLiteral("Read-only until you enable byte editing for the file. Saving asks "
                       "before overwriting and writes a timestamped backup."));
    bar->addWidget(m_statusMode);

    m_statusFile = new QLabel(QStringLiteral("no file"), bar);
    bar->addWidget(m_statusFile, 1);

    m_statusFormat = new QLabel(bar);
    bar->addWidget(m_statusFormat);

    m_statusOffset = new QLabel(bar);
    bar->addWidget(m_statusOffset);

    m_statusSelection = new QLabel(bar);
    bar->addWidget(m_statusSelection);

    m_busyBar = new QProgressBar(bar);
    m_busyBar->setRange(0, 0);
    m_busyBar->setTextVisible(false);
    m_busyBar->setFixedWidth(90);
    m_busyBar->setVisible(false);
    bar->addPermanentWidget(m_busyBar);

    m_statusMemory = new QLabel(bar);
    m_statusMemory->setToolTip(QStringLiteral("Process resident memory"));
    bar->addPermanentWidget(m_statusMemory);

    m_statusTheme = new QLabel(ThemeManager::instance().current().name, bar);
    bar->addPermanentWidget(m_statusTheme);
}

QAction* MainWindow::addCommand(const QString& id, const QString& title,
                                const QString& category,
                                const QKeySequence& defaultShortcut,
                                std::function<void()> handler)
{
    auto* action = new QAction(title, this);
    action->setObjectName(id);
    action->setShortcut(Settings::instance().shortcut(id, defaultShortcut));
    action->setShortcutContext(Qt::WindowShortcut);
    connect(action, &QAction::triggered, this,
            [this, id, handler = std::move(handler)]() {
                recordRecentCommand(id);
                handler();
            });
    addAction(action);
    m_commands.insert(id, action);
    m_defaultShortcuts.insert(id, defaultShortcut);
    m_commandCategories.insert(id, category);
    m_commandOrder.append(id);
    return action;
}

void MainWindow::buildCommands()
{
    const QString fileCategory = QStringLiteral("File");
    const QString editCategory = QStringLiteral("Edit");
    const QString viewCategory = QStringLiteral("View");
    const QString toolsCategory = QStringLiteral("Tools");
    const QString helpCategory = QStringLiteral("Help");

    addCommand(QStringLiteral("file.newProject"), QStringLiteral("New project\u2026"), fileCategory,
               QKeySequence(QStringLiteral("Ctrl+Shift+N")), [this]() { createProject(); });
    addCommand(QStringLiteral("file.openProject"), QStringLiteral("Open project folder\u2026"),
               fileCategory, QKeySequence(QStringLiteral("Ctrl+Shift+O")),
               [this]() { openProjectDialog(); });

    addCommand(QStringLiteral("file.openFile"), QStringLiteral("Open file\u2026"), fileCategory,
               QKeySequence(QStringLiteral("Ctrl+O")), [this]() {
                   const QString path = QFileDialog::getOpenFileName(
                       this, QStringLiteral("Open file"), m_lastDirectory,
                       QStringLiteral("All files (*)"));
                   if (!path.isEmpty())
                       openFile(path);
               });
    addCommand(QStringLiteral("file.save"), QStringLiteral("Save"), fileCategory,
               QKeySequence(QStringLiteral("Ctrl+S")),
               [this]() { saveActiveEditor(false); });
    addCommand(QStringLiteral("file.saveAs"), QStringLiteral("Save as\u2026"), fileCategory,
               QKeySequence(QStringLiteral("Ctrl+Shift+S")),
               [this]() { saveActiveEditor(true); });
    addCommand(QStringLiteral("file.exportReport"), QStringLiteral("Export analysis report\u2026"),
               fileCategory, QKeySequence(QStringLiteral("Ctrl+Shift+E")),
               [this]() { exportActiveReport(); });
    addCommand(QStringLiteral("file.exit"), QStringLiteral("Exit"), fileCategory,
               QKeySequence(QStringLiteral("Ctrl+Q")), [this]() { close(); });

    addCommand(QStringLiteral("edit.undo"), QStringLiteral("Undo byte edit"), editCategory,
               QKeySequence(QStringLiteral("Ctrl+Z")), [this]() {
                   FileEditor* editor = currentEditor();
                   if (editor != nullptr && !editor->undoEdit())
                       statusBar()->showMessage(QStringLiteral("Nothing to undo"), 2000);
               });
    addCommand(QStringLiteral("edit.redo"), QStringLiteral("Redo byte edit"), editCategory,
               QKeySequence(QStringLiteral("Ctrl+Shift+Z")), [this]() {
                   FileEditor* editor = currentEditor();
                   if (editor != nullptr && !editor->redoEdit())
                       statusBar()->showMessage(QStringLiteral("Nothing to redo"), 2000);
               });
    addCommand(QStringLiteral("edit.find"), QStringLiteral("Find in current file\u2026"),
               editCategory, QKeySequence(QStringLiteral("Ctrl+F")), [this]() {
                   if (FileEditor* editor = currentEditor())
                       editor->focusFindBar();
               });
    addCommand(QStringLiteral("edit.goto"), QStringLiteral("Go to offset\u2026"), editCategory,
               QKeySequence(QStringLiteral("Ctrl+G")), [this]() {
                   if (FileEditor* editor = currentEditor())
                       editor->goToOffsetInteractive();
               });
    addCommand(QStringLiteral("edit.copyHex"), QStringLiteral("Copy selection as hex"),
               editCategory, QKeySequence(QStringLiteral("Ctrl+Shift+H")), [this]() {
                   if (FileEditor* editor = currentEditor()) {
                       const QString text = editor->hexView()->selectionAsHex();
                       if (!text.isEmpty())
                           QApplication::clipboard()->setText(text);
                   }
               });
    addCommand(QStringLiteral("edit.copyAscii"), QStringLiteral("Copy selection as ASCII"),
               editCategory, QKeySequence(QStringLiteral("Ctrl+Shift+A")), [this]() {
                   if (FileEditor* editor = currentEditor()) {
                       const QString text = editor->hexView()->selectionAsAscii();
                       if (!text.isEmpty())
                           QApplication::clipboard()->setText(text);
                   }
               });
    addCommand(QStringLiteral("edit.selectAll"), QStringLiteral("Select all bytes"), editCategory,
               QKeySequence(QStringLiteral("Ctrl+Shift+End")), [this]() {
                   if (FileEditor* editor = currentEditor())
                       editor->hexView()->selectAll();
               });

    addCommand(QStringLiteral("view.explorer"), QStringLiteral("Show explorer"), viewCategory,
               QKeySequence(QStringLiteral("Ctrl+Shift+1")), [this]() {
                   switchSidebar(QStringLiteral("explorer"));
                   m_activity->setCurrent(QStringLiteral("explorer"));
               });
    addCommand(QStringLiteral("view.search"), QStringLiteral("Show project search"), viewCategory,
               QKeySequence(QStringLiteral("Ctrl+Shift+F")), [this]() {
                   switchSidebar(QStringLiteral("search"));
                   m_activity->setCurrent(QStringLiteral("search"));
                   m_searchPanel->focusSearch();
               });
    addCommand(QStringLiteral("view.recent"), QStringLiteral("Show recent files"), viewCategory,
               QKeySequence(QStringLiteral("Ctrl+Shift+R")), [this]() {
                   switchSidebar(QStringLiteral("recent"));
                   m_activity->setCurrent(QStringLiteral("recent"));
               });
    addCommand(QStringLiteral("view.gameforge"),
               QStringLiteral("GameForge: project workspace"), viewCategory,
               QKeySequence(QStringLiteral("Ctrl+Shift+G")), [this]() {
                   switchSidebar(QStringLiteral("gameforge"));
                   m_activity->setCurrent(QStringLiteral("gameforge"));
               });
    addCommand(QStringLiteral("view.togglePanel"), QStringLiteral("Toggle output panel"),
               viewCategory, QKeySequence(QStringLiteral("Ctrl+J")),
               [this]() { m_output->setVisible(!m_output->isVisible()); });
    addCommand(QStringLiteral("view.themeEditor"), QStringLiteral("Theme editor\u2026"),
               viewCategory, QKeySequence(), [this]() { showThemeEditor(); });
    addCommand(QStringLiteral("view.wallpaper"),
               QStringLiteral("Set theme background image\u2026"), viewCategory, QKeySequence(),
               [this]() { chooseThemeBackground(); });
    addCommand(QStringLiteral("view.clearWallpaper"),
               QStringLiteral("Clear theme background image"), viewCategory, QKeySequence(),
               [this]() { clearThemeBackground(); });
    addCommand(QStringLiteral("view.lightweight"), QStringLiteral("Lightweight mode"),
               viewCategory, QKeySequence(), [this]() {
                   m_settings.lightweight = !m_settings.lightweight;
                   Settings::instance().setValues(m_settings);
                   applyEditorPreferences();
                   logInfo(QString::fromLatin1(kLogCategory),
                           m_settings.lightweight ? QStringLiteral("Lightweight mode enabled")
                                                  : QStringLiteral("Lightweight mode disabled"));
               });
    addCommand(QStringLiteral("view.zoomIn"), QStringLiteral("Increase editor font size"),
               viewCategory, QKeySequence(QStringLiteral("Ctrl+=")), [this]() {
                   m_settings.fontSize = std::min(24, m_settings.fontSize + 1);
                   Settings::instance().setValues(m_settings);
                   applyEditorPreferences();
               });
    addCommand(QStringLiteral("view.zoomOut"), QStringLiteral("Decrease editor font size"),
               viewCategory, QKeySequence(QStringLiteral("Ctrl+-")), [this]() {
                   m_settings.fontSize = std::max(7, m_settings.fontSize - 1);
                   Settings::instance().setValues(m_settings);
                   applyEditorPreferences();
               });
    addCommand(QStringLiteral("view.commandPalette"), QStringLiteral("Command palette\u2026"),
               viewCategory, QKeySequence(QStringLiteral("Ctrl+Shift+P")),
               [this]() { showCommandPalette(); });

    addCommand(QStringLiteral("tools.ai"), QStringLiteral("AI chat"), toolsCategory,
               QKeySequence(QStringLiteral("Ctrl+Shift+I")),
               [this]() { showAiAssistant(); });
    addCommand(QStringLiteral("tools.settings"), QStringLiteral("Settings\u2026"), toolsCategory,
               QKeySequence(QStringLiteral("Ctrl+,")), [this]() { showSettings(); });
    addCommand(QStringLiteral("tools.exportLog"), QStringLiteral("Export log\u2026"), toolsCategory,
               QKeySequence(), [this]() { exportLog(); });
    addCommand(QStringLiteral("tools.dataFolder"), QStringLiteral("Open data folder"),
               toolsCategory, QKeySequence(), [this]() { openDataDirectory(); });
    addCommand(QStringLiteral("tools.clearCache"), QStringLiteral("Clear analysis cache"),
               toolsCategory, QKeySequence(), [this]() { clearAnalysisCache(); });
    addCommand(QStringLiteral("tools.diagnostics"), QStringLiteral("Diagnostics\u2026"),
               toolsCategory, QKeySequence(), [this]() { showDiagnosticsDialog(); });

    addCommand(QStringLiteral("help.formats"), QStringLiteral("Supported formats"), helpCategory,
               QKeySequence(), [this]() { showFormatSupport(); });
    addCommand(QStringLiteral("help.legal"), QStringLiteral("Legal notice and scope"),
               helpCategory, QKeySequence(), [this]() { showLegalNotice(); });
    addCommand(QStringLiteral("help.about"), QStringLiteral("About PKG Studio"), helpCategory,
               QKeySequence(QStringLiteral("F1")), [this]() { showAbout(); });

    addCommand(QStringLiteral("view.structureBuilder"),
               QStringLiteral("Toggle structure builder"), viewCategory,
               QKeySequence(QStringLiteral("Ctrl+Shift+B")), [this]() {
                   FileEditor* editor = currentEditor();
                   if (editor == nullptr) {
                       statusBar()->showMessage(
                           QStringLiteral("Open a file to use the structure builder"), 3000);
                       return;
                   }
                   editor->toggleStructurePanel();
               });
    addCommand(QStringLiteral("tools.structureTemplateLoad"),
               QStringLiteral("Structure: load template\u2026"), toolsCategory, QKeySequence(),
               [this]() {
                   if (FileEditor* editor = currentEditor())
                       editor->openStructureTemplate();
               });
    addCommand(QStringLiteral("tools.structureTemplateEdit"),
               QStringLiteral("Structure: edit template\u2026"), toolsCategory, QKeySequence(),
               [this]() {
                   if (FileEditor* editor = currentEditor())
                       editor->editStructureTemplate();
               });

    addCommand(QStringLiteral("help.guide"), QStringLiteral("Getting started guide"),
               helpCategory, QKeySequence(),
               [this]() { showHelpTopic(QStringLiteral("help.guide")); });
    addCommand(QStringLiteral("help.shortcuts"), QStringLiteral("Keyboard shortcuts"),
               helpCategory, QKeySequence(),
               [this]() { showHelpTopic(QStringLiteral("help.shortcuts")); });
    addCommand(QStringLiteral("help.structureTemplates"),
               QStringLiteral("Structure template guide"), helpCategory, QKeySequence(),
               [this]() { showHelpTopic(QStringLiteral("help.structureTemplates")); });
    addCommand(QStringLiteral("help.gameforge"), QStringLiteral("GameForge workspace guide"),
               helpCategory, QKeySequence(), [this]() {
                   showHelpTopic(QStringLiteral("help.gameforge"));
               });
    addCommand(QStringLiteral("help.limitations"), QStringLiteral("Limitations and scope"),
               helpCategory, QKeySequence(),
               [this]() { showHelpTopic(QStringLiteral("help.limitations")); });
    addCommand(QStringLiteral("help.codeListing"), QStringLiteral("Readable listings (Code tab)"),
               helpCategory, QKeySequence(),
               [this]() { showHelpTopic(QStringLiteral("help.codeListing")); });
    addCommand(QStringLiteral("help.ai"), QStringLiteral("AI assistant guide"), helpCategory,
               QKeySequence(), [this]() { showHelpTopic(QStringLiteral("help.ai")); });
    addCommand(QStringLiteral("help.privacy"), QStringLiteral("Privacy policy"), helpCategory,
               QKeySequence(), [this]() { showHelpTopic(QStringLiteral("help.privacy")); });
    addCommand(QStringLiteral("help.terms"), QStringLiteral("Terms of use"), helpCategory,
               QKeySequence(), [this]() { showHelpTopic(QStringLiteral("help.terms")); });

    const QString demoCategory = QStringLiteral("Demo");
    addCommand(QStringLiteral("demo.openElf"), QStringLiteral("Open ELF sample"), demoCategory,
               QKeySequence(), [this]() { openSampleFile(QStringLiteral("sample.elf")); });
    addCommand(QStringLiteral("demo.openApk"), QStringLiteral("Open APK sample"), demoCategory,
               QKeySequence(), [this]() { openSampleFile(QStringLiteral("sample.apk")); });
    addCommand(QStringLiteral("demo.openProject"), QStringLiteral("Open sample project"),
               demoCategory, QKeySequence(), [this]() { openSampleProject(); });
    addCommand(QStringLiteral("help.featureStatus"), QStringLiteral("Feature status"),
               helpCategory, QKeySequence(),
               [this]() { showHelpTopic(QStringLiteral("help.featureStatus")); });

    const auto describe = [this](const QString& id, const QString& description,
                                 const QString& keywords = QString()) {
        m_commandDescriptions.insert(id, description);
        m_commandKeywords.insert(id, keywords);
        if (QAction* action = m_commands.value(id)) {
            action->setToolTip(description);
            action->setStatusTip(description);
        }
    };
    describe(QStringLiteral("file.newProject"),
             QStringLiteral("Create a project database and working folders"),
             QStringLiteral("workspace folder"));
    describe(QStringLiteral("file.openProject"),
             QStringLiteral("Open a folder as the current project"), QStringLiteral("workspace"));
    describe(QStringLiteral("file.openFile"),
             QStringLiteral("Open a binary for read-only analysis or editing"),
             QStringLiteral("browse binary package"));
    describe(QStringLiteral("file.save"),
             QStringLiteral("Write pending byte edits with a backup and atomic replace"),
             QStringLiteral("write backup"));
    describe(QStringLiteral("file.saveAs"),
             QStringLiteral("Write pending byte edits to another path"),
             QStringLiteral("copy export"));
    describe(QStringLiteral("file.exportReport"),
             QStringLiteral("Export the current analysis text report"),
             QStringLiteral("report txt"));
    describe(QStringLiteral("file.exit"), QStringLiteral("Close every tab and quit PKG Studio"),
             QStringLiteral("quit"));
    describe(QStringLiteral("edit.undo"), QStringLiteral("Undo the last byte edit"),
             QStringLiteral("revert history"));
    describe(QStringLiteral("edit.redo"), QStringLiteral("Reapply the last undone byte edit"),
             QStringLiteral("history"));
    describe(QStringLiteral("edit.find"), QStringLiteral("Search the current file for text or bytes"),
             QStringLiteral("search hex ascii find"));
    describe(QStringLiteral("edit.goto"), QStringLiteral("Jump to a decimal or 0x hexadecimal offset"),
             QStringLiteral("seek address position"));
    describe(QStringLiteral("edit.copyHex"), QStringLiteral("Copy the selected bytes as hex text"),
             QStringLiteral("clipboard"));
    describe(QStringLiteral("edit.copyAscii"),
             QStringLiteral("Copy the selected bytes as ASCII text"),
             QStringLiteral("clipboard text"));
    describe(QStringLiteral("edit.selectAll"), QStringLiteral("Select every byte in the file"),
             QStringLiteral("selection"));
    describe(QStringLiteral("view.explorer"), QStringLiteral("Show the project file explorer"),
             QStringLiteral("sidebar files"));
    describe(QStringLiteral("view.search"),
             QStringLiteral("Search file names and contents in the project"),
             QStringLiteral("find project"));
    describe(QStringLiteral("view.recent"), QStringLiteral("Show recently opened files"),
             QStringLiteral("history"));
    describe(QStringLiteral("view.gameforge"),
             QStringLiteral("GameForge: project inventory, sizes and porting evidence"),
             QStringLiteral("game port modding assets project scanner porting api"));
    describe(QStringLiteral("view.togglePanel"), QStringLiteral("Show or hide the output panel"),
             QStringLiteral("diagnostics log panel"));
    describe(QStringLiteral("view.themeEditor"), QStringLiteral("Open the theme editor"),
             QStringLiteral("settings colors appearance void black"));
    describe(QStringLiteral("view.wallpaper"),
             QStringLiteral("Use an image as the background of the current theme"),
             QStringLiteral("wallpaper picture custom theme"));
    describe(QStringLiteral("view.clearWallpaper"),
             QStringLiteral("Remove the background image from the current theme"),
             QStringLiteral("wallpaper picture reset"));
    describe(QStringLiteral("tools.ai"),
             QStringLiteral("Open the AI chat and apply the byte edits it suggests"),
             QStringLiteral("assistant chat ollama lm studio llama.cpp openai api key local "
                            "model"));
    describe(QStringLiteral("view.lightweight"),
             QStringLiteral("Toggle reduced animation and effects"),
             QStringLiteral("performance settings"));
    describe(QStringLiteral("view.zoomIn"), QStringLiteral("Increase the editor font size"),
             QStringLiteral("settings text"));
    describe(QStringLiteral("view.zoomOut"), QStringLiteral("Decrease the editor font size"),
             QStringLiteral("settings text"));
    describe(QStringLiteral("view.commandPalette"),
             QStringLiteral("Search commands, files, analysis tools and help"),
             QStringLiteral("quick open launcher"));
    describe(QStringLiteral("view.structureBuilder"),
             QStringLiteral("Show or hide the binary structure builder"),
             QStringLiteral("layout template fields offsets hex"));
    describe(QStringLiteral("tools.settings"), QStringLiteral("Open application settings"),
             QStringLiteral("preferences options"));
    describe(QStringLiteral("tools.exportLog"), QStringLiteral("Export the session log"),
             QStringLiteral("diagnostics log"));
    describe(QStringLiteral("tools.dataFolder"),
             QStringLiteral("Open the settings, log and template folder"),
             QStringLiteral("storage templates"));
    describe(QStringLiteral("tools.clearCache"),
             QStringLiteral("Delete cached parser results"), QStringLiteral("analysis cache"));
    describe(QStringLiteral("tools.diagnostics"),
             QStringLiteral("Show runtime, memory and database diagnostics"),
             QStringLiteral("system info"));
    describe(QStringLiteral("tools.structureTemplateLoad"),
             QStringLiteral("Load a structure template JSON file"),
             QStringLiteral("layout fields import"));
    describe(QStringLiteral("tools.structureTemplateEdit"),
             QStringLiteral("Create, validate or save a structure template"),
             QStringLiteral("layout fields offsets types"));
    describe(QStringLiteral("help.formats"),
             QStringLiteral("List parsed formats and explicit non-goals"),
             QStringLiteral("supported parsers"));
    describe(QStringLiteral("help.legal"), QStringLiteral("Show the authorized-use notice"),
             QStringLiteral("license scope"));
    describe(QStringLiteral("help.codeListing"),
             QStringLiteral("Explain the readable listing and its limits"),
             QStringLiteral("disassemble assemble code tab bytes"));
    describe(QStringLiteral("help.ai"),
             QStringLiteral("Set up a local AI endpoint and apply its edits"),
             QStringLiteral("assistant ollama local model key"));
    describe(QStringLiteral("help.privacy"),
             QStringLiteral("What is stored locally and what the AI endpoint sees"),
             QStringLiteral("telemetry data policy"));
    describe(QStringLiteral("help.terms"),
             QStringLiteral("Authorized use, AI responsibility and anti-piracy terms"),
             QStringLiteral("license piracy legal eula"));
    describe(QStringLiteral("help.about"), QStringLiteral("Version, licence and build details"),
             QStringLiteral("version"));
    describe(QStringLiteral("help.guide"),
             QStringLiteral("Offline walkthrough of the main workflows"),
             QStringLiteral("tutorial documentation manual offline"));
    describe(QStringLiteral("help.shortcuts"),
             QStringLiteral("Offline reference for keyboard shortcuts"),
             QStringLiteral("keys documentation"));
    describe(QStringLiteral("help.structureTemplates"),
             QStringLiteral("Offline guide to the structure template schema"),
             QStringLiteral("documentation schema json layout"));
    describe(QStringLiteral("help.gameforge"),
             QStringLiteral("How the project scan and portability report work"),
             QStringLiteral("game port inventory evidence api documentation"));
    describe(QStringLiteral("help.limitations"),
             QStringLiteral("What is intentionally not supported or confirmed"),
             QStringLiteral("scope documentation honesty"));
    describe(QStringLiteral("demo.openElf"),
             QStringLiteral("Open the bundled synthetic ELF sample and its analysis"),
             QStringLiteral("tour example demo walkthrough"));
    describe(QStringLiteral("demo.openApk"),
             QStringLiteral("Open the bundled synthetic APK sample and its analysis"),
             QStringLiteral("tour example demo walkthrough zip"));
    describe(QStringLiteral("demo.openProject"),
             QStringLiteral("Open the bundled sample folder as a project"),
             QStringLiteral("tour example demo workspace files"));
    describe(QStringLiteral("help.featureStatus"),
             QStringLiteral("Implemented, partial/experimental and planned features"),
             QStringLiteral("experimental coming soon roadmap honesty"));
}

void MainWindow::recordRecentCommand(const QString& id)
{
    if (id == QLatin1String("view.commandPalette"))
        return;
    m_recentCommands.removeAll(id);
    m_recentCommands.prepend(id);
    while (m_recentCommands.size() > 8)
        m_recentCommands.removeLast();
}

void MainWindow::showHelpTopic(const QString& id)
{
    if (!HelpDialog::showTopic(this, id))
        logWarning(QString::fromLatin1(kLogCategory),
                   QStringLiteral("Unknown help topic '%1'").arg(id));
}

void MainWindow::buildMenus()
{
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(m_commands.value(QStringLiteral("file.newProject")));
    fileMenu->addAction(m_commands.value(QStringLiteral("file.openProject")));
    fileMenu->addAction(m_commands.value(QStringLiteral("file.openFile")));
    fileMenu->addSeparator();
    fileMenu->addAction(m_commands.value(QStringLiteral("demo.openElf")));
    fileMenu->addAction(m_commands.value(QStringLiteral("demo.openApk")));
    fileMenu->addAction(m_commands.value(QStringLiteral("demo.openProject")));
    fileMenu->addSeparator();
    fileMenu->addAction(m_commands.value(QStringLiteral("file.save")));
    fileMenu->addAction(m_commands.value(QStringLiteral("file.saveAs")));
    fileMenu->addSeparator();
    fileMenu->addAction(m_commands.value(QStringLiteral("file.exportReport")));
    fileMenu->addSeparator();
    fileMenu->addAction(m_commands.value(QStringLiteral("file.exit")));

    auto* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(m_commands.value(QStringLiteral("edit.undo")));
    editMenu->addAction(m_commands.value(QStringLiteral("edit.redo")));
    editMenu->addSeparator();
    editMenu->addAction(m_commands.value(QStringLiteral("edit.find")));
    editMenu->addAction(m_commands.value(QStringLiteral("edit.goto")));
    editMenu->addSeparator();
    editMenu->addAction(m_commands.value(QStringLiteral("edit.copyHex")));
    editMenu->addAction(m_commands.value(QStringLiteral("edit.copyAscii")));
    editMenu->addAction(m_commands.value(QStringLiteral("edit.selectAll")));
    editMenu->addSeparator();
    editMenu->addAction(m_commands.value(QStringLiteral("view.commandPalette")));

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(m_commands.value(QStringLiteral("view.explorer")));
    viewMenu->addAction(m_commands.value(QStringLiteral("view.search")));
    viewMenu->addAction(m_commands.value(QStringLiteral("view.recent")));
    viewMenu->addSeparator();
    viewMenu->addAction(m_commands.value(QStringLiteral("view.togglePanel")));
    viewMenu->addAction(m_commands.value(QStringLiteral("view.structureBuilder")));
    QAction* lightweight = m_commands.value(QStringLiteral("view.lightweight"));
    lightweight->setCheckable(true);
    lightweight->setChecked(m_settings.lightweight);
    connect(lightweight, &QAction::toggled, this, [this, lightweight](bool checked) {
        if (m_settings.lightweight != checked) {
            m_settings.lightweight = checked;
            Settings::instance().setValues(m_settings);
        }
        applyEditorPreferences();
    });
    viewMenu->addAction(lightweight);
    viewMenu->addSeparator();
    viewMenu->addAction(m_commands.value(QStringLiteral("view.zoomIn")));
    viewMenu->addAction(m_commands.value(QStringLiteral("view.zoomOut")));
    viewMenu->addSeparator();
    m_themeMenu = viewMenu->addMenu(QStringLiteral("Theme"));
    rebuildThemeMenu();
    viewMenu->addAction(m_commands.value(QStringLiteral("view.themeEditor")));
    viewMenu->addAction(m_commands.value(QStringLiteral("view.wallpaper")));
    viewMenu->addAction(m_commands.value(QStringLiteral("view.clearWallpaper")));

    auto* toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));
    toolsMenu->addAction(m_commands.value(QStringLiteral("view.gameforge")));
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_commands.value(QStringLiteral("tools.ai")));
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_commands.value(QStringLiteral("tools.settings")));
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_commands.value(QStringLiteral("tools.exportLog")));
    toolsMenu->addAction(m_commands.value(QStringLiteral("tools.dataFolder")));
    toolsMenu->addAction(m_commands.value(QStringLiteral("tools.clearCache")));
    toolsMenu->addAction(m_commands.value(QStringLiteral("tools.diagnostics")));
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_commands.value(QStringLiteral("tools.structureTemplateLoad")));
    toolsMenu->addAction(m_commands.value(QStringLiteral("tools.structureTemplateEdit")));

    auto* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->addAction(m_commands.value(QStringLiteral("help.guide")));
    helpMenu->addAction(m_commands.value(QStringLiteral("help.shortcuts")));
    helpMenu->addAction(m_commands.value(QStringLiteral("help.structureTemplates")));
    helpMenu->addAction(m_commands.value(QStringLiteral("help.codeListing")));
    helpMenu->addAction(m_commands.value(QStringLiteral("help.gameforge")));
    helpMenu->addAction(m_commands.value(QStringLiteral("help.ai")));
    helpMenu->addAction(m_commands.value(QStringLiteral("help.limitations")));
    helpMenu->addSeparator();
    helpMenu->addAction(m_commands.value(QStringLiteral("help.formats")));
    helpMenu->addAction(m_commands.value(QStringLiteral("help.legal")));
    helpMenu->addAction(m_commands.value(QStringLiteral("help.privacy")));
    helpMenu->addAction(m_commands.value(QStringLiteral("help.terms")));
    helpMenu->addSeparator();
    helpMenu->addAction(m_commands.value(QStringLiteral("demo.openProject")));
    helpMenu->addAction(m_commands.value(QStringLiteral("help.featureStatus")));
    helpMenu->addSeparator();
    helpMenu->addAction(m_commands.value(QStringLiteral("help.about")));
}

void MainWindow::rebuildThemeMenu()
{
    if (m_themeMenu == nullptr)
        return;
    m_themeMenu->clear();
    ThemeManager& manager = ThemeManager::instance();
    for (const Theme& theme : manager.themes()) {
        auto* action = m_themeMenu->addAction(theme.name);
        action->setCheckable(true);
        action->setChecked(theme.id == manager.current().id);
        connect(action, &QAction::triggered, this, [this, id = theme.id]() {
            m_settings.themeId = id;
            Settings::instance().setValues(m_settings);
            applyTheme();
        });
    }
    m_themeMenu->addSeparator();
    m_themeMenu->addAction(m_commands.value(QStringLiteral("view.themeEditor")));
}

void MainWindow::refreshShortcuts()
{
    for (auto it = m_commands.constBegin(); it != m_commands.constEnd(); ++it) {
        it.value()->setShortcut(
            Settings::instance().shortcut(it.key(), m_defaultShortcuts.value(it.key())));
    }
}

void MainWindow::runCommand(const QString& id)
{
    QAction* action = m_commands.value(id);
    if (action == nullptr) {
        logWarning(QString::fromLatin1(kLogCategory),
                   QStringLiteral("Unknown command '%1'").arg(id));
        return;
    }
    action->trigger();
}

void MainWindow::showCommandPalette()
{
    QList<CommandPalette::Entry> entries;

    const auto addRecent = [&entries](const QString& id, QAction* action) {
        CommandPalette::Entry entry;
        entry.id = QStringLiteral("cmd:") + id;
        entry.title = action->text();
        entry.category = QStringLiteral("Recent");
        entry.shortcut = keySequenceText(action->shortcut());
        entry.description = QStringLiteral("Recently used command");
        entry.keywords = QStringLiteral("history recent");
        entries.append(entry);
    };
    for (const QString& id : m_recentCommands) {
        if (QAction* action = m_commands.value(id); action != nullptr && action->isEnabled())
            addRecent(id, action);
    }

    entries.reserve(entries.size() + m_commandOrder.size() + 16);
    for (const QString& id : m_commandOrder) {
        QAction* action = m_commands.value(id);
        if (action == nullptr || !action->isEnabled())
            continue;
        CommandPalette::Entry entry;
        entry.id = QStringLiteral("cmd:") + id;
        entry.title = action->text();
        entry.category = m_commandCategories.value(id);
        entry.shortcut = keySequenceText(action->shortcut());
        entry.description = m_commandDescriptions.value(
            id, QStringLiteral("%1 command").arg(entry.category));
        entry.keywords = m_commandKeywords.value(id) + QLatin1Char(' ') + entry.category;
        entries.append(entry);
    }

    // Open files are first-class palette results, not just tab titles.
    for (int i = 0; i < m_editors->count(); ++i) {
        FileEditor* editor = m_editors->editorAt(i);
        if (editor == nullptr || editor->path().isEmpty())
            continue;
        CommandPalette::Entry entry;
        entry.id = QStringLiteral("file:") + editor->path();
        entry.title = QStringLiteral("Switch to %1").arg(QFileInfo(editor->path()).fileName());
        entry.category = QStringLiteral("Open file");
        entry.description = editor->path();
        entry.keywords = QStringLiteral("tab document switch jump");
        entries.append(entry);
    }

    if (FileEditor* editor = currentEditor()) {
        CommandPalette::Entry analysis;
        analysis.id = QStringLiteral("analysis.show");
        analysis.title = QStringLiteral("Show analysis for %1")
                             .arg(QFileInfo(editor->path()).fileName());
        analysis.category = QStringLiteral("Analysis");
        analysis.description =
            QStringLiteral("Open the Analysis tab with parser findings and warnings");
        analysis.keywords = QStringLiteral("parser format report hex structure");
        entries.append(analysis);
    }

    for (const HelpDialog::Topic& topic : HelpDialog::topics()) {
        CommandPalette::Entry entry;
        entry.id = QStringLiteral("cmd:") + topic.id;
        entry.title = topic.title;
        entry.category = QStringLiteral("Documentation");
        entry.description = topic.description;
        entry.keywords = QStringLiteral("help docs offline manual tutorial guide");
        entries.append(entry);
    }

    CommandPalette palette(entries, this);
    QString chosen;
    connect(&palette, &CommandPalette::commandChosen, this,
            [&chosen](const QString& id) { chosen = id; });
    palette.exec();
    if (chosen.isEmpty())
        return;

    if (chosen.startsWith(QLatin1String("cmd:"))) {
        runCommand(chosen.mid(4));
    } else if (chosen.startsWith(QLatin1String("file:"))) {
        if (FileEditor* editor = m_editors->editorForPath(chosen.mid(5))) {
            m_editors->setCurrentWidget(editor);
            statusBar()->showMessage(QStringLiteral("Switched to %1").arg(editor->path()), 3000);
        }
    } else if (chosen == QLatin1String("analysis.show")) {
        if (FileEditor* editor = currentEditor())
            editor->showAnalysisTab();
    }
}

void MainWindow::switchSidebar(const QString& id)
{
    if (id == QLatin1String("settings")) {
        showSettings();
        return;
    }
    if (id == QLatin1String("themes")) {
        showThemeEditor();
        return;
    }
    if (id == QLatin1String("diagnostics")) {
        showDiagnosticsDialog();
        return;
    }
    if (id == QLatin1String("search")) {
        m_sideStack->setCurrentWidget(m_searchPanel);
        m_searchPanel->focusSearch();
        return;
    }
    if (id == QLatin1String("recent")) {
        m_sideStack->setCurrentWidget(m_recentPanel);
        return;
    }
    if (id == QLatin1String("gameforge")) {
        m_sideStack->setCurrentWidget(m_gameForge);
        return;
    }
    m_sideStack->setCurrentWidget(m_explorer);
}

QFont MainWindow::editorFont() const
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!m_settings.fontFamily.isEmpty()) {
        font.setFamily(m_settings.fontFamily);
    } else {
        const QString preferred = preferredMonospaceFamily();
        if (!preferred.isEmpty())
            font.setFamily(preferred);
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(m_settings.fontSize);
    return font;
}

HexLayout MainWindow::hexLayout() const
{
    HexLayout layout;
    layout.bytesPerRow = m_settings.hexBytesPerRow;
    layout.groupSize = m_settings.hexGroupSize;
    return layout.normalized();
}

void MainWindow::applyEditorPreferences()
{
    const QFont font = editorFont();
    for (int i = 0; i < m_editors->count(); ++i) {
        if (FileEditor* editor = m_editors->editorAt(i)) {
            editor->setEditorFont(font);
            editor->setHexLayout(hexLayout());
        }
    }
    m_activity->setIconColor(ThemeManager::instance().current()
                                 .color(QStringLiteral("activityBarText")));
    if (m_settings.lightweight) {
        QApplication::setEffectEnabled(Qt::UI_AnimateMenu, false);
        QApplication::setEffectEnabled(Qt::UI_AnimateCombo, false);
        QApplication::setEffectEnabled(Qt::UI_AnimateTooltip, false);
        QApplication::setEffectEnabled(Qt::UI_FadeMenu, false);
        QApplication::setEffectEnabled(Qt::UI_FadeTooltip, false);
    }
}

void MainWindow::applyTheme()
{
    ThemeManager& manager = ThemeManager::instance();
    const Status status = manager.setCurrent(m_settings.themeId);
    if (!status) {
        logWarning(QString::fromLatin1(kLogCategory),
                   QStringLiteral("Theme '%1' not found; falling back to the default")
                       .arg(m_settings.themeId));
        m_settings.themeId = manager.current().id;
    }
    if (QApplication* app = qobject_cast<QApplication*>(QApplication::instance())) {
        ThemeManager::apply(manager.current(), *app);
        app->setFont(editorFont());
    }
    m_statusTheme->setText(manager.current().name);
    applyEditorPreferences();
    rebuildThemeMenu();
}

void MainWindow::refreshRecentPanels()
{
    const QList<RecentEntry> entries = m_database.recent(m_settings.recentLimit);
    m_recentPanel->setEntries(entries);
    m_welcome->setRecent(entries);
}

void MainWindow::setProjectPath(const QString& path)
{
    m_projectPath = path;
    m_explorer->setRootPath(path);
    m_searchPanel->setRootPath(path);
    m_gameForge->setProjectRoot(path);
    m_welcome->setProjectName(QFileInfo(path).fileName());
    if (!path.isEmpty()) {
        m_settings.lastProject = path;
        Settings::instance().setValues(m_settings);
        m_database.addRecent(path, QStringLiteral("project"));
    }
    refreshRecentPanels();
    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    QString title = QStringLiteral("PKG Studio");
    if (!m_projectPath.isEmpty())
        title += QStringLiteral(" \u2014 ") + QFileInfo(m_projectPath).fileName();
    if (FileEditor* editor = currentEditor()) {
        title += QStringLiteral(" \u2014 ") + QFileInfo(editor->path()).fileName();
        if (editor->isModified())
            title += QStringLiteral(" \u25CF");
    }
    setWindowTitle(title);
}

FileEditor* MainWindow::currentEditor() const
{
    return m_editors ? m_editors->currentEditor() : nullptr;
}

bool MainWindow::showAnalysisTabForCurrentEditor()
{
    FileEditor* editor = currentEditor();
    if (editor == nullptr)
        return false;
    editor->showAnalysisTab();
    return true;
}

bool MainWindow::showByteInspectorForCurrentEditor()
{
    FileEditor* editor = currentEditor();
    if (editor == nullptr)
        return false;
    editor->showByteInspectorAt(editor->hexView()->cursorOffset());
    return true;
}

bool MainWindow::showStructureForCurrentEditor()
{
    FileEditor* editor = currentEditor();
    if (editor == nullptr || editor->structurePanel() == nullptr)
        return false;
    QString templateId = QStringLiteral("builtin.elf64-le-header");
    switch (editor->analysisInfo().kind) {
    case FormatKind::Zip:
    case FormatKind::Apk:
        templateId = QStringLiteral("builtin.zip-local-header");
        break;
    default:
        break;
    }
    editor->showStructurePanel(true);
    return editor->structurePanel()->selectTemplateById(templateId);
}

void MainWindow::updateFileStatus(FileEditor* editor)
{
    if (editor == nullptr) {
        m_statusFile->setText(QStringLiteral("no file"));
        m_statusFormat->clear();
        m_statusOffset->clear();
        m_statusSelection->clear();
        updateModeStatus();
        return;
    }
    m_statusFile->setText(QStringLiteral("%1 \u2014 %2")
                              .arg(QFileInfo(editor->path()).fileName(),
                                   fmt::humanBytes(editor->fileSize())));
    m_statusFile->setToolTip(editor->path());
    updateCursorStatus(editor, editor->hexView()->cursorOffset(),
                       editor->hexView()->selectionLength());
    if (editor->hasAnalysis()) {
        m_statusFormat->setText(editor->analysisInfo().formatName);
        m_output->setDiagnostics(editor->analysisInfo().warnings, editor->path());
    } else {
        m_statusFormat->clear();
        m_output->setDiagnostics(QStringList{}, QStringLiteral("Analysis in progress…"));
    }
    updateModeStatus();
}

void MainWindow::updateModeStatus()
{
    FileEditor* editor = currentEditor();
    if (editor == nullptr) {
        m_statusMode->setText(QStringLiteral("read-only"));
        return;
    }
    if (editor->isModified())
        m_statusMode->setText(QStringLiteral("modified \u25CF"));
    else if (editor->isEditingEnabled())
        m_statusMode->setText(QStringLiteral("editing"));
    else
        m_statusMode->setText(QStringLiteral("read-only"));
}

void MainWindow::updateCursorStatus(FileEditor* editor, quint64 offset, quint64 selectionLength)
{
    if (editor == nullptr)
        return;
    m_statusOffset->setText(QStringLiteral("offset %1").arg(fmt::hexPrefixed(offset, 8)));
    if (selectionLength > 1)
        m_statusSelection->setText(QStringLiteral("%1 bytes selected").arg(selectionLength));
    else
        m_statusSelection->clear();
}

void MainWindow::updateMemoryStatus()
{
    const auto resident = sysinfo::currentResidentBytes();
    if (resident)
        m_statusMemory->setText(QStringLiteral("RSS %1").arg(fmt::humanBytes(resident.value())));
    else
        m_statusMemory->setText(QStringLiteral("RSS n/a"));
}

void MainWindow::setBusy(bool busy, const QString& message)
{
    if (m_busyBar != nullptr)
        m_busyBar->setVisible(busy);
    if (busy)
        m_statusMode->setText(message.isEmpty() ? QStringLiteral("working\u2026") : message);
    else
        updateModeStatus();
}

void MainWindow::startAnalysis(FileEditor* editor, std::shared_ptr<HexDocument> document)
{
    if (editor == nullptr || !document)
        return;

    const QString path = document->path();
    const quint64 size = document->size();
    const QFileInfo info(path);
    const qint64 modified = info.lastModified().toMSecsSinceEpoch();

    const auto cached = m_database.cachedAnalysis(path, size, modified);
    if (cached) {
        const QJsonDocument cachedJson = QJsonDocument::fromJson(cached->payload);
        auto infoResult = PackageInfo::fromJson(cachedJson.object());
        if (infoResult) {
            editor->setAnalysis(infoResult.value());
            m_output->setDiagnostics(infoResult.value().warnings, path);
            m_statusFormat->setText(infoResult.value().formatName);
            if (editor == m_pendingAnalysisFocus) {
                editor->showAnalysisTab();
                m_pendingAnalysisFocus.clear();
            }
            logInfo(QString::fromLatin1(kLogCategory),
                    QStringLiteral("Analysis cache hit for %1").arg(path));
            return;
        }
        logWarning(QString::fromLatin1(kLogCategory),
                   QStringLiteral("Discarding invalid cached analysis for %1").arg(path));
    }

    ++m_activeAnalyses;
    setBusy(true, QStringLiteral("Analyzing %1\u2026").arg(QFileInfo(path).fileName()));

    auto watcher = std::make_shared<QFutureWatcher<Result<PackageInfo>>>();
    m_analyses.insert(editor, watcher);

    QPointer<FileEditor> guard(editor);
    connect(watcher.get(), &QFutureWatcher<Result<PackageInfo>>::finished, this,
            [this, guard, watcher, path, size, modified]() {
                m_activeAnalyses = std::max(0, m_activeAnalyses - 1);
                setBusy(m_activeAnalyses > 0,
                        m_activeAnalyses > 0 ? QStringLiteral("Analyzing\u2026")
                                             : QStringLiteral("read-only"));
                if (guard) {
                    m_analyses.remove(guard.data());
                    const Result<PackageInfo> result = watcher->result();
                    if (!result) {
                        const QString message =
                            result.error().detail.isEmpty()
                                ? result.error().message
                                : QStringLiteral("%1 (%2)")
                                      .arg(result.error().message, result.error().detail);
                        guard->setAnalysisError(message);
                        m_output->setDiagnostics({message}, path);
                        logError(QString::fromLatin1(kLogCategory),
                                 QStringLiteral("Analysis failed for %1: %2").arg(path, message));
                        return;
                    }

                    const PackageInfo& package = result.value();
                    guard->setAnalysis(package);
                    m_output->setDiagnostics(package.warnings, path);
                    if (guard.data() == m_editors->currentEditor())
                        m_statusFormat->setText(package.formatName);
                    if (guard.data() == m_pendingAnalysisFocus) {
                        guard->showAnalysisTab();
                        m_pendingAnalysisFocus.clear();
                    }

                    const QByteArray payload =
                        QJsonDocument(package.toJson()).toJson(QJsonDocument::Compact);
                    m_database.cacheAnalysis(path, size, modified, package.formatName, payload);
                    m_database.trimAnalysisCache(300);

                    logInfo(QString::fromLatin1(kLogCategory),
                            QStringLiteral("Analyzed %1 as %2 (%3 warning(s))")
                                .arg(path, package.formatName)
                                .arg(package.warnings.size()));
                }
            });

    CancelToken token;
    watcher->setFuture(QtConcurrent::run([document, token]() {
        return ParserRegistry::instance().analyze(document->source(), token);
    }));
}

bool MainWindow::openFile(const QString& path)
{
    if (path.isEmpty())
        return false;
    const QFileInfo info(path);
    const QString absolute = info.absoluteFilePath();

    if (auto* existing = m_editors->editorForPath(absolute)) {
        m_editors->setCurrentWidget(existing);
        return true;
    }

    auto source = FileSource::open(absolute);
    if (!source) {
        const QString message = source.error().detail.isEmpty()
                                    ? source.error().message
                                    : QStringLiteral("%1 (%2)").arg(source.error().message,
                                                                   source.error().detail);
        QMessageBox::warning(this, QStringLiteral("Cannot open file"), message);
        logError(QString::fromLatin1(kLogCategory),
                 QStringLiteral("Cannot open %1: %2").arg(absolute, message));
        return false;
    }

    auto document = std::make_shared<HexDocument>(std::move(source.value()));
    auto* editor = new FileEditor(document, this);
    editor->setEditorFont(editorFont());
    editor->setHexLayout(hexLayout());

    connect(editor, &FileEditor::cursorMoved, this, [this, editor](quint64 offset) {
        if (editor == m_editors->currentEditor())
            updateCursorStatus(editor, offset, editor->hexView()->selectionLength());
    });
    connect(editor, &FileEditor::selectionChanged, this,
            [this, editor](quint64, quint64 length) {
                if (editor == m_editors->currentEditor())
                    updateCursorStatus(editor, editor->hexView()->cursorOffset(), length);
            });
    connect(editor, &FileEditor::analysisExportRequested, this,
            &MainWindow::exportActiveReport);
    connect(editor, &FileEditor::modificationChanged, this, [this, editor](bool) {
        m_editors->updateTitle(editor);
        if (editor == currentEditor()) {
            updateModeStatus();
            updateWindowTitle();
        }
    });
    connect(editor, &FileEditor::pathChanged, this, [this, editor](const QString&) {
        m_editors->updateTitle(editor);
        if (editor == currentEditor()) {
            updateFileStatus(editor);
            updateWindowTitle();
        }
    });

    m_editors->addEditor(editor);
    m_lastDirectory = info.absolutePath();
    m_database.addRecent(absolute, QStringLiteral("file"));
    refreshRecentPanels();
    updateFileStatus(editor);
    updateWindowTitle();

    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("Opened %1 (%2 bytes)").arg(absolute).arg(document->size()));

    startAnalysis(editor, document);
    return true;
}

void MainWindow::openProject(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        QMessageBox::warning(this, QStringLiteral("Cannot open project"),
                             QStringLiteral("%1 is not a folder.").arg(path));
        return;
    }
    setProjectPath(info.absoluteFilePath());
    switchSidebar(QStringLiteral("explorer"));
    m_activity->setCurrent(QStringLiteral("explorer"));
    m_lastDirectory = info.absolutePath();
    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("Project opened: %1").arg(m_projectPath));
}

void MainWindow::createProject()
{
    const QString parent = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose a parent folder for the new project"), m_lastDirectory);
    if (parent.isEmpty())
        return;

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("New project"), QStringLiteral("Folder name:"),
        QLineEdit::Normal, QStringLiteral("new-project"), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    const QString target = QDir(parent).filePath(name.trimmed());
    if (QFileInfo::exists(target)) {
        QMessageBox::warning(this, QStringLiteral("Cannot create project"),
                             QStringLiteral("A file or folder already exists at %1").arg(target));
        return;
    }
    if (!QDir().mkpath(target)) {
        QMessageBox::warning(this, QStringLiteral("Cannot create project"),
                             QStringLiteral("The folder could not be created."));
        logError(QString::fromLatin1(kLogCategory),
                 QStringLiteral("Cannot create project folder %1").arg(target));
        return;
    }
    logInfo(QString::fromLatin1(kLogCategory), QStringLiteral("Created project %1").arg(target));
    openProject(target);
}

void MainWindow::openProjectDialog()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Open project folder"), m_lastDirectory);
    if (!directory.isEmpty())
        openProject(directory);
}

void MainWindow::openSampleFile(const QString& fileName)
{
    if (m_samplesPath.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("Sample files not installed"),
            QStringLiteral("The bundled synthetic samples were not found next to this build. "
                           "Open any file you are authorized to inspect instead."));
        return;
    }
    const QString path = QDir(m_samplesPath).filePath(fileName);
    if (!QFileInfo(path).isFile()) {
        QMessageBox::warning(this, QStringLiteral("Sample file missing"),
                             QStringLiteral("Expected the sample at %1.").arg(path));
        return;
    }
    if (!openFile(path))
        return;
    FileEditor* editor = currentEditor();
    m_pendingAnalysisFocus = editor;
    if (editor != nullptr && editor->hasAnalysis()) {
        editor->showAnalysisTab();
        m_pendingAnalysisFocus.clear();
    }
}

void MainWindow::openSampleProject()
{
    if (m_samplesPath.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("Sample files not installed"),
            QStringLiteral("The bundled synthetic samples were not found next to this build. "
                           "Open a project folder you are authorized to inspect instead."));
        return;
    }
    openProject(m_samplesPath);
    statusBar()->showMessage(QStringLiteral("Sample project opened: %1").arg(m_samplesPath),
                             4000);
}

void MainWindow::showWelcome()
{
    m_editors->setCurrentWidget(m_welcome);
    updateFileStatus(nullptr);
    updateWindowTitle();
}

void MainWindow::showSettings()
{
    QList<SettingsDialog::ActionInfo> actions;
    for (const QString& id : m_commandOrder) {
        const QAction* action = m_commands.value(id);
        if (action == nullptr)
            continue;
        SettingsDialog::ActionInfo info;
        info.id = id;
        info.title = action->text();
        info.category = m_commandCategories.value(id);
        info.defaultShortcut = m_defaultShortcuts.value(id);
        actions.append(info);
    }

    SettingsDialog dialog(m_settings, actions, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    if (dialog.shortcutsWereReset())
        Settings::instance().resetShortcuts();

    const QHash<QString, QKeySequence> overrides = dialog.shortcutOverrides();
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        if (it.value().isEmpty())
            Settings::instance().setShortcut(it.key(), QKeySequence());
        else if (it.value() != m_defaultShortcuts.value(it.key()))
            Settings::instance().setShortcut(it.key(), it.value());
        else
            Settings::instance().setShortcut(it.key(), QKeySequence());
    }

    m_settings = dialog.values();
    Settings::instance().setValues(m_settings);
    Logger::instance().setMinLevel(m_settings.logLevel);
    Logger::instance().setFileLoggingEnabled(m_settings.fileLogging);

    refreshShortcuts();
    applyTheme();
    applyEditorPreferences();

    QAction* lightweight = m_commands.value(QStringLiteral("view.lightweight"));
    if (lightweight != nullptr && lightweight->isChecked() != m_settings.lightweight) {
        QSignalBlocker blocker(lightweight);
        lightweight->setChecked(m_settings.lightweight);
    }

    logInfo(QString::fromLatin1(kLogCategory), QStringLiteral("Settings applied"));
}

void MainWindow::showThemeEditor()
{
    ThemeManager& manager = ThemeManager::instance();
    Theme base = manager.current();
    base.id = base.id + QStringLiteral(".custom");
    base.name = base.name + QStringLiteral(" (custom)");

    ThemeEditorDialog dialog(base, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    Theme theme = dialog.theme();
    // Copy a wallpaper into the theme folder so the theme stays portable; a
    // theme without an image is written as plain JSON.
    const Status saved = theme.backgroundImage.isEmpty()
        ? manager.saveUserTheme(theme)
        : manager.setWallpaper(theme.id, theme.backgroundImage, theme.backgroundOpacity);
    if (!saved) {
        QMessageBox::warning(this, QStringLiteral("Cannot save theme"),
                             saved.error().detail.isEmpty() ? saved.error().message
                                                            : saved.error().detail);
        return;
    }
    const Status applied = manager.setCurrent(theme.id);
    if (!applied)
        logWarning(QString::fromLatin1(kLogCategory),
                   QStringLiteral("Custom theme could not be applied: %1")
                       .arg(applied.error().message));
    m_settings.themeId = theme.id;
    Settings::instance().setValues(m_settings);
    applyTheme();
    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("Custom theme saved: %1").arg(theme.id));
}

bool MainWindow::showCodeTabForCurrentEditor()
{
    FileEditor* editor = currentEditor();
    if (editor == nullptr)
        return false;
    editor->refreshCodeListing();
    editor->showCodeTab();
    return true;
}

void MainWindow::showAiAssistant()
{
    if (m_aiDock == nullptr)
        return;
    if (m_aiDock->isVisible()) {
        m_aiDock->hide();
        return;
    }
    m_aiDock->show();
    m_aiDock->raise();
    resizeDocks({m_aiDock}, {420}, Qt::Horizontal);
    m_aiPanel->refreshContext();
    m_aiPanel->focusInput();
}

FileEditor* MainWindow::activeEditor() const
{
    return currentEditor();
}

void MainWindow::chooseThemeBackground()
{
    ThemeManager& manager = ThemeManager::instance();
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Theme background image"), QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp *.gif)"));
    if (path.isEmpty())
        return;
    const Theme& theme = manager.current();
    const Status status = manager.setWallpaper(theme.id, path, theme.backgroundOpacity);
    if (!status) {
        QMessageBox::warning(this, QStringLiteral("Cannot set the background image"),
                             status.error().detail.isEmpty() ? status.error().message
                                                             : status.error().detail);
        return;
    }
    applyTheme();
}

void MainWindow::clearThemeBackground()
{
    ThemeManager& manager = ThemeManager::instance();
    const Theme& theme = manager.current();
    if (theme.backgroundImage.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("This theme has no background image"), 3000);
        return;
    }
    const Status status = manager.setWallpaper(theme.id, QString(), 100);
    if (!status) {
        QMessageBox::warning(this, QStringLiteral("Cannot clear the background image"),
                             status.error().detail.isEmpty() ? status.error().message
                                                             : status.error().detail);
        return;
    }
    applyTheme();
}

QString MainWindow::backupPathFor(const QString& path) const
{
    const QFileInfo info(path);
    const QString directory = info.absolutePath();
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString base = QStringLiteral("%1.pkgstudio-backup-%2")
                             .arg(info.fileName(), stamp);
    QString candidate = QDir(directory).filePath(base + QStringLiteral(".bak"));
    int suffix = 2;
    while (QFileInfo::exists(candidate) && suffix < 100) {
        candidate =
            QDir(directory).filePath(QStringLiteral("%1-%2.bak").arg(base).arg(suffix));
        ++suffix;
    }
    return candidate;
}

void MainWindow::saveActiveEditor(bool saveAs)
{
    FileEditor* editor = currentEditor();
    if (editor == nullptr) {
        statusBar()->showMessage(QStringLiteral("Open a file before saving"), 3000);
        return;
    }
    saveEditor(editor, saveAs);
}

bool MainWindow::saveEditor(FileEditor* editor, bool saveAs)
{
    if (editor == nullptr)
        return false;

    if (!saveAs && !editor->isModified()) {
        statusBar()->showMessage(QStringLiteral("No unsaved changes"), 3000);
        return true;
    }

    QString target = editor->path();
    if (saveAs) {
        const QString chosen = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save file as"), target,
            QStringLiteral("All files (*)"));
        if (chosen.isEmpty())
            return false;
        target = chosen;
    }
    if (target.isEmpty())
        return false;

    const QFileInfo targetInfo(target);
    const bool overwriting = targetInfo.exists();
    const QString backupPath = backupPathFor(target);

    if (overwriting) {
        if (m_settings.confirmWrites) {
            QMessageBox box(QMessageBox::Warning, QStringLiteral("Overwrite file?"),
                            QStringLiteral("<b>%1</b> already exists.")
                                .arg(targetInfo.fileName().toHtmlEscaped()),
                            QMessageBox::NoButton, this);
            box.setTextFormat(Qt::RichText);
            box.setInformativeText(
                QStringLiteral("Saving replaces its contents atomically, after writing a backup "
                               "of the current file to:\n%1\n\nThis cannot be undone once the "
                               "original is replaced.")
                    .arg(backupPath));
            auto* overwriteButton =
                box.addButton(QStringLiteral("Overwrite"), QMessageBox::DestructiveRole);
            auto* cancelButton = box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(cancelButton);
            box.exec();
            if (box.clickedButton() != overwriteButton)
                return false;
        }

    }

    std::shared_ptr<HexDocument> document = editor->document();
    if (!document) {
        QMessageBox::critical(this, QStringLiteral("Cannot save file"),
                              QStringLiteral("The document is no longer open."));
        return false;
    }

    // Backup and save both stream the file, which can take a while for large
    // packages. Run them on the shared worker pool behind a window-modal
    // progress dialog so the window keeps painting and Cancel stays live.
    // Edits cannot change meanwhile: the modal dialog blocks input, and the
    // document is only read (mmap + override map) by the worker.
    QProgressDialog progress(this);
    progress.setWindowTitle(QStringLiteral("Saving file"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setLabelText(QStringLiteral("Preparing save\u2026"));
    progress.setCancelButtonText(QStringLiteral("Cancel"));
    progress.setRange(0, 100);
    progress.setMinimumDuration(250);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setValue(0);

    CancelToken token;
    connect(&progress, &QProgressDialog::canceled, &progress,
            [token]() { token.cancel(); });

    QPointer<QProgressDialog> dialog(&progress);
    const auto reporter = [dialog, token](int base, int span, const QString& label) {
        return [dialog, token, base, span, label](quint64 done, quint64 total) {
            if (token.isCancelled())
                return;
            const int percent =
                total == 0 ? base + span
                           : base + static_cast<int>((done * static_cast<quint64>(span)) / total);
            QObject* context = dialog.data();
            if (context == nullptr)
                return;
            QMetaObject::invokeMethod(
                context,
                [dialog, percent, label]() {
                    if (!dialog)
                        return;
                    dialog->setLabelText(label);
                    dialog->setValue(std::clamp(percent, 0, 100));
                },
                Qt::QueuedConnection);
        };
    };

    const bool hadExistingFile = overwriting;
    const auto backupReporter =
        reporter(0, hadExistingFile ? 45 : 0, QStringLiteral("Writing backup\u2026"));
    const auto saveReporter = reporter(hadExistingFile ? 45 : 0, hadExistingFile ? 55 : 100,
                                       QStringLiteral("Saving changes\u2026"));

    auto watcher = std::make_shared<QFutureWatcher<Result<quint64>>>();
    QEventLoop loop;
    connect(watcher.get(), &QFutureWatcher<Result<quint64>>::finished, &loop,
            &QEventLoop::quit);
    watcher->setFuture(QtConcurrent::run(
        [document, target, backupPath, hadExistingFile, token, backupReporter, saveReporter]() {
            if (hadExistingFile) {
                const auto backup =
                    atomic::backupFile(target, backupPath, token, backupReporter);
                if (!backup)
                    return Result<quint64>(backup.error());
            }
            return document->saveTo(target, token, saveReporter);
        }));
    loop.exec();
    progress.close();

    const Result<quint64> written = watcher->result();
    if (token.isCancelled()) {
        statusBar()->showMessage(QStringLiteral("Save cancelled; edits were kept"), 4000);
        logInfo(QString::fromLatin1(kLogCategory),
                QStringLiteral("Save cancelled for %1").arg(target));
        return false;
    }
    if (!written) {
        const QString detail = written.error().detail.isEmpty()
                                   ? written.error().message
                                   : written.error().detail;
        QMessageBox::critical(this, QStringLiteral("Cannot save file"),
                              QStringLiteral("%1\n\nThe original file was left untouched.")
                                  .arg(detail));
        logError(QString::fromLatin1(kLogCategory),
                 QStringLiteral("Save failed for %1: %2").arg(target, detail));
        return false;
    }

    // The worker only wrote the file; reopen it and drop edit state on the UI
    // thread before any repaint can observe a half-updated document.
    const Status reloaded = editor->reloadFrom(target);
    if (!reloaded) {
        const QString detail = reloaded.error().detail.isEmpty()
                                   ? reloaded.error().message
                                   : reloaded.error().detail;
        QMessageBox::critical(this, QStringLiteral("Saved, but reload failed"),
                              QStringLiteral("The file was written, but reopening it failed.\n\n%1")
                                  .arg(detail));
        logError(QString::fromLatin1(kLogCategory),
                 QStringLiteral("Reload after save failed for %1: %2").arg(target, detail));
        return false;
    }
    if (hadExistingFile)
        logInfo(QString::fromLatin1(kLogCategory),
                QStringLiteral("Backup written to %1").arg(backupPath));

    m_editors->updateTitle(editor);
    if (editor == currentEditor())
        updateFileStatus(editor);
    updateWindowTitle();
    m_lastDirectory = targetInfo.absolutePath();
    m_database.addRecent(target, QStringLiteral("file"));
    refreshRecentPanels();

    const QString message = QStringLiteral("Saved %1 (%2)")
                                .arg(targetInfo.fileName(), fmt::humanBytes(written.value()));
    statusBar()->showMessage(message, 5000);
    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("Saved %1 (%2 bytes)").arg(target).arg(written.value()));

    // The bytes changed, so refresh the format analysis for the edited file.
    startAnalysis(editor, editor->document());
    return true;
}

bool MainWindow::maybeSaveEditor(FileEditor* editor)
{
    if (editor == nullptr || !editor->isModified())
        return true;

    QMessageBox box(QMessageBox::Warning, QStringLiteral("Unsaved byte edits"),
                    QStringLiteral("%1 has unsaved byte edits.")
                        .arg(QFileInfo(editor->path()).fileName()),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.setDefaultButton(QMessageBox::Save);
    box.setInformativeText(
        QStringLiteral("Save overwrites the file after a confirmation and writes a timestamped "
                       "backup. Discard throws the edits away."));
    switch (box.exec()) {
    case QMessageBox::Save:
        return saveEditor(editor, false);
    case QMessageBox::Discard:
        return true;
    default:
        return false;
    }
}

void MainWindow::closeEditorWithConfirmation(FileEditor* editor)
{
    if (maybeSaveEditor(editor))
        m_editors->closeEditor(editor);
}

void MainWindow::exportActiveReport()
{
    FileEditor* editor = currentEditor();
    if (editor == nullptr) {
        QMessageBox::information(this, QStringLiteral("Export report"),
                                 QStringLiteral("Open a file first."));
        return;
    }
    const QString report = editor->analysisReport();
    if (report.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Export report"),
                                 QStringLiteral("No analysis is available for this file yet."));
        return;
    }
    const QString suggested =
        QDir(m_lastDirectory).filePath(QFileInfo(editor->path()).fileName() +
                                       QStringLiteral(".analysis.txt"));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export analysis report"), suggested,
        QStringLiteral("Text report (*.txt)"));
    if (path.isEmpty())
        return;

    const Status written = atomic::writeFile(path, report.toUtf8());
    if (!written) {
        QMessageBox::warning(this, QStringLiteral("Cannot export report"),
                             written.error().detail.isEmpty() ? written.error().message
                                                              : written.error().detail);
        return;
    }
    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("Report exported to %1").arg(path));
}

void MainWindow::exportLog()
{
    const QString suggested = QDir(m_lastDirectory).filePath(QStringLiteral("pkgstudio.log"));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export log"),
                                                     suggested,
                                                     QStringLiteral("Log file (*.log *.txt)"));
    if (path.isEmpty())
        return;

    QString text;
    for (const LogRecord& record : Logger::instance().snapshot()) {
        text += QStringLiteral("%1 [%2] [%3] %4\n")
                    .arg(record.timestamp.toString(Qt::ISODateWithMs),
                         Logger::levelName(record.level), record.category, record.message);
    }
    const Status written = atomic::writeFile(path, text.toUtf8());
    if (!written) {
        QMessageBox::warning(this, QStringLiteral("Cannot export log"),
                             written.error().detail.isEmpty() ? written.error().message
                                                              : written.error().detail);
        return;
    }
    logInfo(QString::fromLatin1(kLogCategory), QStringLiteral("Log exported to %1").arg(path));
}

void MainWindow::openDataDirectory()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(Settings::dataDirectory()));
}

void MainWindow::clearAnalysisCache()
{
    if (m_database.isOpen())
        m_database.clearAnalysisCache();
    logInfo(QString::fromLatin1(kLogCategory), QStringLiteral("Analysis cache cleared"));
    QMessageBox::information(this, QStringLiteral("Analysis cache"),
                             QStringLiteral("Cached analyses were cleared. Files will be "
                                            "analyzed again when opened."));
}

void MainWindow::showDiagnosticsDialog()
{
    const auto resident = sysinfo::currentResidentBytes();
    QStringList lines;
    lines << QStringLiteral("PKG Studio %1").arg(QApplication::applicationVersion());
    lines << QStringLiteral("Qt: %1 (built with %2)")
                 .arg(QString::fromLatin1(qVersion()), QString::fromLatin1(QT_VERSION_STR));
    lines << QStringLiteral("Operating system: %1").arg(sysinfo::operatingSystem());
    lines << QStringLiteral("CPU: %1 (%2 logical cores)")
                 .arg(sysinfo::cpuDescription())
                 .arg(sysinfo::logicalCpuCount());
    lines << QStringLiteral("Total RAM: %1")
                 .arg(sysinfo::totalRamBytes() > 0
                          ? fmt::humanBytes(sysinfo::totalRamBytes())
                          : QStringLiteral("unknown"));
    lines << QStringLiteral("Resident memory: %1")
                 .arg(resident ? fmt::humanBytes(resident.value())
                               : QStringLiteral("unavailable"));
    lines << QStringLiteral("Registers parsers: %1")
                 .arg(ParserRegistry::instance().parserIds().join(QStringLiteral(", ")));
    lines << QStringLiteral("Data directory: %1").arg(Settings::dataDirectory());
    lines << QStringLiteral("Log file: %1").arg(Logger::instance().logFilePath());
    lines << QStringLiteral("Database: %1 (schema v%2)")
                 .arg(m_database.path())
                 .arg(m_database.schemaVersion());
    lines << QStringLiteral("Analysis cache entries: %1").arg(m_database.analysisCacheCount());
    lines << QStringLiteral("Open documents: %1").arg(openFileCount());

    QMessageBox box(QMessageBox::Information, QStringLiteral("Diagnostics"),
                    lines.join(QLatin1Char('\n')), QMessageBox::Ok, this);
    box.setDetailedText(QStringLiteral(
        "Diagnostics are local only. PKG Studio has no telemetry and never uploads "
        "files or usage data."));
    box.exec();
}

void MainWindow::showFormatSupport()
{
    const QString text =
        QStringLiteral(
            "Formats verified by signature-matched parsers and automated tests:\n\n"
            "\u2022 ELF (32/64-bit, both byte orders): header, machine, entry point, "
            "program headers, PT_INTERP.\n"
            "\u2022 PE/COFF: DOS header, COFF header, optional header (PE32/PE32+), "
            "section table.\n"
            "\u2022 ZIP and Android APK: central directory listing, manifest presence "
            "(binary XML is not decoded).\n"
            "\u2022 PSP/PS Vita PKG: documented plain-text header fields only.\n"
            "\u2022 PS4 PKG: 0x1000 byte container header fields only.\n\n"
            "Not supported: PS5 PKG layout (unverified, not claimed), decryption, "
            "content extraction, DRM or console security bypass, and any modification "
            "of protected commercial content.\n\n"
            "Unrecognized files are shown as raw bytes with an honest notice instead "
            "of a guessed format.");
    QMessageBox::information(this, QStringLiteral("Supported formats"), text);
}

void MainWindow::showLegalNotice()
{
    const QString text =
        QStringLiteral(
            "PKG Studio is an analysis tool for files you own or are authorized to "
            "inspect and modify, such as homebrew packages, public test files and your own "
            "builds.\n\n"
            "It does not decrypt protected commercial packages, does not bypass console "
            "security or DRM, and does not extract copyrighted content. A PKG Studio "
            "licence grants no rights to third-party material, and the Code tab's byte "
            "listing is not a CPU disassembler.\n\n"
            "Copies: PKG Studio is proprietary. Distributing the application or a modified "
            "build without written permission is prohibited; unauthorized copies are "
            "pirated copies and identified cases may be reported and pursued.")
        + QStringLiteral(
              "\n\n"
              "Privacy: everything stays local. There is no telemetry, no account and no "
              "upload. The optional AI assistant sends prompts only to the endpoint you "
              "configure; use a loopback endpoint to keep files on this computer.\n\n"
              "Full texts: Help \u2192 Privacy policy and Help \u2192 Terms of use, or the "
              "LICENSE, PRIVACY.md and TERMS.md files shipped with the application.");
    QMessageBox::information(this, QStringLiteral("Legal notice and scope"), text);
}

void MainWindow::showAbout()
{
    std::unique_ptr<QMessageBox> box(createAboutDialog(this, m_samplesPath));
    box->exec();
}

void MainWindow::restoreSession()
{
    refreshRecentPanels();
    if (!m_settings.restoreSession)
        return;
    const QJsonObject session = m_database.session(QStringLiteral("main"));
    if (session.isEmpty())
        return;

    const QString project = session.value(QStringLiteral("project")).toString();
    if (!project.isEmpty() && QFileInfo(project).isDir())
        openProject(project);

    for (const QJsonValue& value : session.value(QStringLiteral("files")).toArray()) {
        const QString path = value.toString();
        if (!path.isEmpty() && QFileInfo(path).isFile())
            openFile(path);
    }
    const QString active = session.value(QStringLiteral("active")).toString();
    if (!active.isEmpty()) {
        if (auto* editor = m_editors->editorForPath(active))
            m_editors->setCurrentWidget(editor);
    }
    logInfo(QString::fromLatin1(kLogCategory), QStringLiteral("Session restored"));
}

void MainWindow::saveSession()
{
    QJsonArray files;
    QString active;
    for (int i = 0; i < m_editors->count(); ++i) {
        if (FileEditor* editor = m_editors->editorAt(i)) {
            files.append(editor->path());
            if (editor == m_editors->currentEditor())
                active = editor->path();
        }
    }
    QJsonObject session;
    session.insert(QStringLiteral("project"), m_projectPath);
    session.insert(QStringLiteral("files"), files);
    session.insert(QStringLiteral("active"), active);
    m_database.saveSession(QStringLiteral("main"), session);
    m_settings.lastProject = m_projectPath;
    Settings::instance().setValues(m_settings);
}

void MainWindow::paintEvent(QPaintEvent* event)
{
    QMainWindow::paintEvent(event);

    // A theme wallpaper is painted behind everything; panel colours are
    // translucent (see ThemeManager::styleSheet) so the image shows through.
    const Theme& theme = ThemeManager::instance().current();
    if (theme.backgroundImage.isEmpty()) {
        // No wallpaper: a soft accent bloom in the top-left corner fading into
        // a darker far edge. The same translucent panels then read as glass
        // instead of as flat boxes.
        const QColor window = theme.color(QStringLiteral("window"));
        const QColor accent = theme.color(QStringLiteral("accent"));
        const QColor glow = blendColours(window, accent, theme.dark ? 0.16 : 0.18);
        const QColor deep = blendColours(window,
                                         theme.dark ? QColor(0, 0, 0) : QColor(255, 255, 255),
                                         theme.dark ? 0.50 : 0.60);
        QRadialGradient gradient(
            QPointF(width() * 0.15, 0.0),
            static_cast<qreal>(std::max(width(), height())) * 1.25);
        gradient.setColorAt(0.0, glow);
        gradient.setColorAt(0.45, window);
        gradient.setColorAt(1.0, deep);
        QPainter painter(this);
        painter.fillRect(rect(), gradient);
        return;
    }
    if (m_wallpaperPath != theme.backgroundImage) {
        m_wallpaperPath = theme.backgroundImage;
        m_wallpaper = QPixmap(theme.backgroundImage);
    }
    if (m_wallpaper.isNull())
        return;

    QPainter painter(this);
    const QPixmap scaled = m_wallpaper.scaled(
        size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    painter.drawPixmap((width() - scaled.width()) / 2, (height() - scaled.height()) / 2,
                       scaled);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    for (int i = 0; i < m_editors->count(); ++i) {
        FileEditor* editor = m_editors->editorAt(i);
        if (editor != nullptr && !maybeSaveEditor(editor)) {
            event->ignore();
            return;
        }
    }
    saveSession();
    logInfo(QString::fromLatin1(kLogCategory), QStringLiteral("Shutting down"));
    m_memoryTimer->stop();
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty())
        return;
    const QString path = urls.first().toLocalFile();
    if (path.isEmpty())
        return;
    if (QFileInfo(path).isDir())
        openProject(path);
    else
        openFile(path);
    event->acceptProposedAction();
}

}  // namespace pkg::ui
