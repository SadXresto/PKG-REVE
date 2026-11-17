// Smart command center ranking and structure panel widget behaviour.
#include "core/FileSource.h"
#include "core/HexDocument.h"
#include "core/Structure.h"
#include "ui/CommandPalette.h"
#include "ui/StructurePanel.h"

#include <QFile>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTreeWidget>

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

using namespace pkg;
using namespace pkg::ui;

namespace {

std::unique_ptr<HexDocument> openBytes(const QTemporaryDir& dir, const QByteArray& content)
{
    const QString path = dir.filePath(QStringLiteral("panel.bin"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return nullptr;
    file.write(content);
    file.close();
    auto source = FileSource::open(path);
    if (!source)
        return nullptr;
    return std::make_unique<HexDocument>(std::move(source.value()));
}

StructureTemplate parseStructure(const QString& json)
{
    auto parsed = StructureTemplate::fromJson(QJsonDocument::fromJson(json.toUtf8()).object());
    if (!parsed)
        return {};
    return parsed.value();
}

QTreeWidgetItem* findItem(QTreeWidget* tree, const QString& text)
{
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = tree->topLevelItem(i);
        if (item->text(0) == text)
            return item;
    }
    return nullptr;
}

}  // namespace

TEST(CommandPaletteRanking, PrefersTitleMatchesAndSearchesDescriptions)
{
    const QList<CommandPalette::Entry> entries{
        {QStringLiteral("a"), QStringLiteral("Save"), QStringLiteral("File"),
         QStringLiteral("Ctrl+S"), QStringLiteral("Write pending byte edits"),
         QStringLiteral("write backup")},
        {QStringLiteral("b"), QStringLiteral("Toggle structure builder"),
         QStringLiteral("View"), QStringLiteral("Ctrl+Shift+B"),
         QStringLiteral("Show or hide the binary structure builder"),
         QStringLiteral("layout template fields")},
        {QStringLiteral("c"), QStringLiteral("Structure template guide"),
         QStringLiteral("Documentation"), QString(),
         QStringLiteral("Offline guide to the structure template schema"),
         QStringLiteral("help docs")},
        {QStringLiteral("d"), QStringLiteral("Getting started guide"),
         QStringLiteral("Documentation"), QString(),
         QStringLiteral("Offline walkthrough of the main workflows"),
         QStringLiteral("tutorial manual")},
    };

    const auto structure = CommandPalette::rankEntries(entries, QStringLiteral("structure"));
    ASSERT_EQ(structure.size(), 2);
    EXPECT_EQ(structure.at(0).id, QStringLiteral("c"));
    const auto toggle = CommandPalette::rankEntries(entries, QStringLiteral("toggle"));
    ASSERT_EQ(toggle.size(), 1U);
    EXPECT_EQ(toggle.first().id, QStringLiteral("b"));

    const auto docs = CommandPalette::rankEntries(entries, QStringLiteral("documentation"));
    ASSERT_EQ(docs.size(), 2);
    EXPECT_TRUE(docs.at(0).category == QStringLiteral("Documentation"));

    const auto tutorial = CommandPalette::rankEntries(entries, QStringLiteral("tutorial"));
    ASSERT_EQ(tutorial.size(), 1U);
    EXPECT_EQ(tutorial.first().id, QStringLiteral("d"));

    const auto multi = CommandPalette::rankEntries(entries, QStringLiteral("save backup"));
    ASSERT_EQ(multi.size(), 1U);
    EXPECT_EQ(multi.first().id, QStringLiteral("a"));

    EXPECT_TRUE(CommandPalette::rankEntries(entries, QStringLiteral("zzzz")).isEmpty());

    const auto all = CommandPalette::rankEntries(entries, QString());
    ASSERT_EQ(all.size(), entries.size());
    EXPECT_EQ(all.first().id, QStringLiteral("a"));
}

TEST(StructurePanelWidget, BuildsTreeAndSynchronisesSelections)
{
    QTemporaryDir dir;
    auto document = openBytes(dir, QByteArray("MG\x05\x06", 4));
    ASSERT_TRUE(document);

    const StructureTemplate structure = parseStructure(QStringLiteral(R"json({
        "name": "Panel record",
        "fields": [
          {"name": "magic", "type": "bytes", "offset": 0, "length": 2},
          {"name": "pair", "type": "struct", "offset": 2, "fields": [
            {"name": "a", "type": "u8", "offset": 0},
            {"name": "b", "type": "u8", "offset": 1}
          ]}
        ]
    })json"));
    ASSERT_FALSE(structure.name.isEmpty());

    StructurePanel panel;
    panel.setDocument(document.get());
    std::vector<std::pair<quint64, quint64>> selections;
    QObject::connect(&panel, &StructurePanel::fieldSelected,
                     [&selections](quint64 offset, quint64 length) {
                         selections.emplace_back(offset, length);
                     });
    panel.setTemplate(structure, QStringLiteral("Test template"));
    ASSERT_TRUE(panel.hasTemplate());

    auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("structureTree"));
    ASSERT_NE(tree, nullptr);
    ASSERT_EQ(tree->topLevelItemCount(), 2);
    QTreeWidgetItem* pair = findItem(tree, QStringLiteral("pair"));
    ASSERT_NE(pair, nullptr);
    ASSERT_EQ(pair->childCount(), 2);
    EXPECT_EQ(pair->text(1), QStringLiteral("0x2"));

    tree->setCurrentItem(pair);
    ASSERT_EQ(selections.size(), 1U);
    EXPECT_EQ(selections.back().first, 2U);
    EXPECT_EQ(selections.back().second, 2U);

    EXPECT_TRUE(panel.highlightOffset(3));
    EXPECT_EQ(selections.size(), 1U) << "hex -> tree sync must not emit fieldSelected";
    EXPECT_EQ(tree->currentItem()->data(0, Qt::UserRole + 2).toString(),
              QStringLiteral("pair.b"));
}

TEST(StructurePanelWidget, ShowsValidationErrorsInsteadOfValues)
{
    QTemporaryDir dir;
    auto document = openBytes(dir, QByteArray("\x01\x02", 2));
    ASSERT_TRUE(document);

    const StructureTemplate structure = parseStructure(QStringLiteral(R"json({
        "name": "Too long",
        "fields": [{"name": "value", "type": "u32", "offset": 0}]
    })json"));
    ASSERT_FALSE(structure.name.isEmpty());

    StructurePanel panel;
    panel.setDocument(document.get());
    panel.setTemplate(structure, QStringLiteral("Broken bounds"));

    auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("structureTree"));
    ASSERT_NE(tree, nullptr);
    ASSERT_EQ(tree->topLevelItemCount(), 1);
    EXPECT_EQ(tree->topLevelItem(0)->text(4), QStringLiteral("! not read"));

    auto* diagnostics = panel.findChild<QPlainTextEdit*>(QStringLiteral("structureDiagnostics"));
    ASSERT_NE(diagnostics, nullptr);
    EXPECT_TRUE(diagnostics->isVisibleTo(&panel));
    EXPECT_NE(diagnostics->toPlainText().indexOf(QStringLiteral("Error:")), -1);
    EXPECT_NE(diagnostics->toPlainText().indexOf(QStringLiteral("past the end")), -1);
}
