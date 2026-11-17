#pragma once

#include "core/FormatInfo.h"
#include "core/HexDocument.h"
#include "core/Result.h"
#include "data/Database.h"
#include "data/Settings.h"

#include <QFutureWatcher>
#include <QHash>
#include <QMainWindow>
#include <QPointer>

#include <functional>
#include <memory>

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QMenu;
class QProgressBar;
class QStackedWidget;
class QSplitter;
class QTimer;

namespace pkg::ui {

class ActivityBar;
class EditorArea;
class FileEditor;
class FileExplorer;
class OutputPanel;
class RecentPanel;
class SearchPanel;
class WelcomePage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Database& database, QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString& path);
    /// Shows the analysis tab of the active editor. Used by the screenshot
    /// diagnostics mode so documentation captures the real analysis view.
    bool showAnalysisTabForCurrentEditor();
    /// Diagnostic/screenshot helper for the live byte inspector.
    bool showByteInspectorForCurrentEditor();
    /// Diagnostic/screenshot helper: opens the structure builder with the
    /// built-in example that matches the current analysis kind.
    bool showStructureForCurrentEditor();
    void openProject(const QString& path);
    void showWelcome();
    void showSettings();
    void showCommandPalette();
    void showThemeEditor();

    void restoreSession();
    void saveSession();

    [[nodiscard]] QString projectPath() const { return m_projectPath; }
    [[nodiscard]] int openFileCount() const;
    [[nodiscard]] bool allEditorsAnalyzed() const;
    [[nodiscard]] int activeAnalysisCount() const { return m_activeAnalyses; }
    [[nodiscard]] QStringList openPaths() const;

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void buildUi();
    void buildCommands();
    void buildMenus();
    void buildStatusBar();

    QAction* addCommand(const QString& id, const QString& title, const QString& category,
                        const QKeySequence& defaultShortcut, std::function<void()> handler);
    void refreshShortcuts();
    void recordRecentCommand(const QString& id);
    void showHelpTopic(const QString& id);
    void runCommand(const QString& id);
    [[nodiscard]] QList<QString> commandIds() const { return m_commandOrder; }

    void switchSidebar(const QString& id);
    void refreshRecentPanels();
    void setProjectPath(const QString& path);
    void updateWindowTitle();
    void updateFileStatus(FileEditor* editor);
    void updateCursorStatus(FileEditor* editor, quint64 offset, quint64 selectionLength);
    void updateMemoryStatus();
    void applyTheme();
    void rebuildThemeMenu();
    void applyEditorPreferences();
    void createProject();
    void openProjectDialog();
    void exportActiveReport();
    void exportLog();
    void saveActiveEditor(bool saveAs);
    bool saveEditor(FileEditor* editor, bool saveAs);
    bool maybeSaveEditor(FileEditor* editor);
    void closeEditorWithConfirmation(FileEditor* editor);
    void updateModeStatus();
    [[nodiscard]] QString backupPathFor(const QString& path) const;
    void openDataDirectory();
    void showAbout();
    void showLegalNotice();
    void showFormatSupport();
    void showDiagnosticsDialog();
    void clearAnalysisCache();

    void startAnalysis(FileEditor* editor, std::shared_ptr<HexDocument> document);
    void onAnalysisFinished(FileEditor* editor);
    void setBusy(bool busy, const QString& message);

    [[nodiscard]] FileEditor* currentEditor() const;
    [[nodiscard]] QFont editorFont() const;
    [[nodiscard]] HexLayout hexLayout() const;

    Database& m_database;
    AppSettings m_settings;

    ActivityBar* m_activity = nullptr;
    QStackedWidget* m_sideStack = nullptr;
    FileExplorer* m_explorer = nullptr;
    SearchPanel* m_searchPanel = nullptr;
    RecentPanel* m_recentPanel = nullptr;

    QSplitter* m_mainSplitter = nullptr;
    QSplitter* m_verticalSplitter = nullptr;
    EditorArea* m_editors = nullptr;
    OutputPanel* m_output = nullptr;
    WelcomePage* m_welcome = nullptr;

    QLabel* m_statusFile = nullptr;
    QLabel* m_statusFormat = nullptr;
    QLabel* m_statusOffset = nullptr;
    QLabel* m_statusSelection = nullptr;
    QLabel* m_statusMemory = nullptr;
    QLabel* m_statusMode = nullptr;
    QLabel* m_statusTheme = nullptr;
    QProgressBar* m_busyBar = nullptr;
    QTimer* m_memoryTimer = nullptr;

    QHash<QString, QAction*> m_commands;
    QHash<QString, QKeySequence> m_defaultShortcuts;
    QHash<QString, QString> m_commandCategories;
    QHash<QString, QString> m_commandDescriptions;
    QHash<QString, QString> m_commandKeywords;
    QList<QString> m_commandOrder;
    QList<QString> m_recentCommands;
    QMenu* m_themeMenu = nullptr;

    QHash<FileEditor*, std::shared_ptr<QFutureWatcher<Result<PackageInfo>>>> m_analyses;
    int m_activeAnalyses = 0;

    QString m_projectPath;
    QString m_lastDirectory;
};

}  // namespace pkg::ui
