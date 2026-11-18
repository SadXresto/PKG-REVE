#pragma once

#include "core/Result.h"
#include "core/Structure.h"

#include <QVector>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace pkg::ui {

/// Form-based editor for structure templates: fields can be added, edited,
/// removed and reordered (including nested structs) without writing JSON.
/// The JSON schema stays the single definition format; this widget serialises
/// to it and validates through StructureTemplate::fromJson, so a form-authored
/// layout is exactly as trustworthy as a hand-written one: a layout, not
/// format detection.
class StructureFormEditor : public QWidget {
    Q_OBJECT

public:
    explicit StructureFormEditor(QWidget* parent = nullptr);

    /// Loads an existing template without emitting changed().
    void setTemplate(const StructureTemplate& structure);
    void clear();
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] QVector<StructureField> fields() const { return m_fields; }
    [[nodiscard]] StructureTemplate structure() const;
    /// Schema/bounds-independent validation: serialises the form and runs the
    /// same parser the JSON editor uses. Returns actionable error detail.
    [[nodiscard]] Result<StructureTemplate> buildTemplate() const;
    /// Number of fields including nested ones.
    [[nodiscard]] int fieldCount() const;

    /// Index-path operations ({} is the root list, {0} the first root field,
    /// {0,1} its second child). They rebuild the tree and emit changed().
    bool appendField(const StructureField& field, const QVector<int>& parent = {});
    bool replaceField(const QVector<int>& path, const StructureField& field);
    bool removeField(const QVector<int>& path);
    bool moveField(const QVector<int>& path, int delta);

signals:
    void changed();

private:
    void rebuild(const QVector<int>& selectPath = {});
    void addItems(QTreeWidgetItem* parent, const QVector<StructureField>& fields,
                  const QVector<int>& prefix);
    [[nodiscard]] QVector<int> currentPath() const;
    [[nodiscard]] StructureField* fieldAt(const QVector<int>& path);
    [[nodiscard]] QVector<StructureField>* siblingList(const QVector<int>& parentPath);

    void addField(bool nested);
    void editField();
    void removeSelectedField();
    void moveSelected(int delta);
    void updateButtons();

    QVector<StructureField> m_fields;

    QLineEdit* m_name = nullptr;
    QLineEdit* m_description = nullptr;
    QComboBox* m_endianness = nullptr;
    QTreeWidget* m_tree = nullptr;
    QPushButton* m_nestedButton = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_upButton = nullptr;
    QPushButton* m_downButton = nullptr;
};

}  // namespace pkg::ui
