#include "ui/StructurePanel.h"

#include "ui/StructureFormEditor.h"

#include "core/AtomicFile.h"
#include "core/FormatUtil.h"

#include "data/Settings.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace pkg::ui {
namespace {

constexpr auto kLogCategory = "structure";

QString offsetText(quint64 offset)
{
    return QStringLiteral("0x") + QString::number(offset, 16).toUpper();
}

QString starterJson()
{
    return QStringLiteral(R"json({
  "format": "pkgstudio.structure",
  "version": 1,
  "name": "New template",
  "description": "Describe what this layout is for.",
  "endianness": "little",
  "fields": [
    {"name": "magic", "type": "bytes", "offset": 0, "length": 4},
    {"name": "header_size", "type": "u16", "offset": 4}
  ]
})json");
}

QString sanitizedFileName(const QString& id)
{
    QString name = id.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("template");
    for (QChar& ch : name) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('.') && ch != QLatin1Char('-') &&
            ch != QLatin1Char('_'))
            ch = QLatin1Char('-');
    }
    return name + QStringLiteral(".json");
}

QString errorText(const Error& error)
{
    return error.detail.isEmpty() ? error.message : error.detail;
}

int countAllFields(const QVector<StructureField>& fields)
{
    int total = fields.size();
    for (const StructureField& field : fields)
        total += countAllFields(field.fields);
    return total;
}

/// Template editor with a form for everyday use and the JSON view for advanced
/// templates. Both edit the same documented schema; Validate, Apply and Save
/// use the active tab and switching tabs carries the layout across.
class TemplateEditor : public QDialog {
public:
    TemplateEditor(QWidget* parent, const StructureTemplate* initial, const HexDocument* document,
                   const QString& templatesDirectory)
        : QDialog(parent), m_document(document), m_templatesDirectory(templatesDirectory)
    {
        setWindowTitle(QStringLiteral("Structure template editor"));
        resize(840, 720);
        setModal(true);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(8);

        m_tabs = new QTabWidget(this);
        m_tabs->setObjectName(QStringLiteral("templateEditorTabs"));

        m_form = new StructureFormEditor(this);
        m_tabs->addTab(m_form, QStringLiteral("Form"));

        auto* jsonPage = new QWidget(this);
        auto* jsonLayout = new QVBoxLayout(jsonPage);
        jsonLayout->setContentsMargins(8, 8, 8, 8);
        jsonLayout->setSpacing(6);
        auto* jsonHint = new QLabel(
            QStringLiteral("Advanced: the documented template schema. Offsets are relative to the "
                           "file (or to the parent struct); types are u8/i8/u16/i16/u32/i32/u64/"
                           "i64/f32/f64/ascii/utf8/bytes/struct. Text and bytes need a length. Use "
                           "count for arrays and nested fields for structs."),
            jsonPage);
        jsonHint->setWordWrap(true);
        jsonHint->setObjectName(QStringLiteral("hintLabel"));
        jsonLayout->addWidget(jsonHint);

        m_editor = new QPlainTextEdit(jsonPage);
        m_editor->setObjectName(QStringLiteral("templateEditor"));
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSize(std::max(9, font.pointSize()));
        m_editor->setFont(font);
        m_editor->setTabStopDistance(4 * m_editor->fontMetrics().horizontalAdvance(QLatin1Char(' ')));
        jsonLayout->addWidget(m_editor, 1);
        m_tabs->addTab(jsonPage, QStringLiteral("JSON (advanced)"));
        layout->addWidget(m_tabs, 1);

        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        m_status->setObjectName(QStringLiteral("hintLabel"));
        layout->addWidget(m_status);

        auto* buttons = new QHBoxLayout();
        auto* validateButton = new QPushButton(QStringLiteral("Validate"), this);
        connect(validateButton, &QPushButton::clicked, this, [this]() { validate(); });
        buttons->addWidget(validateButton);

        auto* saveButton = new QPushButton(QStringLiteral("Save template…"), this);
        connect(saveButton, &QPushButton::clicked, this, [this]() { save(); });
        buttons->addWidget(saveButton);
        buttons->addStretch(1);

        auto* cancel = new QPushButton(QStringLiteral("Close"), this);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        buttons->addWidget(cancel);

        m_applyButton = new QPushButton(QStringLiteral("Apply"), this);
        m_applyButton->setDefault(true);
        m_applyButton->setEnabled(false);
        connect(m_applyButton, &QPushButton::clicked, this, [this]() {
            if (validate())
                accept();
        });
        buttons->addWidget(m_applyButton);
        layout->addLayout(buttons);

        if (initial != nullptr) {
            m_form->setTemplate(*initial);
            writeJson(
                QJsonDocument(initial->toJson()).toJson(QJsonDocument::Indented));
        } else {
            // Start from the same small example in both views so a beginner can
            // edit real fields immediately and the JSON tab never disagrees.
            const QJsonDocument starter =
                QJsonDocument::fromJson(starterJson().toUtf8());
            const auto parsed = StructureTemplate::fromJson(starter.object());
            if (parsed)
                m_form->setTemplate(parsed.value());
            writeJson(starterJson().toUtf8());
        }

        connect(m_tabs, &QTabWidget::currentChanged, this,
                [this](int index) { switchTab(index); });
        connect(m_form, &StructureFormEditor::changed, this, [this]() { markEdited(); });
        connect(m_editor, &QPlainTextEdit::textChanged, this, [this]() { markEdited(); });
    }

    [[nodiscard]] bool hasTemplate() const { return m_valid; }
    [[nodiscard]] StructureTemplate templateValue() const { return m_structure; }
    [[nodiscard]] bool savedToFile() const { return m_savedToFile; }
    [[nodiscard]] QString savedPath() const { return m_savedPath; }

private:
    static constexpr int kJsonTab = 1;

    void markEdited()
    {
        m_valid = false;
        m_applyButton->setEnabled(false);
        m_status->clear();
    }

    void writeJson(const QByteArray& json)
    {
        const QSignalBlocker blocker(m_editor);
        m_editor->setPlainText(QString::fromUtf8(json));
    }

    void writeJsonFromForm()
    {
        writeJson(QJsonDocument(m_form->structure().toJson()).toJson(QJsonDocument::Indented));
    }

    [[nodiscard]] Result<StructureTemplate> parseJson() const
    {
        QJsonParseError error;
        const QJsonDocument document =
            QJsonDocument::fromJson(m_editor->toPlainText().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError) {
            return makeError(QStringLiteral("JSON error"),
                             QStringLiteral("JSON error at offset %1: %2")
                                 .arg(error.offset)
                                 .arg(error.errorString()));
        }
        if (!document.isObject())
            return makeError(QStringLiteral("Invalid template"),
                             QStringLiteral("The template must be a JSON object."));
        return StructureTemplate::fromJson(document.object());
    }

    /// Switching tabs carries the current layout across. Invalid JSON keeps the
    /// user in the JSON tab instead of silently replacing what they typed.
    void switchTab(int index)
    {
        if (m_syncing)
            return;
        if (index == kJsonTab) {
            writeJsonFromForm();
            return;
        }
        const auto parsed = parseJson();
        if (!parsed) {
            m_status->setText(errorText(parsed.error()) +
                              QStringLiteral(" Fix the JSON before switching to the form."));
            m_syncing = true;
            m_tabs->setCurrentIndex(kJsonTab);
            m_syncing = false;
            return;
        }
        m_form->setTemplate(parsed.value());
    }

    bool validate()
    {
        const bool fromJson = m_tabs->currentIndex() == kJsonTab;
        const auto result = fromJson ? parseJson() : m_form->buildTemplate();
        if (!result) {
            m_status->setText(errorText(result.error()));
            m_valid = false;
            m_applyButton->setEnabled(false);
            return false;
        }
        const StructureTemplate parsed = result.value();
        if (fromJson)
            m_form->setTemplate(parsed);
        else
            writeJsonFromForm();

        QString text = QStringLiteral("Valid template '%1' — %2 top-level field(s), %3 field(s) "
                                      "in total, default endianness %4.")
                           .arg(parsed.name)
                           .arg(parsed.fields.size())
                           .arg(countAllFields(parsed.fields))
                           .arg(parsed.littleEndian ? QStringLiteral("little")
                                                    : QStringLiteral("big"));
        if (m_document != nullptr && m_document->isValid()) {
            const auto evaluation = evaluateStructure(parsed, *m_document);
            if (!evaluation) {
                text += QStringLiteral(" ") + errorText(evaluation.error());
                m_status->setText(text);
                m_valid = false;
                m_applyButton->setEnabled(false);
                return false;
            }
            if (!evaluation.value().errors.isEmpty()) {
                text += QStringLiteral(" %1 field(s) fall outside the file; they will be marked "
                                       "as errors in the tree.")
                            .arg(evaluation.value().errors.size());
            }
        }
        m_status->setText(text);
        m_structure = parsed;
        m_valid = true;
        m_applyButton->setEnabled(true);
        return true;
    }

    void save()
    {
        if (!validate())
            return;
        QDir().mkpath(m_templatesDirectory);
        const QString suggested =
            QDir(m_templatesDirectory).filePath(sanitizedFileName(m_structure.id));
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save structure template"), suggested,
            QStringLiteral("Structure template (*.json)"));
        if (path.isEmpty())
            return;
        const QByteArray json =
            QJsonDocument(m_structure.toJson()).toJson(QJsonDocument::Indented);
        const Status written = atomic::writeFile(path, json);
        if (!written) {
            QMessageBox::warning(this, QStringLiteral("Cannot save template"),
                                 written.error().detail.isEmpty() ? written.error().message
                                                                  : written.error().detail);
            return;
        }
        m_savedToFile = true;
        m_savedPath = path;
        accept();
    }

    const HexDocument* m_document = nullptr;
    QString m_templatesDirectory;
    QTabWidget* m_tabs = nullptr;
    StructureFormEditor* m_form = nullptr;
    QPlainTextEdit* m_editor = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_applyButton = nullptr;
    StructureTemplate m_structure;
    bool m_valid = false;
    bool m_savedToFile = false;
    bool m_syncing = false;
    QString m_savedPath;
};

}  // namespace

StructurePanel::StructurePanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("structurePanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("Structure builder"), this);
    title->setObjectName(QStringLiteral("panelTitle"));
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    m_scopeLabel = new QLabel(this);
    m_scopeLabel->setObjectName(QStringLiteral("hintLabel"));
    titleRow->addWidget(m_scopeLabel);
    layout->addLayout(titleRow);

    auto* pickerRow = new QHBoxLayout();
    pickerRow->setSpacing(6);
    m_templateBox = new QComboBox(this);
    m_templateBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_templateBox->setMinimumContentsLength(22);
    m_templateBox->setToolTip(
        QStringLiteral("Structure templates describe a layout. They are not format detection: "
                       "a template can be applied to any file."));
    pickerRow->addWidget(m_templateBox, 1);

    auto* editButton = new QToolButton(this);
    editButton->setText(QStringLiteral("Edit\u2026"));
    editButton->setToolTip(QStringLiteral("Edit or create a structure template"));
    editButton->setAutoRaise(true);
    pickerRow->addWidget(editButton);

    auto* loadButton = new QToolButton(this);
    loadButton->setText(QStringLiteral("Load\u2026"));
    loadButton->setToolTip(QStringLiteral("Load a template JSON file"));
    loadButton->setAutoRaise(true);
    pickerRow->addWidget(loadButton);

    auto* folderButton = new QToolButton(this);
    folderButton->setText(QStringLiteral("Folder"));
    folderButton->setToolTip(QStringLiteral("Open the user template folder"));
    folderButton->setAutoRaise(true);
    pickerRow->addWidget(folderButton);
    layout->addLayout(pickerRow);

    m_disclaimer = new QLabel(
        QStringLiteral("Template interpretation: types, offsets and bounds are checked; "
                       "format conformance is not. Confirmed formats come from the Analysis tab."),
        this);
    m_disclaimer->setObjectName(QStringLiteral("hintLabel"));
    m_disclaimer->setWordWrap(true);
    layout->addWidget(m_disclaimer);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("structureTree"));
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Offset"),
                             QStringLiteral("Length"), QStringLiteral("Type"),
                             QStringLiteral("Value")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_tree->setColumnWidth(0, 133);
    m_tree->setColumnWidth(1, 55);
    m_tree->setColumnWidth(2, 54);
    m_tree->setColumnWidth(3, 50);
    layout->addWidget(m_tree, 1);

    m_coverageLabel = new QLabel(this);
    m_coverageLabel->setObjectName(QStringLiteral("hintLabel"));
    m_coverageLabel->setWordWrap(true);
    layout->addWidget(m_coverageLabel);

    m_diagnostics = new QPlainTextEdit(this);
    m_diagnostics->setObjectName(QStringLiteral("structureDiagnostics"));
    m_diagnostics->setReadOnly(true);
    m_diagnostics->setMaximumHeight(110);
    m_diagnostics->setVisible(false);
    layout->addWidget(m_diagnostics);

    connect(m_templateBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &StructurePanel::applyIndex);
    connect(editButton, &QToolButton::clicked, this, &StructurePanel::editTemplate);
    connect(loadButton, &QToolButton::clicked, this, &StructurePanel::loadTemplateFile);
    connect(folderButton, &QToolButton::clicked, this, &StructurePanel::openTemplatesDirectory);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        QTreeWidgetItem* item = m_tree->currentItem();
        if (item == nullptr)
            return;
        const quint64 offset = item->data(0, Qt::UserRole).toULongLong();
        const quint64 length = item->data(0, Qt::UserRole + 1).toULongLong();
        if (length > 0)
            emit fieldSelected(offset, length);
    });

    reloadTemplates();
    updateEmptyState();
}

QString StructurePanel::templatesDirectory() const
{
    return QDir(Settings::dataDirectory()).filePath(QStringLiteral("templates"));
}

void StructurePanel::setDocument(HexDocument* document)
{
    m_document = document;
    clearTree();
    m_hasTemplate = false;
    m_hasEvaluation = false;
    m_templateBox->setCurrentIndex(0);
    updateEmptyState();
}

void StructurePanel::reloadTemplates(const QString& selectId)
{
    m_entries.clear();
    for (const StructureTemplate& structure : builtinStructureTemplates())
        m_entries.append({structure, QStringLiteral("Built-in example")});

    QStringList loadErrors;
    const auto userTemplates = loadStructureTemplates(templatesDirectory(), &loadErrors);
    if (userTemplates) {
        for (const StructureTemplate& structure : userTemplates.value())
            m_entries.append({structure, QStringLiteral("User template")});
    }

    QString diagnostics;
    if (!loadErrors.isEmpty()) {
        diagnostics = QStringLiteral("Some user templates could not be loaded:\n") +
                      loadErrors.join(QLatin1Char('\n'));
        m_diagnostics->setPlainText(diagnostics);
        m_diagnostics->setVisible(true);
    }

    rebuildCombo(selectId);
}

void StructurePanel::rebuildCombo(const QString& selectId)
{
    const QString wanted = selectId.isEmpty()
                               ? (m_hasTemplate ? m_structure.id : QString())
                               : selectId;
    int selected = 0;
    {
        const QSignalBlocker blocker(m_templateBox);
        m_templateBox->clear();
        m_templateBox->addItem(QStringLiteral("Select a template\u2026"));
        for (int i = 0; i < m_entries.size(); ++i) {
            m_templateBox->addItem(QStringLiteral("%1   \u00B7  %2")
                                       .arg(m_entries.at(i).structure.name,
                                            m_entries.at(i).origin));
            if (!wanted.isEmpty() && m_entries.at(i).structure.id == wanted)
                selected = i + 1;
        }
    }

    if (selected > 0) {
        {
            const QSignalBlocker blocker(m_templateBox);
            m_templateBox->setCurrentIndex(selected);
        }
        applyIndex(selected);
        return;
    }

    m_templateBox->setCurrentIndex(0);
    if (!wanted.isEmpty()) {
        // The selected template disappeared (file deleted); say so instead of
        // silently showing a stale tree.
        m_hasTemplate = false;
        updateEmptyState();
    }
}

bool StructurePanel::selectTemplateById(const QString& id)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).structure.id != id)
            continue;
        m_templateBox->setCurrentIndex(i + 1);
        return m_hasTemplate && m_structure.id == id;
    }
    return false;
}

void StructurePanel::setTemplate(const StructureTemplate& structure, const QString& origin)
{
    if (!structure.id.isEmpty()) {
        const auto existing = std::find_if(m_entries.begin(), m_entries.end(),
                                           [&structure](const TemplateEntry& entry) {
                                               return entry.structure.id == structure.id;
                                           });
        if (existing != m_entries.end()) {
            existing->structure = structure;
            existing->origin = origin;
        } else {
            m_entries.append({structure, origin});
        }
    }
    rebuildCombo(structure.id);
}

void StructurePanel::applyIndex(int index)
{
    if (index <= 0 || index > m_entries.size()) {
        m_hasTemplate = false;
        m_hasEvaluation = false;
        clearTree();
        updateEmptyState();
        return;
    }
    m_structure = m_entries.at(index - 1).structure;
    m_hasTemplate = true;
    m_scopeLabel->setText(
        QStringLiteral("%1 \u00B7 %2")
            .arg(m_structure.littleEndian ? QStringLiteral("little-endian")
                                          : QStringLiteral("big-endian"),
                 m_entries.at(index - 1).origin));
    m_templateBox->setToolTip(m_structure.description.isEmpty()
                                  ? QStringLiteral("User-authored layout; not a format verdict.")
                                  : m_structure.description);
    evaluateCurrent();
    emit templateChanged(m_structure.name);
}

void StructurePanel::evaluateCurrent()
{
    if (m_document == nullptr || !m_document->isValid() || !m_hasTemplate) {
        updateEmptyState();
        return;
    }
    const auto evaluation = evaluateStructure(m_structure, *m_document);
    if (!evaluation) {
        m_hasEvaluation = false;
        clearTree();
        m_coverageLabel->clear();
        m_diagnostics->setPlainText(evaluation.error().detail.isEmpty()
                                        ? evaluation.error().message
                                        : evaluation.error().detail);
        m_diagnostics->setVisible(true);
        return;
    }
    m_evaluation = evaluation.value();
    m_hasEvaluation = true;
    populateTree();
    updateDiagnostics(m_evaluation);
}

void StructurePanel::refresh()
{
    if (m_hasTemplate)
        evaluateCurrent();
    else
        updateEmptyState();
}

void StructurePanel::clearTree()
{
    m_tree->clear();
}

void StructurePanel::updateEmptyState()
{
    clearTree();
    m_coverageLabel->clear();
    m_diagnostics->clear();
    m_diagnostics->setVisible(false);
    if (m_document == nullptr || !m_document->isValid()) {
        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(0, QStringLiteral("Open a file to inspect its structure."));
        item->setFlags(Qt::NoItemFlags);
        m_scopeLabel->clear();
        return;
    }
    if (!m_hasTemplate) {
        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(0, QStringLiteral("Choose a template above, or create one with Edit\u2026"));
        item->setFlags(Qt::NoItemFlags);
        m_scopeLabel->setText(QStringLiteral("no template"));
    }
}

namespace {

void addNode(QTreeWidget* tree, QTreeWidgetItem* parent, const StructureNode& node)
{
    auto* item = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
    item->setText(0, node.name);
    item->setText(1, offsetText(node.offset));
    item->setText(2, QString::number(node.length));
    item->setText(3, node.typeName);
    item->setText(4, node.error ? QStringLiteral("! ") + node.display : node.display);
    item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(node.offset));
    item->setData(0, Qt::UserRole + 1, QVariant::fromValue<qulonglong>(node.length));
    item->setData(0, Qt::UserRole + 2, node.path);
    if (!node.note.isEmpty() || node.error)
        item->setToolTip(4, node.note.isEmpty() ? QStringLiteral("Outside the file") : node.note);
    if (node.error) {
        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(4, font);
    }
    for (const StructureNode& child : node.children)
        addNode(tree, item, child);
}

QTreeWidgetItem* findPath(QTreeWidget* tree, const QString& path)
{
    QList<QTreeWidgetItem*> stack;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        stack.append(tree->topLevelItem(i));
    while (!stack.isEmpty()) {
        QTreeWidgetItem* item = stack.takeLast();
        if (item->data(0, Qt::UserRole + 2).toString() == path)
            return item;
        for (int i = 0; i < item->childCount(); ++i)
            stack.append(item->child(i));
    }
    return nullptr;
}

}  // namespace

void StructurePanel::populateTree()
{
    const QString previous =
        m_tree->currentItem() != nullptr
            ? m_tree->currentItem()->data(0, Qt::UserRole + 2).toString()
            : QString();
    clearTree();
    for (const StructureNode& node : m_evaluation.roots)
        addNode(m_tree, nullptr, node);
    m_tree->expandAll();

    if (!previous.isEmpty()) {
        if (QTreeWidgetItem* item = findPath(m_tree, previous)) {
            const QSignalBlocker blocker(m_tree);
            m_tree->setCurrentItem(item);
        }
    }

    const double percent =
        m_document->size() == 0
            ? 0.0
            : 100.0 * static_cast<double>(m_evaluation.coveredBytes) /
                  static_cast<double>(m_document->size());
    m_coverageLabel->setText(
        QStringLiteral("Fields cover %1 of %2 (%3%) \u00B7 %4 field(s) \u00B7 template "
                       "interpretation only")
            .arg(fmt::humanBytes(m_evaluation.coveredBytes),
                 fmt::humanBytes(m_document->size()))
            .arg(QString::number(percent, 'f', 1))
            .arg(m_evaluation.fieldCount));
    m_disclaimer->setToolTip(m_structure.description);
}

void StructurePanel::updateDiagnostics(const StructureEvaluation& evaluation)
{
    QStringList lines;
    for (const QString& error : evaluation.errors)
        lines << QStringLiteral("Error: ") + error;
    for (const QString& warning : evaluation.warnings)
        lines << QStringLiteral("Warning: ") + warning;
    if (lines.isEmpty()) {
        m_diagnostics->setVisible(false);
        m_diagnostics->clear();
        return;
    }
    m_diagnostics->setPlainText(lines.join(QLatin1Char('\n')));
    m_diagnostics->setVisible(true);
}

bool StructurePanel::highlightOffset(quint64 offset)
{
    if (!m_hasEvaluation)
        return false;

    QTreeWidgetItem* best = nullptr;
    quint64 bestLength = std::numeric_limits<quint64>::max();
    QList<QTreeWidgetItem*> stack;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        stack.append(m_tree->topLevelItem(i));
    while (!stack.isEmpty()) {
        QTreeWidgetItem* item = stack.takeLast();
        const quint64 itemOffset = item->data(0, Qt::UserRole).toULongLong();
        const quint64 itemLength = item->data(0, Qt::UserRole + 1).toULongLong();
        if (itemLength > 0 && offset >= itemOffset && offset < itemOffset + itemLength &&
            itemLength < bestLength) {
            best = item;
            bestLength = itemLength;
        }
        for (int i = 0; i < item->childCount(); ++i)
            stack.append(item->child(i));
    }
    if (best == nullptr)
        return false;

    const QSignalBlocker blocker(m_tree);
    m_tree->setCurrentItem(best);
    m_tree->scrollToItem(best);
    return true;
}

void StructurePanel::editTemplate()
{
    TemplateEditor editor(this, m_hasTemplate ? &m_structure : nullptr, m_document,
                          templatesDirectory());
    if (editor.exec() != QDialog::Accepted || !editor.hasTemplate())
        return;
    if (editor.savedToFile()) {
        reloadTemplates(editor.templateValue().id);
        return;
    }
    setTemplate(editor.templateValue(),
                QStringLiteral("Session template (not saved to disk)"));
}

void StructurePanel::loadTemplateFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Load structure template"), templatesDirectory(),
        QStringLiteral("Structure template (*.json);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("Cannot read template"),
                             QStringLiteral("The file could not be opened: %1").arg(path));
        return;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this, QStringLiteral("Invalid template"),
                             QStringLiteral("JSON error at offset %1: %2")
                                 .arg(error.offset)
                                 .arg(error.errorString()));
        return;
    }
    auto structure = StructureTemplate::fromJson(document.object());
    if (!structure) {
        QMessageBox::warning(this, QStringLiteral("Invalid template"),
                             structure.error().detail.isEmpty()
                                 ? structure.error().message
                                 : structure.error().detail);
        return;
    }
    setTemplate(structure.value(), QStringLiteral("Loaded: %1").arg(QFileInfo(path).fileName()));
}

void StructurePanel::openTemplatesDirectory()
{
    const QString directory = templatesDirectory();
    if (!QDir().mkpath(directory)) {
        QMessageBox::warning(this, QStringLiteral("Cannot open template folder"),
                             QStringLiteral("Could not create %1").arg(directory));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

}  // namespace pkg::ui
