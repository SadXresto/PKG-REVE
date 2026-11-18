// Form-based structure field editing: the form must produce exactly the same
// schema the JSON editor accepts, and report actionable problems.
#include "ui/StructureFormEditor.h"

#include <QJsonDocument>
#include <QLineEdit>
#include <QTreeWidget>

#include <gtest/gtest.h>

using namespace pkg;
using namespace pkg::ui;

namespace {

StructureTemplate parseTemplate(const QString& json)
{
    const auto parsed =
        StructureTemplate::fromJson(QJsonDocument::fromJson(json.toUtf8()).object());
    return parsed ? parsed.value() : StructureTemplate{};
}

StructureField field(const QString& name, StructureFieldType type, quint64 offset,
                     quint64 length = 0)
{
    StructureField result;
    result.name = name;
    result.type = type;
    result.offset = offset;
    result.length = length;
    return result;
}

void setName(StructureFormEditor& editor, const QString& name)
{
    auto* edit = editor.findChild<QLineEdit*>(QStringLiteral("structureFormName"));
    ASSERT_NE(edit, nullptr);
    edit->setText(name);
}

}  // namespace

TEST(StructureFormEditorWidget, LoadsExistingTemplateAndRebuildsItExactly)
{
    const StructureTemplate original = parseTemplate(QStringLiteral(R"json({
        "name": "Form record",
        "description": "Nested test layout",
        "endianness": "big",
        "fields": [
          {"name": "magic", "type": "bytes", "offset": 0, "length": 4},
          {"name": "items", "type": "struct", "offset": 4, "count": 2, "fields": [
            {"name": "id", "type": "u16", "offset": 0, "endianness": "little"},
            {"name": "value", "type": "u8", "offset": 2}
          ]}
        ]
    })json"));
    ASSERT_EQ(original.fields.size(), 2U);

    StructureFormEditor editor;
    int changes = 0;
    QObject::connect(&editor, &StructureFormEditor::changed, [&changes]() { ++changes; });
    editor.setTemplate(original);
    EXPECT_EQ(changes, 0) << "loading a template must not count as an edit";
    EXPECT_FALSE(editor.isEmpty());
    EXPECT_EQ(editor.fieldCount(), 4);

    auto* tree = editor.findChild<QTreeWidget*>(QStringLiteral("structureFormTree"));
    ASSERT_NE(tree, nullptr);
    ASSERT_EQ(tree->topLevelItemCount(), 2);
    ASSERT_EQ(tree->topLevelItem(1)->childCount(), 2);
    EXPECT_EQ(tree->topLevelItem(0)->text(1), QStringLiteral("bytes"));
    EXPECT_EQ(tree->topLevelItem(0)->text(2), QStringLiteral("0"));
    EXPECT_EQ(tree->topLevelItem(1)->text(4), QStringLiteral("inherit"));
    EXPECT_EQ(tree->topLevelItem(1)->child(0)->text(4), QStringLiteral("little"));

    auto built = editor.buildTemplate();
    ASSERT_TRUE(built) << built.error().detail.toStdString();
    EXPECT_EQ(built.value().toJson(), original.toJson());
}

TEST(StructureFormEditorWidget, AddsNestedFieldsEditsRemovesAndReorders)
{
    StructureFormEditor editor;
    setName(editor, QStringLiteral("Form record"));
    EXPECT_TRUE(editor.isEmpty());
    EXPECT_TRUE(editor.appendField(field(QStringLiteral("magic"), StructureFieldType::Bytes, 0, 4)));

    StructureField items = field(QStringLiteral("items"), StructureFieldType::Struct, 4);
    items.count = 2;
    items.fields = {
        field(QStringLiteral("id"), StructureFieldType::UInt16, 0),
        field(QStringLiteral("value"), StructureFieldType::UInt8, 2),
    };
    EXPECT_TRUE(editor.appendField(items));
    EXPECT_EQ(editor.fieldCount(), 4);

    // Nested fields only go inside a struct.
    EXPECT_TRUE(editor.appendField(field(QStringLiteral("extra"), StructureFieldType::UInt16, 3),
                                   {1}));
    EXPECT_EQ(editor.fieldCount(), 5);
    EXPECT_FALSE(
        editor.appendField(field(QStringLiteral("bad"), StructureFieldType::UInt8, 0), {0}));

    EXPECT_TRUE(editor.replaceField({0}, field(QStringLiteral("signature"),
                                               StructureFieldType::Bytes, 0, 2)));
    EXPECT_TRUE(editor.moveField({1}, -1));
    ASSERT_EQ(editor.fields().size(), 2U);
    EXPECT_EQ(editor.fields().at(0).name, QStringLiteral("items"));
    EXPECT_EQ(editor.fields().at(1).name, QStringLiteral("signature"));
    ASSERT_EQ(editor.fields().at(0).fields.size(), 3U);
    EXPECT_EQ(editor.fields().at(0).fields.at(2).name, QStringLiteral("extra"));

    auto built = editor.buildTemplate();
    ASSERT_TRUE(built) << built.error().detail.toStdString();
    EXPECT_EQ(built.value().fields.at(1).length, 2U);
    EXPECT_EQ(built.value().fields.at(0).count, 2);
    EXPECT_EQ(built.value().fields.at(0).fields.at(2).offset, 3U);

    // Out-of-range operations fail instead of silently doing nothing visible.
    EXPECT_FALSE(editor.moveField({0}, -1));
    EXPECT_FALSE(editor.removeField({9}));
    EXPECT_FALSE(editor.removeField({}));

    EXPECT_TRUE(editor.removeField({0}));
    ASSERT_EQ(editor.fields().size(), 1U);
    EXPECT_EQ(editor.fields().at(0).name, QStringLiteral("signature"));
    EXPECT_EQ(editor.fieldCount(), 1);
}

TEST(StructureFormEditorWidget, ReportsActionableValidationErrors)
{
    StructureFormEditor unnamed;
    EXPECT_TRUE(unnamed.appendField(field(QStringLiteral("magic"), StructureFieldType::UInt8, 0)));
    auto missingName = unnamed.buildTemplate();
    ASSERT_FALSE(missingName);
    EXPECT_NE(missingName.error().message.toLower().indexOf(QStringLiteral("name")), -1);

    StructureFormEditor noFields;
    setName(noFields, QStringLiteral("Empty layout"));
    auto emptyResult = noFields.buildTemplate();
    ASSERT_FALSE(emptyResult);
    EXPECT_NE(emptyResult.error().message.toLower().indexOf(QStringLiteral("field")), -1);

    StructureFormEditor broken;
    setName(broken, QStringLiteral("Broken layout"));
    broken.appendField(field(QStringLiteral("label"), StructureFieldType::Ascii, 0, 0));
    auto badLength = broken.buildTemplate();
    ASSERT_FALSE(badLength);
    EXPECT_NE(badLength.error().detail.indexOf(QStringLiteral("label")), -1);
    EXPECT_NE(badLength.error().detail.indexOf(QStringLiteral("length")), -1);

    StructureFormEditor duplicate;
    setName(duplicate, QStringLiteral("Duplicate layout"));
    duplicate.appendField(field(QStringLiteral("value"), StructureFieldType::UInt8, 0));
    duplicate.appendField(field(QStringLiteral("VALUE"), StructureFieldType::UInt8, 1));
    auto duplicateResult = duplicate.buildTemplate();
    ASSERT_FALSE(duplicateResult);
    EXPECT_NE(duplicateResult.error().detail.toLower().indexOf(QStringLiteral("duplicate")), -1);
}
