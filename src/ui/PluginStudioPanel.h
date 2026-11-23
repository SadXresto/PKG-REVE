// Extension Studio: edit, validate and package declarative plugin manifests.
// Plugin code is never loaded or executed by this panel.
#pragma once

#include "core/plugins/PluginManifest.h"

#include <QHash>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;

namespace pkg::ui {

class PluginStudioPanel : public QWidget {
    Q_OBJECT

public:
    explicit PluginStudioPanel(QWidget* parent = nullptr);

    /// Reloads installed manifests from the data directory.
    void refresh();
    /// Directory that holds user plugins.
    [[nodiscard]] static QString pluginsDirectory();
    /// Writes the example plugin when it does not exist and selects it.
    void createExample();

private:
    void buildUi();
    void loadInstalled();
    void loadSelected();
    void clearForm();
    void fillForm(const plugins::PluginManifest& manifest);
    [[nodiscard]] plugins::PluginManifest manifestFromForm() const;
    [[nodiscard]] QString selectedDirectory() const;
    void saveCurrent();
    void validateCurrent();
    void exportCurrent();
    void showValidation(const plugins::ValidationReport& report);
    void setStatus(const QString& message);
    void showEmptyState(bool empty);

    QStackedWidget* m_pages = nullptr;
    QLabel* m_emptyDetail = nullptr;
    QListWidget* m_pluginList = nullptr;
    QTabWidget* m_tabs = nullptr;
    QLineEdit* m_id = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_version = nullptr;
    QLineEdit* m_apiVersion = nullptr;
    QLineEdit* m_author = nullptr;
    QLineEdit* m_entry = nullptr;
    QLineEdit* m_description = nullptr;
    QLineEdit* m_capabilities = nullptr;
    QPlainTextEdit* m_dependencies = nullptr;
    QVector<QCheckBox*> m_permissionBoxes;
    QPlainTextEdit* m_validation = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_save = nullptr;
    QPushButton* m_validate = nullptr;
    QPushButton* m_export = nullptr;

    QVector<plugins::PluginManifest> m_installed;
    QString m_currentDirectory;
};

}  // namespace pkg::ui
