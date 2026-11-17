#pragma once

#include "core/Structure.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace pkg::ui {

/// Declarative binary structure view. Templates are user-authored layouts, so
/// the panel always labels its output as an interpretation rather than a
/// confirmed format, and shows validation/bounds errors instead of guessing.
class StructurePanel : public QWidget {
    Q_OBJECT

public:
    explicit StructurePanel(QWidget* parent = nullptr);

    /// Non-owning; the document must outlive the panel (they share an editor).
    void setDocument(HexDocument* document);
    void refresh();

    void setTemplate(const StructureTemplate& structure, const QString& origin);
    /// Selects a loaded template by id; false when it is not available.
    bool selectTemplateById(const QString& id);
    [[nodiscard]] bool hasTemplate() const { return m_hasTemplate; }
    [[nodiscard]] QString templateName() const { return m_hasTemplate ? m_structure.name : QString(); }
    [[nodiscard]] const StructureTemplate& currentTemplate() const { return m_structure; }

    /// Selects the deepest field containing `offset` without emitting
    /// fieldSelected(), so hex -> tree sync cannot loop.
    bool highlightOffset(quint64 offset);

    void editTemplate();
    void loadTemplateFile();
    void openTemplatesDirectory();

signals:
    /// A tree selection that should be selected in the hex view.
    void fieldSelected(quint64 offset, quint64 length);
    /// Emitted when a template changes so the window can log/status it.
    void templateChanged(const QString& name);

private:
    struct TemplateEntry {
        StructureTemplate structure;
        QString origin;
    };

    void reloadTemplates(const QString& selectId = {});
    void rebuildCombo(const QString& selectId);
    void applyIndex(int index);
    void evaluateCurrent();
    void populateTree();
    void clearTree();
    void updateDiagnostics(const StructureEvaluation& evaluation);
    void updateEmptyState();
    [[nodiscard]] QString templatesDirectory() const;

    HexDocument* m_document = nullptr;
    QVector<TemplateEntry> m_entries;
    StructureTemplate m_structure;
    StructureEvaluation m_evaluation;
    bool m_hasTemplate = false;
    bool m_hasEvaluation = false;

    QComboBox* m_templateBox = nullptr;
    QLabel* m_scopeLabel = nullptr;
    QLabel* m_coverageLabel = nullptr;
    QLabel* m_disclaimer = nullptr;
    QTreeWidget* m_tree = nullptr;
    QPlainTextEdit* m_diagnostics = nullptr;
};

}  // namespace pkg::ui
