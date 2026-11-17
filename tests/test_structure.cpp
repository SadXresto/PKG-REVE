// Binary structure templates: parsing, validation and interpretation.
#include "core/FileSource.h"
#include "core/HexDocument.h"
#include "core/Structure.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

using namespace pkg;

namespace {

std::unique_ptr<HexDocument> openBytes(const QTemporaryDir& dir, const QByteArray& content,
                                       const QString& name = QStringLiteral("structure.bin"))
{
    const QString path = dir.filePath(name);
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

StructureTemplate parse(const QString& json, bool* ok = nullptr)
{
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    auto parsed = StructureTemplate::fromJson(document.object());
    if (ok != nullptr)
        *ok = static_cast<bool>(parsed);
    return parsed ? parsed.value() : StructureTemplate{};
}

const char* kTemplate = R"json({
  "format": "pkgstudio.structure",
  "version": 1,
  "name": "Test record",
  "endianness": "little",
  "fields": [
    {"name": "magic", "type": "bytes", "offset": 0, "length": 2},
    {"name": "count", "type": "u8", "offset": 2},
    {"name": "items", "type": "struct", "offset": 3, "count": 2, "fields": [
      {"name": "id", "type": "u16", "offset": 0},
      {"name": "value", "type": "u8", "offset": 2}
    ]}
  ]
})json";

}  // namespace

TEST(StructureTemplate, ParsesAndEvaluatesNestedArrays)
{
    QTemporaryDir dir;
    QByteArray bytes;
    bytes.append("MG", 2);
    bytes.append(static_cast<char>(2));
    const char items[] = {0x01, 0x00, 0x05, 0x02, 0x00, 0x07};
    bytes.append(items, static_cast<int>(sizeof(items)));
    auto document = openBytes(dir, bytes);
    ASSERT_TRUE(document);

    bool parsedOk = false;
    const StructureTemplate structure = parse(QLatin1String(kTemplate), &parsedOk);
    ASSERT_TRUE(parsedOk);
    EXPECT_EQ(structure.name, QStringLiteral("Test record"));

    const auto evaluation = evaluateStructure(structure, *document);
    ASSERT_TRUE(evaluation) << evaluation.error().message.toStdString();
    EXPECT_TRUE(evaluation.value().errors.isEmpty());
    EXPECT_TRUE(evaluation.value().allFieldsInBounds);
    ASSERT_EQ(evaluation.value().roots.size(), 3U);

    const auto& itemsNode = evaluation.value().roots.at(2);
    ASSERT_EQ(itemsNode.children.size(), 2U);
    ASSERT_EQ(itemsNode.children.at(0).children.size(), 2U);
    EXPECT_EQ(itemsNode.children.at(0).children.at(0).display,
              QStringLiteral("0x0001  (1)"));
    EXPECT_EQ(itemsNode.children.at(1).children.at(0).display,
              QStringLiteral("0x0002  (2)"));
    EXPECT_EQ(itemsNode.children.at(1).children.at(1).offset, 8U);
    EXPECT_EQ(evaluation.value().coveredBytes, 9U);
}

TEST(StructureTemplate, HonoursEndiannessAtTemplateAndFieldLevel)
{
    QTemporaryDir dir;
    auto document = openBytes(dir, QByteArray::fromHex("12345678"));
    ASSERT_TRUE(document);

    bool ok = false;
    const StructureTemplate little = parse(QStringLiteral(R"json({
        "name": "Little", "endianness": "little",
        "fields": [{"name": "value", "type": "u16", "offset": 0}]
    })json"),
                                           &ok);
    ASSERT_TRUE(ok);
    const auto littleResult = evaluateStructure(little, *document);
    ASSERT_TRUE(littleResult);
    EXPECT_EQ(littleResult.value().roots.at(0).display, QStringLiteral("0x3412  (13330)"));

    const StructureTemplate big = parse(QStringLiteral(R"json({
        "name": "Mixed", "endianness": "little",
        "fields": [
          {"name": "little_value", "type": "u16", "offset": 0},
          {"name": "big_value", "type": "u16", "offset": 0, "endianness": "big"}
        ]
    })json"),
                                       &ok);
    ASSERT_TRUE(ok);
    const auto bigResult = evaluateStructure(big, *document);
    ASSERT_TRUE(bigResult);
    EXPECT_EQ(bigResult.value().roots.at(0).display, QStringLiteral("0x3412  (13330)"));
    EXPECT_EQ(bigResult.value().roots.at(1).display, QStringLiteral("0x1234  (4660)"));
}

TEST(StructureTemplate, OutOfBoundsFieldIsAnErrorNodeNotAValue)
{
    QTemporaryDir dir;
    auto document = openBytes(dir, QByteArray::fromHex("01020304"));
    ASSERT_TRUE(document);

    bool ok = false;
    const StructureTemplate structure = parse(QStringLiteral(R"json({
        "name": "Truncated read",
        "fields": [
          {"name": "inside", "type": "u16", "offset": 0},
          {"name": "past_end", "type": "u32", "offset": 2}
        ]
    })json"),
                                             &ok);
    ASSERT_TRUE(ok);
    const auto evaluation = evaluateStructure(structure, *document);
    ASSERT_TRUE(evaluation);
    EXPECT_FALSE(evaluation.value().errors.isEmpty());
    EXPECT_FALSE(evaluation.value().allFieldsInBounds);
    ASSERT_EQ(evaluation.value().roots.size(), 2U);
    EXPECT_FALSE(evaluation.value().roots.at(0).error);
    EXPECT_TRUE(evaluation.value().roots.at(1).error);
    EXPECT_EQ(evaluation.value().roots.at(1).display, QStringLiteral("not read"));
    EXPECT_NE(evaluation.value().roots.at(1).note.indexOf(QStringLiteral("Outside the file")), -1);
}

TEST(StructureTemplate, RejectsInvalidDefinitionsWithExplanations)
{
    bool ok = true;
    parse(QStringLiteral(R"json({"name": "broken", "fields": [
        {"name": "a", "type": "u16x", "offset": 0}
    ]})json"),
          &ok);
    EXPECT_FALSE(ok);

    parse(QStringLiteral(R"json({"name": "broken", "fields": [
        {"name": "text", "type": "ascii", "offset": 0}
    ]})json"),
          &ok);
    EXPECT_FALSE(ok);

    parse(QStringLiteral(R"json({"name": "broken", "fields": [
        {"name": "dup", "type": "u8", "offset": 0},
        {"name": "DUP", "type": "u8", "offset": 1}
    ]})json"),
          &ok);
    EXPECT_FALSE(ok);

    parse(QStringLiteral(R"json({"version": 99, "name": "future", "fields": [
        {"name": "a", "type": "u8", "offset": 0}
    ]})json"),
          &ok);
    EXPECT_FALSE(ok);

    parse(QStringLiteral(R"json({"name": "empty", "fields": []})json"), &ok);
    EXPECT_FALSE(ok);

    parse(QStringLiteral(R"json({"name": "negative", "fields": [
        {"name": "a", "type": "u8", "offset": -1}
    ]})json"),
          &ok);
    EXPECT_FALSE(ok);
}

TEST(StructureTemplate, BuiltinsMatchShippedSamples)
{
    const QVector<StructureTemplate> templates = builtinStructureTemplates();
    ASSERT_EQ(templates.size(), 3);

    const auto elf = std::find_if(templates.begin(), templates.end(), [](const auto& structure) {
        return structure.id == QStringLiteral("builtin.elf64-le-header");
    });
    ASSERT_NE(elf, templates.end());
    auto elfDocument = FileSource::open(QStringLiteral(PKGSTUDIO_SOURCE_DIR "/samples/sample.elf"));
    ASSERT_TRUE(elfDocument) << elfDocument.error().message.toStdString();
    HexDocument elfBytes(std::move(elfDocument.value()));
    const auto elfResult = evaluateStructure(*elf, elfBytes);
    ASSERT_TRUE(elfResult);
    EXPECT_TRUE(elfResult.value().errors.isEmpty());
    const auto entry = std::find_if(elfResult.value().roots.begin(), elfResult.value().roots.end(),
                                    [](const auto& node) { return node.name == QStringLiteral("e_entry"); });
    ASSERT_NE(entry, elfResult.value().roots.end());
    EXPECT_EQ(entry->display, QStringLiteral("0x0000000000401000"));
    EXPECT_EQ(entry->note, QStringLiteral("Decimal: 4198400"));

    const auto zip = std::find_if(templates.begin(), templates.end(), [](const auto& structure) {
        return structure.id == QStringLiteral("builtin.zip-local-header");
    });
    ASSERT_NE(zip, templates.end());
    auto zipDocument = FileSource::open(QStringLiteral(PKGSTUDIO_SOURCE_DIR "/samples/sample.apk"));
    ASSERT_TRUE(zipDocument) << zipDocument.error().message.toStdString();
    HexDocument zipBytes(std::move(zipDocument.value()));
    const auto zipResult = evaluateStructure(*zip, zipBytes);
    ASSERT_TRUE(zipResult);
    EXPECT_TRUE(zipResult.value().errors.isEmpty());
    const auto name = std::find_if(zipResult.value().roots.begin(), zipResult.value().roots.end(),
                                   [](const auto& node) { return node.name == QStringLiteral("file_name"); });
    ASSERT_NE(name, zipResult.value().roots.end());
    EXPECT_EQ(name->display, QStringLiteral("\"AndroidManifest.xml\""));
}

TEST(StructureTemplate, ShippedTemplateFilesMatchBuiltinsAndLoadFromDisk)
{
    const QVector<StructureTemplate> builtins = builtinStructureTemplates();
    const QString directory = QStringLiteral(PKGSTUDIO_SOURCE_DIR "/templates");
    QStringList errors;
    const auto loaded = loadStructureTemplates(directory, &errors);
    ASSERT_TRUE(loaded) << loaded.error().message.toStdString();
    EXPECT_TRUE(errors.isEmpty()) << errors.join(QStringLiteral("; ")).toStdString();
    ASSERT_EQ(loaded.value().size(), builtins.size());

    for (const StructureTemplate& builtin : builtins) {
        const auto match = std::find_if(loaded.value().begin(), loaded.value().end(),
                                        [&builtin](const auto& structure) {
                                            return structure.id == builtin.id;
                                        });
        ASSERT_NE(match, loaded.value().end()) << builtin.id.toStdString();
        EXPECT_EQ(match->toJson(), builtin.toJson()) << builtin.id.toStdString();
    }
}

TEST(StructureTemplate, LoaderReportsBrokenFilesAndSkipsThem)
{
    QTemporaryDir dir;
    QFile good(dir.filePath(QStringLiteral("good.json")));
    ASSERT_TRUE(good.open(QIODevice::WriteOnly));
    good.write(R"json({"name": "Good", "fields": [{"name": "a", "type": "u8", "offset": 0}]})json");
    good.close();
    QFile bad(dir.filePath(QStringLiteral("bad.json")));
    ASSERT_TRUE(bad.open(QIODevice::WriteOnly));
    bad.write("{ not json");
    bad.close();

    QStringList errors;
    const auto loaded = loadStructureTemplates(dir.path(), &errors);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded.value().size(), 1U);
    EXPECT_EQ(loaded.value().at(0).name, QStringLiteral("Good"));
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_NE(errors.at(0).indexOf(QStringLiteral("bad.json")), -1);
}

TEST(StructureTemplate, RoundTripsThroughJson)
{
    bool ok = false;
    const StructureTemplate structure = parse(QLatin1String(kTemplate), &ok);
    ASSERT_TRUE(ok);

    const auto reparsed = StructureTemplate::fromJson(structure.toJson());
    ASSERT_TRUE(reparsed) << reparsed.error().message.toStdString();
    EXPECT_EQ(reparsed.value().toJson(), structure.toJson());
    EXPECT_EQ(reparsed.value().fields.at(2).count, 2);
    EXPECT_EQ(reparsed.value().fields.at(0).length, 2U);
}
