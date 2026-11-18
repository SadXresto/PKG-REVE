#include "ui/StructureFormEditor.h"

#include "core/SafeMath.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>

namespace pkg::ui {
namespace {

constexpr quint64 kMaxTextFieldLength = 1024U * 1024U;
constexpr int kMaxArrayElements = 4096;

QString hexOffset(quint64 offset)
{
    return QStringLiteral("0x") + QString::number(offset, 16).toUpper();
}

/// Byte span of one field as the evaluator computes it (excluding the field's
/// own offset). nullopt when the size depends on data that the form cannot see
/// or when the arithmetic overflows.
std::optional<quint64> fieldByteSize(const StructureField& field)
{
    quint64 element = 0;
    if (field.type == StructureFieldType::Struct) {
        std::optional<quint64> span;
        for (const StructureField& child : field.fields) {
            const auto size = fieldByteSize(child);
            if (!size)
                return std::nullopt;
            const auto end = safe::add(child.offset, *size);
            if (!end)
                return std::nullopt;
            span = span ? std::max(*span, *end) : *end;
        }
        if (!span)
            return std::nullopt;
        element = *span;
    } else if (const auto primitive = structurePrimitiveSize(field.type)) {
        element = *primitive;
    } else {
        element = field.length;
    }
    return safe::mul(element, static_cast<quint64>(std::max(1, field.count)));
}

quint64 nextFreeOffset(const QVector<StructureField>& siblings)
{
    quint64 end = 0;
    for (const StructureField& field : siblings) {
        const auto size = fieldByteSize(field);
        if (!size)
            continue;
        if (const auto fieldEnd = safe::add(field.offset, *size))
            end = std::max(end, *fieldEnd);
    }
    return end;
}

bool siblingNameTaken(const QVector<StructureField>& siblings, const QString& name, int ignoreIndex)
{
    for (int i = 0; i < siblings.size(); ++i) {
        if (i == ignoreIndex)
            continue;
        if (siblings.at(i).name.trimmed().compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString uniqueFieldName(const QVector<StructureField>& siblings, const QString& base)
{
    QString name = base;
    int suffix = 2;
    while (siblingNameTaken(siblings, name, -1))
        name = base + QLatin1Char('_') + QString::number(suffix++);
    return name;
}

int countFields(const QVector<StructureField>& fields)
{
    int total = fields.size();
    for (const StructureField& field : fields)
        total += countFields(field.fields);
    return total;
}

QString typeLabel(StructureFieldType type)
{
    switch (type) {
    case StructureFieldType::UInt8: return QStringLiteral("u8 — unsigned 8-bit");
    case StructureFieldType::Int8: return QStringLiteral("i8 — signed 8-bit");
    case StructureFieldType::UInt16: return QStringLiteral("u16 — unsigned 16-bit");
    case StructureFieldType::Int16: return QStringLiteral("i16 — signed 16-bit");
    case StructureFieldType::UInt32: return QStringLiteral("u32 — unsigned 32-bit");
    case StructureFieldType::Int32: return QStringLiteral("i32 — signed 32-bit");
    case StructureFieldType::UInt64: return QStringLiteral("u64 — unsigned 64-bit");
    case StructureFieldType::Int64: return QStringLiteral("i64 — signed 64-bit");
    case StructureFieldType::Float32: return QStringLiteral("f32 — 32-bit float");
    case StructureFieldType::Float64: return QStringLiteral("f64 — 64-bit float");
    case StructureFieldType::Ascii: return QStringLiteral("ascii — text");
    case StructureFieldType::Utf8: return QStringLiteral("utf8 — Unicode text");
    case StructureFieldType::Bytes: return QStringLiteral("bytes — raw bytes");
    case StructureFieldType::Struct: return QStringLiteral("struct — nested fields");
    }
    return QStringLiteral("u8 — unsigned 8-bit");
}

/// Edits one field. It only collects values that the schema can represent;
/// cross-field rules (duplicate sibling names, struct children) are enforced by
/// StructureFormEditor once the dialog is accepted.
class StructureFieldDialog : public QDialog {
public:
    StructureFieldDialog(QWidget* parent, const StructureField& initial, const QString& title)
        : QDialog(parent), m_field(initial)
    {
        setWindowTitle(title);
        setModal(true);
        setMinimumWidth(440);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(8);

        auto* form = new QFormLayout();
        form->setLabelAlignment(Qt::AlignRight);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        m_name = new QLineEdit(this);
        m_name->setObjectName(QStringLiteral("structureFieldName"));
        m_name->setText(initial.name);
        m_name->setPlaceholderText(QStringLiteral("magic"));
        form->addRow(QStringLiteral("Name"), m_name);

        m_type = new QComboBox(this);
        m_type->setObjectName(QStringLiteral("structureFieldType"));
        for (const StructureFieldType type :
             {StructureFieldType::UInt8, StructureFieldType::Int8, StructureFieldType::UInt16,
              StructureFieldType::Int16, StructureFieldType::UInt32, StructureFieldType::Int32,
              StructureFieldType::UInt64, StructureFieldType::Int64, StructureFieldType::Float32,
              StructureFieldType::Float64, StructureFieldType::Ascii, StructureFieldType::Utf8,
              StructureFieldType::Bytes, StructureFieldType::Struct}) {
            m_type->addItem(typeLabel(type), static_cast<int>(type));
        }
        const int typeIndex = m_type->findData(static_cast<int>(initial.type));
        m_type->setCurrentIndex(typeIndex < 0 ? 0 : typeIndex);
        form->addRow(QStringLiteral("Type"), m_type);

        m_offset = new QLineEdit(this);
        m_offset->setObjectName(QStringLiteral("structureFieldOffset"));
        m_offset->setText(QString::number(initial.offset));
        m_offset->setToolTip(QStringLiteral("Relative to the file start, or to the parent struct "
                                            "for nested fields. Decimal or 0x hexadecimal."));
        form->addRow(QStringLiteral("Offset"), m_offset);

        m_length = new QLineEdit(this);
        m_length->setObjectName(QStringLiteral("structureFieldLength"));
        m_length->setText(QString::number(std::max<quint64>(1, initial.length)));
        m_length->setToolTip(QStringLiteral("Byte length of one element. Text and bytes fields "
                                            "need this; fixed-width types ignore it."));
        form->addRow(QStringLiteral("Length (bytes)"), m_length);

        m_endianness = new QComboBox(this);
        m_endianness->setObjectName(QStringLiteral("structureFieldEndianness"));
        m_endianness->addItem(QStringLiteral("Inherit from template"), -1);
        m_endianness->addItem(QStringLiteral("Little-endian"), 1);
        m_endianness->addItem(QStringLiteral("Big-endian"), 0);
        int endianIndex = 0;
        if (initial.littleEndian)
            endianIndex = *initial.littleEndian ? 1 : 2;
        m_endianness->setCurrentIndex(endianIndex);
        form->addRow(QStringLiteral("Byte order"), m_endianness);

        m_count = new QSpinBox(this);
        m_count->setObjectName(QStringLiteral("structureFieldCount"));
        m_count->setRange(1, kMaxArrayElements);
        m_count->setValue(std::clamp(initial.count, 1, kMaxArrayElements));
        m_count->setToolTip(QStringLiteral("Repeat this field as an array. A struct repeats its "
                                           "whole nested layout per element."));
        form->addRow(QStringLiteral("Count"), m_count);

        m_description = new QLineEdit(this);
        m_description->setObjectName(QStringLiteral("structureFieldDescription"));
        m_description->setText(initial.description);
        m_description->setPlaceholderText(QStringLiteral("Shown as a tooltip in the tree"));
        form->addRow(QStringLiteral("Description"), m_description);

        layout->addLayout(form);

        m_error = new QLabel(this);
        m_error->setObjectName(QStringLiteral("hintLabel"));
        m_error->setWordWrap(true);
        layout->addWidget(m_error);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() { accept(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);

        connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this]() { updateTypeUi(); });
        updateTypeUi();
        m_name->setFocus();
        m_name->selectAll();
    }

    [[nodiscard]] StructureField field() const { return m_field; }

protected:
    void accept() override
    {
        const QString name = m_name->text().trimmed();
        if (name.isEmpty()) {
            fail(QStringLiteral("Every field needs a name."));
            return;
        }
        const auto offset = safe::parseOffset(m_offset->text());
        if (!offset) {
            fail(QStringLiteral("Offset must be a non-negative number or a 0x hexadecimal value."));
            return;
        }

        const StructureFieldType type = selectedType();
        const bool isText = type == StructureFieldType::Ascii ||
                            type == StructureFieldType::Utf8 ||
                            type == StructureFieldType::Bytes;
        quint64 length = 0;
        if (isText) {
            const auto parsedLength = safe::parseOffset(m_length->text());
            if (!parsedLength || *parsedLength == 0 || *parsedLength > kMaxTextFieldLength) {
                fail(QStringLiteral("Length must be between 1 and %1 bytes.")
                         .arg(kMaxTextFieldLength));
                return;
            }
            length = *parsedLength;
        }

        StructureField field = m_field;
        field.name = name;
        field.type = type;
        field.offset = *offset;
        field.length = length;
        field.count = m_count->value();
        field.description = m_description->text().trimmed();
        const int endian = m_endianness->currentData().toInt();
        field.littleEndian = endian < 0 ? std::nullopt : std::optional<bool>(endian == 1);
        if (type != StructureFieldType::Struct)
            field.fields.clear();
        m_field = field;
        QDialog::accept();
    }

private:
    [[nodiscard]] StructureFieldType selectedType() const
    {
        return static_cast<StructureFieldType>(m_type->currentData().toInt());
    }

    void updateTypeUi()
    {
        const StructureFieldType type = selectedType();
        const bool isText = type == StructureFieldType::Ascii ||
                            type == StructureFieldType::Utf8 ||
                            type == StructureFieldType::Bytes;
        m_length->setEnabled(isText);
        if (isText) {
            const QString current = m_length->text().trimmed();
            if (current.isEmpty() || current == QStringLiteral("0"))
                m_length->setText(QStringLiteral("1"));
        }
        const auto primitive = structurePrimitiveSize(type);
        m_endianness->setEnabled(primitive.has_value() && *primitive > 1);
    }

    void fail(const QString& message)
    {
        m_error->setText(message);
    }

    StructureField m_field;
    QLineEdit* m_name = nullptr;
    QComboBox* m_type = nullptr;
    QLineEdit* m_offset = nullptr;
    QLineEdit* m_length = nullptr;
    QComboBox* m_endianness = nullptr;
    QSpinBox* m_count = nullptr;
    QLineEdit* m_description = nullptr;
    QLabel* m_error = nullptr;
};

}  // namespace

StructureFormEditor::StructureFormEditor(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("structureFormEditor"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* hint = new QLabel(
        QStringLiteral("Describe the layout one field at a time. Offsets are relative to the file "
                       "start, or to the parent struct for nested fields. To group fields, add a "
                       "struct, select it and choose Add nested field."),
        this);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* metadata = new QFormLayout();
    metadata->setLabelAlignment(Qt::AlignRight);
    metadata->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("structureFormName"));
    m_name->setPlaceholderText(QStringLiteral("Example header"));
    m_name->setToolTip(QStringLiteral("Shown in the template picker and saved as the default id."));
    metadata->addRow(QStringLiteral("Template name"), m_name);

    m_description = new QLineEdit(this);
    m_description->setObjectName(QStringLiteral("structureFormDescription"));
    m_description->setPlaceholderText(QStringLiteral("What does this layout describe? (optional)"));
    metadata->addRow(QStringLiteral("Description"), m_description);

    m_endianness = new QComboBox(this);
    m_endianness->setObjectName(QStringLiteral("structureFormEndianness"));
    m_endianness->addItem(QStringLiteral("Little-endian (Intel, most packages)"), true);
    m_endianness->addItem(QStringLiteral("Big-endian (network, PS Vita/PKG headers)"), false);
    m_endianness->setToolTip(QStringLiteral("Default for numeric fields. Each field can override "
                                            "it."));
    metadata->addRow(QStringLiteral("Default byte order"), m_endianness);
    layout->addLayout(metadata);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("structureFormTree"));
    m_tree->setColumnCount(6);
    m_tree->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Type"),
                             QStringLiteral("Offset"), QStringLiteral("Length"),
                             QStringLiteral("Byte order"), QStringLiteral("Count")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(4, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(5, QHeaderView::Interactive);
    m_tree->setColumnWidth(1, 72);
    m_tree->setColumnWidth(2, 72);
    m_tree->setColumnWidth(3, 64);
    m_tree->setColumnWidth(4, 84);
    m_tree->setColumnWidth(5, 56);
    m_tree->setToolTip(QStringLiteral("Fields appear in schema order. Lengths are the full "
                                      "element span; offsets are relative to the parent."));
    layout->addWidget(m_tree, 1);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);

    auto* addButton = new QPushButton(QStringLiteral("Add field…"), this);
    addButton->setObjectName(QStringLiteral("structureAddField"));
    addButton->setToolTip(QStringLiteral("Add a field next to the selection, or at the top level "
                                         "when nothing is selected."));
    buttons->addWidget(addButton);

    m_nestedButton = new QPushButton(QStringLiteral("Add nested field…"), this);
    m_nestedButton->setObjectName(QStringLiteral("structureAddNestedField"));
    m_nestedButton->setToolTip(QStringLiteral("Add a field inside the selected struct."));
    buttons->addWidget(m_nestedButton);

    m_editButton = new QPushButton(QStringLiteral("Edit…"), this);
    m_editButton->setObjectName(QStringLiteral("structureEditField"));
    buttons->addWidget(m_editButton);

    m_removeButton = new QPushButton(QStringLiteral("Remove"), this);
    m_removeButton->setObjectName(QStringLiteral("structureRemoveField"));
    buttons->addWidget(m_removeButton);

    m_upButton = new QPushButton(QStringLiteral("Move up"), this);
    m_upButton->setObjectName(QStringLiteral("structureMoveFieldUp"));
    buttons->addWidget(m_upButton);

    m_downButton = new QPushButton(QStringLiteral("Move down"), this);
    m_downButton->setObjectName(QStringLiteral("structureMoveFieldDown"));
    buttons->addWidget(m_downButton);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(addButton, &QPushButton::clicked, this, [this]() { addField(false); });
    connect(m_nestedButton, &QPushButton::clicked, this, [this]() { addField(true); });
    connect(m_editButton, &QPushButton::clicked, this, [this]() { editField(); });
    connect(m_removeButton, &QPushButton::clicked, this, [this]() { removeSelectedField(); });
    connect(m_upButton, &QPushButton::clicked, this, [this]() { moveSelected(-1); });
    connect(m_downButton, &QPushButton::clicked, this, [this]() { moveSelected(1); });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this]() { updateButtons(); });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem*, int) { editField(); });

    connect(m_name, &QLineEdit::textChanged, this, [this]() { emit changed(); });
    connect(m_description, &QLineEdit::textChanged, this, [this]() { emit changed(); });
    connect(m_endianness, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]() { emit changed(); });

    rebuild();
    updateButtons();
}

void StructureFormEditor::setTemplate(const StructureTemplate& structure)
{
    m_fields = structure.fields;
    {
        const QSignalBlocker nameBlocker(m_name);
        const QSignalBlocker descriptionBlocker(m_description);
        const QSignalBlocker endianBlocker(m_endianness);
        m_name->setText(structure.name);
        m_description->setText(structure.description);
        m_endianness->setCurrentIndex(structure.littleEndian ? 0 : 1);
    }
    rebuild();
}

void StructureFormEditor::clear()
{
    m_fields.clear();
    {
        const QSignalBlocker nameBlocker(m_name);
        const QSignalBlocker descriptionBlocker(m_description);
        const QSignalBlocker endianBlocker(m_endianness);
        m_name->clear();
        m_description->clear();
        m_endianness->setCurrentIndex(0);
    }
    rebuild();
}

bool StructureFormEditor::isEmpty() const
{
    return m_fields.isEmpty();
}

StructureTemplate StructureFormEditor::structure() const
{
    StructureTemplate structure;
    structure.name = m_name->text().trimmed();
    structure.description = m_description->text().trimmed();
    structure.littleEndian = m_endianness->currentIndex() == 0;
    structure.fields = m_fields;
    return structure;
}

Result<StructureTemplate> StructureFormEditor::buildTemplate() const
{
    const StructureTemplate candidate = structure();
    if (candidate.name.isEmpty()) {
        return makeError(QStringLiteral("Template name is missing"),
                         QStringLiteral("Enter a template name before validating or applying."));
    }
    if (candidate.fields.isEmpty()) {
        return makeError(QStringLiteral("Template has no fields"),
                         QStringLiteral("Add at least one field before validating or applying."));
    }
    // The JSON parser is the single schema authority: serialise the form and
    // let it report problems with the same messages the advanced editor uses.
    const auto parsed = StructureTemplate::fromJson(candidate.toJson());
    if (!parsed)
        return makeError(parsed.error().message, parsed.error().detail);
    return parsed.value();
}

int StructureFormEditor::fieldCount() const
{
    return countFields(m_fields);
}

void StructureFormEditor::rebuild(const QVector<int>& selectPath)
{
    {
        const QSignalBlocker blocker(m_tree);
        m_tree->clear();
        addItems(nullptr, m_fields, {});
        m_tree->expandAll();

        QTreeWidgetItem* item = nullptr;
        if (!selectPath.isEmpty()) {
            const int first = selectPath.first();
            if (first >= 0 && first < m_tree->topLevelItemCount())
                item = m_tree->topLevelItem(first);
            for (int depth = 1; item != nullptr && depth < selectPath.size(); ++depth) {
                const int index = selectPath.at(depth);
                item = index >= 0 && index < item->childCount() ? item->child(index) : nullptr;
            }
        }
        if (item != nullptr) {
            m_tree->setCurrentItem(item);
            m_tree->scrollToItem(item);
        }
    }
    updateButtons();
}

void StructureFormEditor::addItems(QTreeWidgetItem* parent, const QVector<StructureField>& fields,
                                   const QVector<int>& prefix)
{
    for (int i = 0; i < fields.size(); ++i) {
        const StructureField& field = fields.at(i);
        QVector<int> path = prefix;
        path.append(i);

        auto* item = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, field.name);
        item->setText(1, structureFieldTypeName(field.type));
        item->setText(2, QString::number(field.offset));
        const auto size = fieldByteSize(field);
        item->setText(3, size ? QString::number(*size) : QStringLiteral("?"));
        item->setText(4, field.littleEndian
                             ? (*field.littleEndian ? QStringLiteral("little")
                                                    : QStringLiteral("big"))
                             : QStringLiteral("inherit"));
        item->setText(5, field.count > 1 ? QString::number(field.count) : QString());

        QVariantList storedPath;
        for (const int index : path)
            storedPath.append(index);
        item->setData(0, Qt::UserRole, storedPath);
        item->setData(0, Qt::UserRole + 1, static_cast<int>(field.type));

        if (!field.description.isEmpty())
            item->setToolTip(0, field.description);
        item->setToolTip(2, QStringLiteral("Relative to the file start (top level) or the parent "
                                           "struct. %1")
                                .arg(hexOffset(field.offset)));
        if (size) {
            item->setToolTip(3, QStringLiteral("%1 byte(s) in total, including count.")
                                    .arg(*size));
        } else {
            item->setToolTip(3, QStringLiteral("Size depends on the nested fields or is too large "
                                               "to calculate."));
        }

        addItems(item, field.fields, path);
    }
}

QVector<int> StructureFormEditor::currentPath() const
{
    QTreeWidgetItem* item = m_tree->currentItem();
    if (item == nullptr)
        return {};
    QVector<int> path;
    const QVariantList stored = item->data(0, Qt::UserRole).toList();
    path.reserve(stored.size());
    for (const QVariant& value : stored)
        path.append(value.toInt());
    return path;
}

StructureField* StructureFormEditor::fieldAt(const QVector<int>& path)
{
    if (path.isEmpty())
        return nullptr;
    QVector<StructureField>* list = &m_fields;
    StructureField* field = nullptr;
    for (const int index : path) {
        if (index < 0 || index >= list->size())
            return nullptr;
        field = &(*list)[index];
        list = &field->fields;
    }
    return field;
}

QVector<StructureField>* StructureFormEditor::siblingList(const QVector<int>& parentPath)
{
    if (parentPath.isEmpty())
        return &m_fields;
    StructureField* parent = fieldAt(parentPath);
    if (parent == nullptr || parent->type != StructureFieldType::Struct)
        return nullptr;
    return &parent->fields;
}

bool StructureFormEditor::appendField(const StructureField& field, const QVector<int>& parent)
{
    QVector<StructureField>* siblings = siblingList(parent);
    if (siblings == nullptr)
        return false;
    siblings->append(field);
    QVector<int> path = parent;
    path.append(siblings->size() - 1);
    rebuild(path);
    emit changed();
    return true;
}

bool StructureFormEditor::replaceField(const QVector<int>& path, const StructureField& field)
{
    StructureField* target = fieldAt(path);
    if (target == nullptr)
        return false;
    *target = field;
    rebuild(path);
    emit changed();
    return true;
}

bool StructureFormEditor::removeField(const QVector<int>& path)
{
    if (path.isEmpty())
        return false;
    QVector<StructureField>* siblings = siblingList(path.mid(0, path.size() - 1));
    if (siblings == nullptr)
        return false;
    const int index = path.last();
    if (index < 0 || index >= siblings->size())
        return false;
    siblings->removeAt(index);
    rebuild(path.mid(0, path.size() - 1));
    emit changed();
    return true;
}

bool StructureFormEditor::moveField(const QVector<int>& path, int delta)
{
    if (path.isEmpty() || delta == 0)
        return false;
    QVector<StructureField>* siblings = siblingList(path.mid(0, path.size() - 1));
    if (siblings == nullptr)
        return false;
    const int index = path.last();
    const int target = index + delta;
    if (index < 0 || index >= siblings->size() || target < 0 || target >= siblings->size())
        return false;
    std::swap((*siblings)[index], (*siblings)[target]);
    QVector<int> moved = path;
    moved.last() = target;
    rebuild(moved);
    emit changed();
    return true;
}

void StructureFormEditor::addField(bool nested)
{
    QVector<int> parent;
    if (nested) {
        const QVector<int> selection = currentPath();
        StructureField* target = fieldAt(selection);
        if (target == nullptr || target->type != StructureFieldType::Struct) {
            QMessageBox::information(
                this, QStringLiteral("Add nested field"),
                QStringLiteral("Select a struct field first. Nested fields are offsets inside "
                               "that struct."));
            return;
        }
        parent = selection;
    } else {
        const QVector<int> selection = currentPath();
        if (!selection.isEmpty())
            parent = selection.mid(0, selection.size() - 1);
    }

    QVector<StructureField>* siblings = siblingList(parent);
    if (siblings == nullptr)
        return;

    StructureField draft;
    draft.name = uniqueFieldName(*siblings, QStringLiteral("field"));
    draft.offset = nextFreeOffset(*siblings);
    StructureFieldDialog dialog(this, draft, nested ? QStringLiteral("Add nested field")
                                                   : QStringLiteral("Add field"));
    if (dialog.exec() != QDialog::Accepted)
        return;

    const StructureField field = dialog.field();
    siblings = siblingList(parent);
    if (siblings == nullptr)
        return;
    if (siblingNameTaken(*siblings, field.name, -1)) {
        QMessageBox::warning(
            this, QStringLiteral("Duplicate field name"),
            QStringLiteral("Another field at this level is already called \"%1\". Names must be "
                           "unique among siblings.")
                .arg(field.name));
        return;
    }
    appendField(field, parent);
}

void StructureFormEditor::editField()
{
    const QVector<int> path = currentPath();
    StructureField* target = fieldAt(path);
    if (target == nullptr)
        return;

    const QString originalName = target->name;
    const int originalChildren = target->fields.size();
    StructureFieldDialog dialog(this, *target, QStringLiteral("Edit field"));
    if (dialog.exec() != QDialog::Accepted)
        return;

    StructureField updated = dialog.field();
    QVector<StructureField>* siblings = siblingList(path.mid(0, path.size() - 1));
    if (siblings == nullptr)
        return;
    if (siblingNameTaken(*siblings, updated.name, path.last())) {
        QMessageBox::warning(
            this, QStringLiteral("Duplicate field name"),
            QStringLiteral("Another field at this level is already called \"%1\". Names must be "
                           "unique among siblings.")
                .arg(updated.name));
        return;
    }

    if (originalChildren > 0 && updated.type != StructureFieldType::Struct) {
        const auto answer =
            QMessageBox::question(this, QStringLiteral("Remove nested fields?"),
                                  QStringLiteral("\"%1\" contains %2 nested field(s). Changing its "
                                                 "type removes them. Continue?")
                                      .arg(originalName)
                                      .arg(originalChildren),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    replaceField(path, updated);
}

void StructureFormEditor::removeSelectedField()
{
    const QVector<int> path = currentPath();
    StructureField* target = fieldAt(path);
    if (target == nullptr)
        return;

    if (!target->fields.isEmpty()) {
        const auto answer =
            QMessageBox::question(this, QStringLiteral("Remove field"),
                                  QStringLiteral("\"%1\" contains %2 nested field(s). Remove them "
                                                 "all?")
                                      .arg(target->name)
                                      .arg(target->fields.size()),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    removeField(path);
}

void StructureFormEditor::moveSelected(int delta)
{
    moveField(currentPath(), delta);
}

void StructureFormEditor::updateButtons()
{
    const QVector<int> path = currentPath();
    StructureField* target = fieldAt(path);
    const bool hasSelection = target != nullptr;

    m_editButton->setEnabled(hasSelection);
    m_removeButton->setEnabled(hasSelection);
    m_nestedButton->setEnabled(target != nullptr && target->type == StructureFieldType::Struct);

    int siblings = 0;
    if (hasSelection) {
        if (const QVector<StructureField>* list = siblingList(path.mid(0, path.size() - 1)))
            siblings = list->size();
    }
    m_upButton->setEnabled(hasSelection && path.last() > 0);
    m_downButton->setEnabled(hasSelection && path.last() + 1 < siblings);
}

}  // namespace pkg::ui
