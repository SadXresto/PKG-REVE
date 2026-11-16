#include "ui/EditorArea.h"

#include "ui/FileEditor.h"

#include <QFileInfo>
#include <QTabBar>

namespace pkg::ui {

EditorArea::EditorArea(QWidget* parent) : QTabWidget(parent)
{
    setObjectName(QStringLiteral("editorTabs"));
    setDocumentMode(true);
    setMovable(true);
    setTabsClosable(true);

    connect(this, &QTabWidget::currentChanged, this, [this](int) {
        emit currentEditorChanged(currentEditor());
    });
    connect(this, &QTabWidget::tabCloseRequested, this, [this](int index) {
        auto* editor = qobject_cast<FileEditor*>(widget(index));
        if (editor == nullptr)
            return;  // welcome tab is not closable
        emit editorCloseRequested(editor);
    });
}

void EditorArea::addWelcome(QWidget* page)
{
    if (m_welcomeIndex >= 0) {
        setCurrentIndex(m_welcomeIndex);
        return;
    }
    m_welcomeIndex = addTab(page, QStringLiteral("Welcome"));
    tabBar()->setTabButton(m_welcomeIndex, QTabBar::RightSide, nullptr);
    setTabToolTip(m_welcomeIndex, QStringLiteral("Project overview and recent files"));
    setCurrentIndex(m_welcomeIndex);
}

void EditorArea::addEditor(FileEditor* editor)
{
    if (editor == nullptr)
        return;
    const int index = addTab(editor, QString());
    updateTitle(editor);
    setCurrentIndex(index);
    editor->setFocus();
}

FileEditor* EditorArea::currentEditor() const
{
    return qobject_cast<FileEditor*>(currentWidget());
}

FileEditor* EditorArea::editorAt(int index) const
{
    if (index < 0 || index >= count())
        return nullptr;
    return qobject_cast<FileEditor*>(widget(index));
}

FileEditor* EditorArea::editorForPath(const QString& path) const
{
    for (int i = 0; i < count(); ++i) {
        auto* editor = editorAt(i);
        if (editor != nullptr && editor->path() == path)
            return editor;
    }
    return nullptr;
}

void EditorArea::closeEditor(FileEditor* editor)
{
    const int index = indexOf(editor);
    if (index < 0)
        return;
    removeTab(index);
    emit editorClosed(editor);
    editor->deleteLater();
    if (count() == 0 && m_welcomeIndex < 0)
        emit currentEditorChanged(nullptr);
}

void EditorArea::closeAllEditors()
{
    while (count() > 0) {
        QWidget* page = widget(0);
        auto* editor = qobject_cast<FileEditor*>(page);
        removeTab(0);
        if (editor != nullptr)
            editor->deleteLater();
    }
    m_welcomeIndex = -1;
}

void EditorArea::updateTitle(FileEditor* editor)
{
    const int index = indexOf(editor);
    if (index < 0)
        return;
    const QString name = QFileInfo(editor->path()).fileName();
    setTabText(index, editor->isModified() ? name + QStringLiteral(" \u25CF") : name);
    setTabToolTip(index, editor->path());
}

}  // namespace pkg::ui
