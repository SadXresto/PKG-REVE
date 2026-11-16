#pragma once

#include "data/Settings.h"

#include <QDialog>
#include <QHash>
#include <QKeySequence>
#include <QList>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QSpinBox;
class QTreeWidget;

namespace pkg::ui {

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

    [[nodiscard]] AppSettings values() const;
    [[nodiscard]] QHash<QString, QKeySequence> shortcutOverrides() const;
    [[nodiscard]] bool shortcutsWereReset() const { return m_shortcutsReset; }

private:
    void buildGeneralTab(QWidget* tab);
    void buildHexTab(QWidget* tab);
    void buildPerformanceTab(QWidget* tab);
    void buildLoggingTab(QWidget* tab);
    void buildShortcutsTab(QWidget* tab);
    void resetShortcutsToDefaults();

    AppSettings m_values;
    QList<ActionInfo> m_actions;
    QHash<QString, QKeySequenceEdit*> m_shortcutEdits;
    bool m_shortcutsReset = false;

    QComboBox* m_theme = nullptr;
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
};

}  // namespace pkg::ui
