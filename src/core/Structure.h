// Declarative binary structure templates.
//
// A template describes where fields live and how to interpret them. It is a
// user-authored layout, NOT proof that a file conforms to that layout: the
// evaluator validates field names, offsets, sizes and bounds, but only a
// signature-matched parser can confirm a format.
#pragma once

#include "core/HexDocument.h"
#include "core/Result.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace pkg {

enum class StructureFieldType {
    UInt8,
    Int8,
    UInt16,
    Int16,
    UInt32,
    Int32,
    UInt64,
    Int64,
    Float32,
    Float64,
    Ascii,
    Utf8,
    Bytes,
    Struct,
};

[[nodiscard]] QString structureFieldTypeName(StructureFieldType type);
[[nodiscard]] std::optional<StructureFieldType> structureFieldTypeFromName(const QString& name);
/// Byte width of a fixed-width primitive; nullopt for text/bytes/struct or when
/// the length has to come from the field.
[[nodiscard]] std::optional<quint64> structurePrimitiveSize(StructureFieldType type);

struct StructureField {
    QString name;
    StructureFieldType type = StructureFieldType::UInt8;
    /// Empty means "inherit from the enclosing template/structure".
    std::optional<bool> littleEndian;
    /// Relative to the start of the enclosing structure (or file for roots).
    quint64 offset = 0;
    /// Required for ascii/utf8/bytes; ignored for fixed-width primitives.
    quint64 length = 0;
    /// Array element count. Arrays of structs reuse the same layout per element.
    int count = 1;
    QVector<StructureField> fields;
    QString description;
};

struct StructureTemplate {
    QString id;
    QString name;
    QString description;
    bool littleEndian = true;
    QVector<StructureField> fields;

    [[nodiscard]] static Result<StructureTemplate> fromJson(const QJsonObject& object);
    [[nodiscard]] QJsonObject toJson() const;
};

struct StructureNode {
    QString name;
    QString path;
    QString typeName;
    QString display;
    QString note;
    quint64 offset = 0;
    quint64 length = 0;
    bool error = false;
    QVector<StructureNode> children;
};

struct StructureEvaluation {
    QString templateName;
    QVector<StructureNode> roots;
    QStringList errors;
    QStringList warnings;
    /// Union of valid leaf ranges, used for coverage summaries.
    quint64 coveredBytes = 0;
    int fieldCount = 0;
    bool truncated = false;
    /// True only when every field was evaluated inside the file without errors.
    bool allFieldsInBounds = false;
};

/// Checks the template itself (names, types, lengths, recursion, limits).
/// It cannot and does not prove anything about a particular file.
[[nodiscard]] QStringList validateStructureTemplate(const StructureTemplate& structure);

/// Evaluates `structure` against the live document, including pending edits.
/// Reads are bounded; out-of-bounds fields become error nodes instead of
/// guessed values.
[[nodiscard]] Result<StructureEvaluation> evaluateStructure(const StructureTemplate& structure,
                                                            const HexDocument& document,
                                                            int maxNodes = 4000);

/// Example templates compiled into the application (also shipped in templates/).
[[nodiscard]] QVector<StructureTemplate> builtinStructureTemplates();

/// Loads every *.json template in `directory`. Invalid files are reported in
/// `errors` and skipped; a missing directory is not an error.
[[nodiscard]] Result<QVector<StructureTemplate>> loadStructureTemplates(
    const QString& directory, QStringList* errors = nullptr);

}  // namespace pkg
