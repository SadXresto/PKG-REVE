#pragma once

#include "data/Theme.h"

#include <QDialog>
#include <QHash>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace pkg::ui {

/// Edits a copy of a theme; the caller decides whether to save it.
class ThemeEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit ThemeEditorDialog(const Theme& base, QWidget* parent = nullptr);

    [[nodiscard]] Theme theme() const { return m_theme; }

private:
    void pickColor(const QString& role);
    void refreshSwatch(const QString& role);

    Theme m_theme;
    QHash<QString, QPushButton*> m_swatches;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_idEdit = nullptr;
    QCheckBox* m_darkBox = nullptr;
    QLineEdit* m_backgroundEdit = nullptr;
    QSpinBox* m_opacity = nullptr;
    QLabel* m_error = nullptr;
};

}  // namespace pkg::ui
