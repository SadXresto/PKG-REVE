#pragma once

#include "data/Settings.h"

#include <QDialog>
#include <QHash>
#include <QKeySequence>
#include <QList>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QKeySequenceEdit;

namespace pkg::ui {

/// Searchable settings center. Every category is built from real, persisted
/// preferences; informational pages never pretend to offer controls that do
/// not exist.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    struct ActionInfo {
        QString id;
        QString title;
        QString category;
        QKeySequence defaultShortcut;
    };

    SettingsDialog(const AppSettings& values, QList<ActionInfo> actions,
                   QWidget* parent = nullptr);
    ~SettingsDialog() override;

    [[nodiscard]] AppSettings values() const;
    [[nodiscard]] QHash<QString, QKeySequence> shortcutOverrides() const;
    [[nodiscard]] bool shortcutsWereReset() const { return m_shortcutsReset; }

protected:
    void reject() override;

private:
    struct Row {
        QWidget* widget = nullptr;
        QString searchText;
    };
    struct Page {
        QString title;
        QWidget* widget = nullptr;
        QList<Row> rows;
    };

    void buildCenter();
    QWidget* createPage(const QString& title);
    void addRow(QWidget* page, QWidget* row, const QString& searchText);
    QWidget* makeSettingRow(const QString& title, const QString& description,
                            QWidget* control, QWidget* parent,
                            const std::function<void()>& reset);
    QWidget* makeInfoCard(const QString& title, const QString& body, QWidget* parent);

    void buildAppearancePage(QWidget* page);
    void buildEditorPage(QWidget* page);
    void buildHexPage(QWidget* page);
    void buildGameForgePage(QWidget* page);
    void buildAiPage(QWidget* page);
    void buildPerformancePage(QWidget* page);
    void buildShortcutsPage(QWidget* page);
    void buildPluginsPage(QWidget* page);
    void buildPrivacyPage(QWidget* page);
    void buildUpdatesPage(QWidget* page);

    void refilter(const QString& text);
    void applyThemePreview();
    void restoreThemePreview();
    void resetCategory();
    void resetAll();
    void resetShortcutsToDefaults();

    AppSettings m_values;
    QList<ActionInfo> m_actions;
    QHash<QString, QKeySequenceEdit*> m_shortcutEdits;
    QList<Page> m_pages;
    QList<QTreeWidgetItem*> m_shortcutItems;
    bool m_shortcutsReset = false;
    QString m_originalThemeId;

    QLineEdit* m_search = nullptr;
    QListWidget* m_categories = nullptr;
    QStackedWidget* m_stack = nullptr;
    QTreeWidget* m_shortcutTree = nullptr;
    QComboBox* m_theme = nullptr;
    QLabel* m_themeDescription = nullptr;
    QWidget* m_themeSwatches = nullptr;
    QComboBox* m_fontFamily = nullptr;
    QSpinBox* m_fontSize = nullptr;
    QCheckBox* m_lightweight = nullptr;
    QCheckBox* m_restoreSession = nullptr;
    QCheckBox* m_confirmWrites = nullptr;
    QSpinBox* m_recentLimit = nullptr;
    QComboBox* m_bytesPerRow = nullptr;
    QSpinBox* m_groupSize = nullptr;
    QSpinBox* m_workerThreads = nullptr;
    QSpinBox* m_cacheLimit = nullptr;
    QComboBox* m_logLevel = nullptr;
    QCheckBox* m_fileLogging = nullptr;
    int m_currentPage = 0;
};

}  // namespace pkg::ui
