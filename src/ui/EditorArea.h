#pragma once

#include <QTabWidget>

namespace pkg::ui {

class FileEditor;

/// Tabbed document area. Holds the welcome tab and one tab per open file.
class EditorArea : public QTabWidget {
    Q_OBJECT

public:
    explicit EditorArea(QWidget* parent = nullptr);

    void addWelcome(QWidget* page);
    void addEditor(FileEditor* editor);
    [[nodiscard]] FileEditor* currentEditor() const;
    [[nodiscard]] FileEditor* editorForPath(const QString& path) const;
    [[nodiscard]] FileEditor* editorAt(int index) const;
    void closeAllEditors();
    /// Removes the tab. Call only after unsaved changes were handled.
    void closeEditor(FileEditor* editor);
    void updateTitle(FileEditor* editor);

signals:
    void currentEditorChanged(FileEditor* editor);
    void editorClosed(FileEditor* editor);
    /// A tab close was requested; the window decides whether to save first.
    void editorCloseRequested(FileEditor* editor);

private:
    int m_welcomeIndex = -1;
};

}  // namespace pkg::ui
