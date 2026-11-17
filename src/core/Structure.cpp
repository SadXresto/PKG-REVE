#include "core/Structure.h"

#include "core/SafeMath.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace pkg {
namespace {

constexpr int kMaxDepth = 8;
constexpr int kMaxTemplateFields = 20000;
constexpr int kMaxArrayElements = 4096;
constexpr int kMaxArrayTreeElements = 256;
constexpr quint64 kMaxTextLength = 1024U * 1024U;
constexpr int kMaxDisplayCharacters = 72;

QString hexNumber(quint64 value, int byteWidth)
{
    return QStringLiteral("0x") +
           QString::number(value, 16).rightJustified(byteWidth * 2, QLatin1Char('0')).toUpper();
}

QString hexBytes(const QByteArray& bytes, int maxBytes)
{
    QStringList parts;
    const int shown = std::min<int>(maxBytes, static_cast<int>(bytes.size()));
    for (int i = 0; i < shown; ++i)
        parts << QStringLiteral("%1").arg(static_cast<quint8>(bytes.at(i)), 2, 16,
                                          QLatin1Char('0'))
                         .toUpper();
    QString text = parts.join(QLatin1Char(' '));
    if (bytes.size() > shown)
        text += QStringLiteral(" \u2026");
    return text;
}

QString printableText(const QByteArray& bytes, bool utf8)
{
    QString text = utf8 ? QString::fromUtf8(bytes) : QString::fromLatin1(bytes);
    text.replace(QLatin1Char('\0'), QChar(0x00B7));
    for (QChar& ch : text) {
        if (ch.category() == QChar::Other_Control || ch == QLatin1Char('\n') ||
            ch == QLatin1Char('\r') || ch == QLatin1Char('\t'))
            ch = QChar(0x00B7);
    }
    if (text.size() > kMaxDisplayCharacters)
        text = text.left(kMaxDisplayCharacters) + QStringLiteral("\u2026");
    return QStringLiteral("\"%1\"").arg(text);
}

quint64 assemble(const QByteArray& bytes, bool littleEndian)
{
    quint64 value = 0;
    if (littleEndian) {
        for (int i = bytes.size() - 1; i >= 0; --i)
            value = (value << 8) | static_cast<quint8>(bytes.at(i));
    } else {
        for (char byte : bytes)
            value = (value << 8) | static_cast<quint8>(byte);
    }
    return value;
}

qint64 assembleSigned(const QByteArray& bytes, bool littleEndian)
{
    quint64 value = assemble(bytes, littleEndian);
    const int bits = bytes.size() * 8;
    if (bits < 64 && (value & (quint64{1} << (bits - 1))) != 0)
        value |= (~quint64{0}) << bits;
    return static_cast<qint64>(value);
}

QString formatFloat(quint64 bits, bool wide, bool littleEndian)
{
    QByteArray raw;
    raw.resize(wide ? 8 : 4);
    if (littleEndian) {
        for (int i = 0; i < raw.size(); ++i)
            raw[i] = static_cast<char>((bits >> (8 * i)) & 0xFFU);
    } else {
        for (int i = 0; i < raw.size(); ++i)
            raw[i] = static_cast<char>((bits >> (8 * (raw.size() - 1 - i))) & 0xFFU);
    }
    if (wide) {
        double value = 0.0;
        std::memcpy(&value, raw.constData(), sizeof(value));
        return QString::number(value, 'g', 10);
    }
    float value = 0.0F;
    std::memcpy(&value, raw.constData(), sizeof(value));
    return QString::number(value, 'g', 8);
}

struct Decoded {
    QString display;
    QString note;
};

Decoded decodePrimitive(StructureFieldType type, const QByteArray& bytes, bool littleEndian)
{
    const int width = static_cast<int>(bytes.size());
    switch (type) {
    case StructureFieldType::UInt8:
    case StructureFieldType::UInt16:
    case StructureFieldType::UInt32:
    case StructureFieldType::UInt64: {
        const quint64 value = assemble(bytes, littleEndian);
        if (width == 8)
            return {hexNumber(value, width), QStringLiteral("Decimal: %1").arg(value)};
        return {hexNumber(value, width) + QStringLiteral("  (%1)").arg(value), {}};
    }
    case StructureFieldType::Int8:
    case StructureFieldType::Int16:
    case StructureFieldType::Int32:
    case StructureFieldType::Int64: {
        const qint64 value = assembleSigned(bytes, littleEndian);
        if (width == 8)
            return {hexNumber(assemble(bytes, littleEndian), width),
                    QStringLiteral("Signed decimal: %1").arg(value)};
        return {hexNumber(assemble(bytes, littleEndian), width) +
                    QStringLiteral("  (%1)").arg(value),
                {}};
    }
    case StructureFieldType::Float32:
    case StructureFieldType::Float64: {
        const quint64 bits = assemble(bytes, littleEndian);
        return {formatFloat(bits, type == StructureFieldType::Float64, littleEndian), {}};
    }
    default:
        return {hexBytes(bytes, 16), {}};
    }
}

std::optional<quint64> fieldSpan(const StructureField& field)
{
    if (field.type == StructureFieldType::Struct) {
        std::optional<quint64> span;
        for (const StructureField& child : field.fields) {
            const auto childSpan = fieldSpan(child);
            if (!childSpan)
                return std::nullopt;
            const auto end = safe::add(child.offset, *childSpan);
            if (!end)
                return std::nullopt;
            span = span ? std::max(*span, *end) : *end;
        }
        if (!span)
            return std::nullopt;
        return safe::mul(*span, static_cast<quint64>(std::max(1, field.count)));
    }

    quint64 element = 0;
    if (const auto primitive = structurePrimitiveSize(field.type)) {
        element = *primitive;
    } else {
        element = field.length;
    }
    return safe::mul(element, static_cast<quint64>(std::max(1, field.count)));
}

bool readUnsigned(const QJsonValue& value, quint64* out)
{
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (number < 0.0 || number > 9007199254740992.0 || std::floor(number) != number)
            return false;
        *out = static_cast<quint64>(number);
        return true;
    }
    if (value.isString()) {
        const auto parsed = safe::parseOffset(value.toString());
        if (!parsed)
            return false;
        *out = *parsed;
        return true;
    }
    return false;
}

Result<StructureTemplate> invalid(const QString& detail)
{
    return makeError(QStringLiteral("Invalid structure template"), detail);
}

bool parseEndianness(const QJsonValue& value, bool* littleEndian)
{
    if (!value.isString())
        return false;
    const QString text = value.toString();
    if (text.compare(QLatin1String("little"), Qt::CaseInsensitive) == 0 ||
        text.compare(QLatin1String("le"), Qt::CaseInsensitive) == 0) {
        *littleEndian = true;
        return true;
    }
    if (text.compare(QLatin1String("big"), Qt::CaseInsensitive) == 0 ||
        text.compare(QLatin1String("be"), Qt::CaseInsensitive) == 0) {
        *littleEndian = false;
        return true;
    }
    return false;
}

struct ParseState {
    QStringList errors;
    int fields = 0;
};

bool parseField(const QJsonObject& object, int depth, const QString& parentPath,
                StructureField* field, ParseState* state)
{
    const QString path = parentPath.isEmpty()
                             ? object.value(QStringLiteral("name")).toString()
                             : parentPath + QLatin1Char('.') +
                                   object.value(QStringLiteral("name")).toString();
    if (depth > kMaxDepth) {
        state->errors << QStringLiteral("%1: nesting deeper than %2 levels").arg(path).arg(kMaxDepth);
        return false;
    }
    if (++state->fields > kMaxTemplateFields) {
        state->errors << QStringLiteral("%1: template has more than %2 fields")
                             .arg(path)
                             .arg(kMaxTemplateFields);
        return false;
    }

    field->name = object.value(QStringLiteral("name")).toString().trimmed();
    if (field->name.isEmpty()) {
        state->errors << QStringLiteral("%1: every field needs a non-empty name").arg(path);
        return false;
    }
    field->description = object.value(QStringLiteral("description")).toString();

    const QString typeName = object.value(QStringLiteral("type")).toString();
    const auto type = structureFieldTypeFromName(typeName);
    if (!type) {
        state->errors << QStringLiteral("%1: unknown type '%2'").arg(path, typeName);
        return false;
    }
    field->type = *type;

    if (object.contains(QStringLiteral("endianness"))) {
        bool little = true;
        if (!parseEndianness(object.value(QStringLiteral("endianness")), &little)) {
            state->errors << QStringLiteral("%1: endianness must be little or big").arg(path);
            return false;
        }
        field->littleEndian = little;
    }

    if (!object.contains(QStringLiteral("offset"))) {
        state->errors << QStringLiteral("%1: missing offset").arg(path);
        return false;
    }
    if (!readUnsigned(object.value(QStringLiteral("offset")), &field->offset)) {
        state->errors << QStringLiteral("%1: offset must be a non-negative number or 0x string")
                             .arg(path);
        return false;
    }

    if (object.contains(QStringLiteral("count"))) {
        quint64 count = 1;
        if (!readUnsigned(object.value(QStringLiteral("count")), &count) || count == 0 ||
            count > static_cast<quint64>(kMaxArrayElements)) {
            state->errors << QStringLiteral("%1: count must be between 1 and %2")
                                 .arg(path)
                                 .arg(kMaxArrayElements);
            return false;
        }
        field->count = static_cast<int>(count);
    }

    if (field->type == StructureFieldType::Struct) {
        const QJsonValue children = object.value(QStringLiteral("fields"));
        if (!children.isArray() || children.toArray().isEmpty()) {
            state->errors << QStringLiteral("%1: a struct needs a non-empty fields array").arg(path);
            return false;
        }
        for (const QJsonValue& childValue : children.toArray()) {
            if (!childValue.isObject()) {
                state->errors << QStringLiteral("%1: fields must be objects").arg(path);
                return false;
            }
            StructureField child;
            if (!parseField(childValue.toObject(), depth + 1, path, &child, state))
                return false;
            field->fields.append(child);
        }
    } else if (field->type == StructureFieldType::Ascii ||
               field->type == StructureFieldType::Utf8 ||
               field->type == StructureFieldType::Bytes) {
        if (!object.contains(QStringLiteral("length")) ||
            !readUnsigned(object.value(QStringLiteral("length")), &field->length) ||
            field->length == 0 || field->length > kMaxTextLength) {
            state->errors << QStringLiteral("%1: length must be between 1 and %2 bytes")
                                 .arg(path)
                                 .arg(kMaxTextLength);
            return false;
        }
    }

    return true;
}

QJsonValue offsetJson(quint64 offset)
{
    return QJsonValue(static_cast<double>(offset));
}

QJsonObject fieldToJson(const StructureField& field)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), field.name);
    object.insert(QStringLiteral("type"), structureFieldTypeName(field.type));
    object.insert(QStringLiteral("offset"), offsetJson(field.offset));
    if (field.littleEndian)
        object.insert(QStringLiteral("endianness"),
                      *field.littleEndian ? QStringLiteral("little") : QStringLiteral("big"));
    if (field.count != 1)
        object.insert(QStringLiteral("count"), field.count);
    if (!field.description.isEmpty())
        object.insert(QStringLiteral("description"), field.description);
    if (field.type == StructureFieldType::Struct) {
        QJsonArray children;
        for (const StructureField& child : field.fields)
            children.append(fieldToJson(child));
        object.insert(QStringLiteral("fields"), children);
    } else if (field.type == StructureFieldType::Ascii ||
               field.type == StructureFieldType::Utf8 ||
               field.type == StructureFieldType::Bytes) {
        object.insert(QStringLiteral("length"), offsetJson(field.length));
    }
    return object;
}

struct EvalContext {
    const HexDocument& document;
    StructureEvaluation* result = nullptr;
    int nodes = 0;
    int maxNodes = 4000;
    QVector<QPair<quint64, quint64>> leafRanges;

    [[nodiscard]] bool budgetExhausted()
    {
        if (nodes < maxNodes)
            return false;
        if (!result->truncated) {
            result->truncated = true;
            result->warnings << QStringLiteral("Stopped after %1 fields; the template is larger "
                                               "than the display limit.")
                                    .arg(maxNodes);
        }
        return true;
    }
};

QString effectiveTypeName(const StructureField& field)
{
    if (field.type == StructureFieldType::Struct)
        return field.count > 1 ? QStringLiteral("struct[%1]").arg(field.count)
                               : QStringLiteral("struct");
    QString base = structureFieldTypeName(field.type);
    if (field.count > 1)
        base += QStringLiteral("[%1]").arg(field.count);
    return base;
}

std::optional<StructureNode> evaluateField(const StructureField& field, quint64 base,
                                           bool inheritedLittleEndian, const QString& parentPath,
                                           int depth, EvalContext* context);

std::optional<StructureNode> makeErrorNode(const QString& name, const QString& path,
                                           const QString& typeName, quint64 offset, quint64 length,
                                           const QString& message)
{
    StructureNode node;
    node.name = name;
    node.path = path;
    node.typeName = typeName;
    node.offset = offset;
    node.length = length;
    node.error = true;
    node.display = QStringLiteral("not read");
    node.note = message;
    return node;
}

std::optional<StructureNode> evaluateStruct(const StructureField& field, quint64 absolute,
                                            bool littleEndian, const QString& path, int depth,
                                            EvalContext* context)
{
    StructureNode node;
    node.name = field.name;
    node.path = path;
    node.typeName = effectiveTypeName(field);
    node.offset = absolute;

    const int elements = std::max(1, field.count);
    const auto singleSpan = fieldSpan(field);
    if (!singleSpan || *singleSpan == 0) {
        context->result->errors << QStringLiteral("%1: struct has no measurable size").arg(path);
        return makeErrorNode(field.name, path, node.typeName, absolute, 0,
                             QStringLiteral("Nested structure has no fields or its size overflows"));
    }
    const quint64 elementSpan = *singleSpan / static_cast<quint64>(elements);
    const auto totalSpan = safe::mul(elementSpan, static_cast<quint64>(elements));
    if (!totalSpan) {
        context->result->errors << QStringLiteral("%1: size overflows").arg(path);
        return makeErrorNode(field.name, path, node.typeName, absolute, 0,
                             QStringLiteral("Structure size overflows 64-bit offsets"));
    }
    node.length = *totalSpan;
    node.display = QStringLiteral("structure, %1 byte(s)").arg(*totalSpan);
    node.note = field.description;

    if (!safe::inRange(absolute, node.length, context->document.size())) {
        context->result->errors
            << QStringLiteral("%1: bytes 0x%2-0x%3 extend past the end of the file (0x%4)")
                   .arg(path)
                   .arg(absolute, 0, 16)
                   .arg(absolute + node.length - 1, 0, 16)
                   .arg(context->document.size(), 0, 16);
        node.error = true;
        node.display = QStringLiteral("not read");
        node.note = QStringLiteral("Outside the file: needs %1 byte(s) from 0x%2, file has %3.")
                        .arg(node.length)
                        .arg(absolute, 0, 16)
                        .arg(context->document.size());
        return node;
    }

    const int shown = std::min(elements, kMaxArrayTreeElements);
    if (elements > shown) {
        context->result->warnings
            << QStringLiteral("%1: showing the first %2 of %3 elements").arg(path).arg(shown).arg(elements);
        context->result->truncated = true;
    }
    for (int index = 0; index < shown; ++index) {
        if (context->budgetExhausted())
            break;
        ++context->nodes;
        const quint64 elementBase = absolute + elementSpan * static_cast<quint64>(index);
        StructureNode element;
        element.typeName = QStringLiteral("struct");
        element.offset = elementBase;
        element.length = elementSpan;
        element.display = QStringLiteral("structure, %1 byte(s)").arg(elementSpan);
        if (elements > 1) {
            element.name = QStringLiteral("%1[%2]").arg(field.name).arg(index);
            element.path = QStringLiteral("%1[%2]").arg(path).arg(index);
        } else {
            element.name = field.name;
            element.path = path;
        }
        for (const StructureField& child : field.fields) {
            if (context->budgetExhausted())
                break;
            auto childNode = evaluateField(child, elementBase, littleEndian, element.path,
                                           depth + 1, context);
            if (childNode)
                element.children.append(*childNode);
        }
        if (elements > 1) {
            node.children.append(element);
        } else {
            node.display = element.display;
            node.children = element.children;
        }
    }
    return node;
}

std::optional<StructureNode> evaluateLeaf(const StructureField& field, quint64 absolute,
                                          bool littleEndian, const QString& path,
                                          EvalContext* context)
{
    const quint64 elementSize = *structurePrimitiveSize(field.type);
    const int elements = std::max(1, field.count);
    const auto total = safe::mul(elementSize, static_cast<quint64>(elements));
    if (!total)
        return makeErrorNode(field.name, path, effectiveTypeName(field), absolute, 0,
                             QStringLiteral("Field size overflows 64-bit offsets"));

    StructureNode node;
    node.name = field.name;
    node.path = path;
    node.typeName = effectiveTypeName(field);
    node.offset = absolute;
    node.length = *total;
    node.note = field.description;

    if (!safe::inRange(absolute, node.length, context->document.size())) {
        context->result->errors
            << QStringLiteral("%1: bytes 0x%2-0x%3 extend past the end of the file (0x%4)")
                   .arg(path)
                   .arg(absolute, 0, 16)
                   .arg(absolute + node.length - 1, 0, 16)
                   .arg(context->document.size(), 0, 16);
        node.error = true;
        node.display = QStringLiteral("not read");
        node.note = QStringLiteral("Outside the file: needs %1 byte(s) from 0x%2, file has %3.")
                        .arg(node.length)
                        .arg(absolute, 0, 16)
                        .arg(context->document.size());
        return node;
    }

    const int shown = std::min(elements, kMaxArrayTreeElements);
    if (elements > shown) {
        context->result->warnings
            << QStringLiteral("%1: showing the first %2 of %3 elements").arg(path).arg(shown).arg(elements);
        context->result->truncated = true;
    }

    if (elements == 1) {
        auto bytes = context->document.readMerged(absolute, elementSize);
        if (!bytes) {
            node.error = true;
            node.display = QStringLiteral("not read");
            node.note = bytes.error().detail.isEmpty() ? bytes.error().message
                                                        : bytes.error().detail;
            return node;
        }
        const Decoded decoded = decodePrimitive(field.type, bytes.value(), littleEndian);
        node.display = decoded.display;
        if (!decoded.note.isEmpty())
            node.note = node.note.isEmpty() ? decoded.note
                                            : node.note + QStringLiteral("\n") + decoded.note;
        context->leafRanges.append({absolute, elementSize});
        return node;
    }

    node.display = QStringLiteral("%1 element(s)").arg(elements);
    for (int index = 0; index < shown; ++index) {
        if (context->budgetExhausted())
            break;
        ++context->nodes;
        const quint64 elementOffset = absolute + elementSize * static_cast<quint64>(index);
        StructureNode child;
        child.name = QStringLiteral("%1[%2]").arg(field.name).arg(index);
        child.path = QStringLiteral("%1[%2]").arg(path).arg(index);
        child.typeName = structureFieldTypeName(field.type);
        child.offset = elementOffset;
        child.length = elementSize;
        auto bytes = context->document.readMerged(elementOffset, elementSize);
        if (!bytes) {
            child.error = true;
            child.display = QStringLiteral("not read");
            child.note = bytes.error().message;
        } else {
            const Decoded decoded = decodePrimitive(field.type, bytes.value(), littleEndian);
            child.display = decoded.display;
            context->leafRanges.append({elementOffset, elementSize});
        }
        node.children.append(child);
    }
    return node;
}

std::optional<StructureNode> evaluateTextField(const StructureField& field, quint64 absolute,
                                               const QString& path, EvalContext* context)
{
    const int elements = std::max(1, field.count);
    const auto total = safe::mul(field.length, static_cast<quint64>(elements));
    if (!total)
        return makeErrorNode(field.name, path, effectiveTypeName(field), absolute, 0,
                             QStringLiteral("Field size overflows 64-bit offsets"));

    StructureNode node;
    node.name = field.name;
    node.path = path;
    node.typeName = effectiveTypeName(field);
    node.offset = absolute;
    node.length = *total;
    node.display = QStringLiteral("%1 byte(s)").arg(node.length);
    node.note = field.description;

    if (!safe::inRange(absolute, node.length, context->document.size())) {
        context->result->errors
            << QStringLiteral("%1: bytes 0x%2-0x%3 extend past the end of the file (0x%4)")
                   .arg(path)
                   .arg(absolute, 0, 16)
                   .arg(absolute + node.length - 1, 0, 16)
                   .arg(context->document.size(), 0, 16);
        node.error = true;
        node.display = QStringLiteral("not read");
        node.note = QStringLiteral("Outside the file: needs %1 byte(s) from 0x%2, file has %3.")
                        .arg(node.length)
                        .arg(absolute, 0, 16)
                        .arg(context->document.size());
        return node;
    }

    const bool isBytes = field.type == StructureFieldType::Bytes;
    const int shown = std::min(elements, kMaxArrayTreeElements);
    if (elements > shown) {
        context->result->warnings
            << QStringLiteral("%1: showing the first %2 of %3 elements").arg(path).arg(shown).arg(elements);
        context->result->truncated = true;
    }
    if (elements == 1) {
        auto bytes = context->document.readMerged(absolute, field.length);
        if (!bytes) {
            node.error = true;
            node.display = QStringLiteral("not read");
            node.note = bytes.error().message;
            return node;
        }
        node.display = isBytes ? hexBytes(bytes.value(), 32)
                               : printableText(bytes.value(),
                                               field.type == StructureFieldType::Utf8);
        context->leafRanges.append({absolute, field.length});
        return node;
    }

    node.display = QStringLiteral("%1 element(s)").arg(elements);
    for (int index = 0; index < shown; ++index) {
        if (context->budgetExhausted())
            break;
        ++context->nodes;
        const quint64 elementOffset = absolute + field.length * static_cast<quint64>(index);
        StructureNode child;
        child.name = QStringLiteral("%1[%2]").arg(field.name).arg(index);
        child.path = QStringLiteral("%1[%2]").arg(path).arg(index);
        child.typeName = structureFieldTypeName(field.type);
        child.offset = elementOffset;
        child.length = field.length;
        auto bytes = context->document.readMerged(elementOffset, field.length);
        if (!bytes) {
            child.error = true;
            child.display = QStringLiteral("not read");
            child.note = bytes.error().message;
        } else {
            child.display = isBytes ? hexBytes(bytes.value(), 24)
                                    : printableText(bytes.value(),
                                                    field.type == StructureFieldType::Utf8);
            context->leafRanges.append({elementOffset, field.length});
        }
        node.children.append(child);
    }
    return node;
}

std::optional<StructureNode> evaluateField(const StructureField& field, quint64 base,
                                           bool inheritedLittleEndian, const QString& parentPath,
                                           int depth, EvalContext* context)
{
    const QString path =
        parentPath.isEmpty() ? field.name : parentPath + QLatin1Char('.') + field.name;
    if (depth > kMaxDepth) {
        context->result->errors << QStringLiteral("%1: nesting deeper than %2 levels")
                                       .arg(path)
                                       .arg(kMaxDepth);
        return std::nullopt;
    }
    const auto absolute = safe::add(base, field.offset);
    if (!absolute) {
        context->result->errors << QStringLiteral("%1: offset overflows").arg(path);
        return makeErrorNode(field.name, path, effectiveTypeName(field), 0, 0,
                             QStringLiteral("Offset addition overflows 64-bit offsets"));
    }
    const bool littleEndian = field.littleEndian.value_or(inheritedLittleEndian);

    if (field.type == StructureFieldType::Struct)
        return evaluateStruct(field, *absolute, littleEndian, path, depth, context);
    if (field.type == StructureFieldType::Ascii ||
        field.type == StructureFieldType::Utf8 ||
        field.type == StructureFieldType::Bytes)
        return evaluateTextField(field, *absolute, path, context);
    return evaluateLeaf(field, *absolute, littleEndian, path, context);
}

void validateFields(const QVector<StructureField>& fields, const QString& parentPath, int depth,
                    QSet<QString>* names, QStringList* errors, int* count)
{
    if (depth > kMaxDepth) {
        errors->append(QStringLiteral("%1: nesting deeper than %2 levels")
                           .arg(parentPath.isEmpty() ? QStringLiteral("<root>") : parentPath)
                           .arg(kMaxDepth));
        return;
    }
    QSet<QString> local = *names;
    for (const StructureField& field : fields) {
        if (++*count > kMaxTemplateFields) {
            errors->append(QStringLiteral("Template has more than %1 fields").arg(kMaxTemplateFields));
            return;
        }
        const QString path =
            parentPath.isEmpty() ? field.name : parentPath + QLatin1Char('.') + field.name;
        if (field.name.trimmed().isEmpty()) {
            errors->append(QStringLiteral("%1: field name is empty").arg(path));
            continue;
        }
        if (local.contains(field.name.toLower()))
            errors->append(QStringLiteral("%1: duplicate sibling field name").arg(path));
        local.insert(field.name.toLower());

        const bool isText = field.type == StructureFieldType::Ascii ||
                            field.type == StructureFieldType::Utf8 ||
                            field.type == StructureFieldType::Bytes;
        if (isText && field.length == 0)
            errors->append(QStringLiteral("%1: text and bytes fields need a length").arg(path));
        if (field.count < 1 || field.count > kMaxArrayElements) {
            errors->append(QStringLiteral("%1: count must be between 1 and %2")
                               .arg(path)
                               .arg(kMaxArrayElements));
        }
        if (field.type == StructureFieldType::Struct) {
            if (field.fields.isEmpty()) {
                errors->append(QStringLiteral("%1: struct has no fields").arg(path));
                continue;
            }
            if (field.count > kMaxArrayTreeElements)
                errors->append(QStringLiteral("%1: struct arrays are limited to %2 elements")
                                   .arg(path)
                                   .arg(kMaxArrayTreeElements));
            QSet<QString> childNames;
            validateFields(field.fields, path, depth + 1, &childNames, errors, count);
        } else if (!structurePrimitiveSize(field.type) && !isText) {
            errors->append(QStringLiteral("%1: type has no known size").arg(path));
        }
    }
}

QString builtinElfJson()
{
    return QStringLiteral(R"json({
  "format": "pkgstudio.structure",
  "version": 1,
  "id": "builtin.elf64-le-header",
  "name": "ELF64 header (little-endian)",
  "description": "Example layout for the 64-byte ELF header, as used by the sample ELF. Offsets follow the ELF specification; this template does not verify that a file is an ELF.",
  "endianness": "little",
  "fields": [
    {"name": "e_ident_magic", "type": "bytes", "offset": 0, "length": 4, "description": "0x7F 'E' 'L' 'F' for ELF files."},
    {"name": "ei_class", "type": "u8", "offset": 4, "description": "1 = 32-bit, 2 = 64-bit."},
    {"name": "ei_data", "type": "u8", "offset": 5, "description": "1 = little-endian, 2 = big-endian."},
    {"name": "ei_version", "type": "u8", "offset": 6},
    {"name": "ei_osabi", "type": "u8", "offset": 7},
    {"name": "ei_abiversion", "type": "u8", "offset": 8},
    {"name": "ei_pad", "type": "bytes", "offset": 9, "length": 7},
    {"name": "e_type", "type": "u16", "offset": 16},
    {"name": "e_machine", "type": "u16", "offset": 18},
    {"name": "e_version", "type": "u32", "offset": 20},
    {"name": "e_entry", "type": "u64", "offset": 24},
    {"name": "e_phoff", "type": "u64", "offset": 32},
    {"name": "e_shoff", "type": "u64", "offset": 40},
    {"name": "e_flags", "type": "u32", "offset": 48},
    {"name": "e_ehsize", "type": "u16", "offset": 52},
    {"name": "e_phentsize", "type": "u16", "offset": 54},
    {"name": "e_phnum", "type": "u16", "offset": 56},
    {"name": "e_shentsize", "type": "u16", "offset": 58},
    {"name": "e_shnum", "type": "u16", "offset": 60},
    {"name": "e_shstrndx", "type": "u16", "offset": 62}
  ]
})json");
}

QString builtinZipJson()
{
    return QStringLiteral(R"json({
  "format": "pkgstudio.structure",
  "version": 1,
  "id": "builtin.zip-local-header",
  "name": "ZIP local file header (sample entry)",
  "description": "Local file header of the first ZIP/APK entry. The file name length is fixed at 19 bytes, which matches 'AndroidManifest.xml' in the bundled sample APK; other archives will need their own length.",
  "endianness": "little",
  "fields": [
    {"name": "signature", "type": "bytes", "offset": 0, "length": 4, "description": "0x04034B50."},
    {"name": "version_needed", "type": "u16", "offset": 4},
    {"name": "flags", "type": "u16", "offset": 6},
    {"name": "method", "type": "u16", "offset": 8},
    {"name": "mod_time", "type": "u16", "offset": 10},
    {"name": "mod_date", "type": "u16", "offset": 12},
    {"name": "crc32", "type": "u32", "offset": 14},
    {"name": "compressed_size", "type": "u32", "offset": 18},
    {"name": "uncompressed_size", "type": "u32", "offset": 22},
    {"name": "name_length", "type": "u16", "offset": 26},
    {"name": "extra_length", "type": "u16", "offset": 28},
    {"name": "file_name", "type": "ascii", "offset": 30, "length": 19, "description": "Fixed-size example: read name_length bytes for other archives."}
  ]
})json");
}

QString builtinPngJson()
{
    return QStringLiteral(R"json({
  "format": "pkgstudio.structure",
  "version": 1,
  "id": "builtin.png-ihdr",
  "name": "PNG signature and IHDR chunk",
  "description": "Big-endian example covering the PNG signature and the first chunk header/IHDR fields.",
  "endianness": "big",
  "fields": [
    {"name": "signature", "type": "bytes", "offset": 0, "length": 8},
    {"name": "chunk_length", "type": "u32", "offset": 8, "description": "Must be 13 for IHDR."},
    {"name": "chunk_type", "type": "ascii", "offset": 12, "length": 4},
    {"name": "width", "type": "u32", "offset": 16},
    {"name": "height", "type": "u32", "offset": 20},
    {"name": "bit_depth", "type": "u8", "offset": 24},
    {"name": "color_type", "type": "u8", "offset": 25},
    {"name": "compression", "type": "u8", "offset": 26},
    {"name": "filter", "type": "u8", "offset": 27},
    {"name": "interlace", "type": "u8", "offset": 28},
    {"name": "crc32", "type": "u32", "offset": 29}
  ]
})json");
}

Result<StructureTemplate> parseBuiltin(const QString& json, const QString& label)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
        return makeError(QStringLiteral("Built-in template failed to parse"), label);
    auto structure = StructureTemplate::fromJson(document.object());
    if (!structure)
        return makeError(QStringLiteral("Built-in template is invalid"),
                         label + QStringLiteral(": ") + structure.error().detail);
    return structure;
}

}  // namespace

QString structureFieldTypeName(StructureFieldType type)
{
    switch (type) {
    case StructureFieldType::UInt8: return QStringLiteral("u8");
    case StructureFieldType::Int8: return QStringLiteral("i8");
    case StructureFieldType::UInt16: return QStringLiteral("u16");
    case StructureFieldType::Int16: return QStringLiteral("i16");
    case StructureFieldType::UInt32: return QStringLiteral("u32");
    case StructureFieldType::Int32: return QStringLiteral("i32");
    case StructureFieldType::UInt64: return QStringLiteral("u64");
    case StructureFieldType::Int64: return QStringLiteral("i64");
    case StructureFieldType::Float32: return QStringLiteral("f32");
    case StructureFieldType::Float64: return QStringLiteral("f64");
    case StructureFieldType::Ascii: return QStringLiteral("ascii");
    case StructureFieldType::Utf8: return QStringLiteral("utf8");
    case StructureFieldType::Bytes: return QStringLiteral("bytes");
    case StructureFieldType::Struct: return QStringLiteral("struct");
    }
    return QStringLiteral("u8");
}

std::optional<StructureFieldType> structureFieldTypeFromName(const QString& name)
{
    const QString lower = name.trimmed().toLower();
    static const QHash<QString, StructureFieldType> types{
        {QStringLiteral("u8"), StructureFieldType::UInt8},
        {QStringLiteral("uint8"), StructureFieldType::UInt8},
        {QStringLiteral("byte"), StructureFieldType::UInt8},
        {QStringLiteral("i8"), StructureFieldType::Int8},
        {QStringLiteral("int8"), StructureFieldType::Int8},
        {QStringLiteral("u16"), StructureFieldType::UInt16},
        {QStringLiteral("uint16"), StructureFieldType::UInt16},
        {QStringLiteral("i16"), StructureFieldType::Int16},
        {QStringLiteral("int16"), StructureFieldType::Int16},
        {QStringLiteral("u32"), StructureFieldType::UInt32},
        {QStringLiteral("uint32"), StructureFieldType::UInt32},
        {QStringLiteral("i32"), StructureFieldType::Int32},
        {QStringLiteral("int32"), StructureFieldType::Int32},
        {QStringLiteral("u64"), StructureFieldType::UInt64},
        {QStringLiteral("uint64"), StructureFieldType::UInt64},
        {QStringLiteral("i64"), StructureFieldType::Int64},
        {QStringLiteral("int64"), StructureFieldType::Int64},
        {QStringLiteral("f32"), StructureFieldType::Float32},
        {QStringLiteral("float"), StructureFieldType::Float32},
        {QStringLiteral("f64"), StructureFieldType::Float64},
        {QStringLiteral("double"), StructureFieldType::Float64},
        {QStringLiteral("ascii"), StructureFieldType::Ascii},
        {QStringLiteral("utf8"), StructureFieldType::Utf8},
        {QStringLiteral("bytes"), StructureFieldType::Bytes},
        {QStringLiteral("struct"), StructureFieldType::Struct},
    };
    const auto it = types.constFind(lower);
    return it == types.constEnd() ? std::nullopt : std::optional<StructureFieldType>(*it);
}

std::optional<quint64> structurePrimitiveSize(StructureFieldType type)
{
    switch (type) {
    case StructureFieldType::UInt8:
    case StructureFieldType::Int8:
        return 1;
    case StructureFieldType::UInt16:
    case StructureFieldType::Int16:
        return 2;
    case StructureFieldType::UInt32:
    case StructureFieldType::Int32:
    case StructureFieldType::Float32:
        return 4;
    case StructureFieldType::UInt64:
    case StructureFieldType::Int64:
    case StructureFieldType::Float64:
        return 8;
    default:
        return std::nullopt;
    }
}

Result<StructureTemplate> StructureTemplate::fromJson(const QJsonObject& object)
{
    if (object.contains(QStringLiteral("format")) &&
        object.value(QStringLiteral("format")).toString() !=
            QStringLiteral("pkgstudio.structure")) {
        return invalid(QStringLiteral("format must be 'pkgstudio.structure'"));
    }
    if (object.contains(QStringLiteral("version"))) {
        const double version = object.value(QStringLiteral("version")).toDouble(0);
        if (version != 1.0)
            return invalid(QStringLiteral("unsupported template version %1 (expected 1)")
                               .arg(object.value(QStringLiteral("version")).toVariant().toString()));
    }

    StructureTemplate structure;
    structure.name = object.value(QStringLiteral("name")).toString().trimmed();
    if (structure.name.isEmpty())
        return invalid(QStringLiteral("the template needs a name"));
    structure.id = object.value(QStringLiteral("id")).toString().trimmed();
    if (structure.id.isEmpty())
        structure.id = structure.name;
    structure.description = object.value(QStringLiteral("description")).toString();
    if (object.contains(QStringLiteral("endianness"))) {
        bool little = true;
        if (!parseEndianness(object.value(QStringLiteral("endianness")), &little))
            return invalid(QStringLiteral("endianness must be 'little' or 'big'"));
        structure.littleEndian = little;
    }

    const QJsonValue fields = object.value(QStringLiteral("fields"));
    if (!fields.isArray() || fields.toArray().isEmpty())
        return invalid(QStringLiteral("the template needs a non-empty fields array"));

    ParseState state;
    for (const QJsonValue& value : fields.toArray()) {
        if (!value.isObject()) {
            state.errors << QStringLiteral("fields must contain objects");
            break;
        }
        StructureField field;
        if (!parseField(value.toObject(), 1, QString(), &field, &state))
            break;
        structure.fields.append(field);
    }
    if (!state.errors.isEmpty())
        return makeError(QStringLiteral("Invalid structure template"),
                         state.errors.join(QLatin1Char('\n')));

    const QStringList problems = validateStructureTemplate(structure);
    if (!problems.isEmpty())
        return makeError(QStringLiteral("Invalid structure template"),
                         problems.join(QLatin1Char('\n')));
    return structure;
}

QJsonObject StructureTemplate::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("format"), QStringLiteral("pkgstudio.structure"));
    object.insert(QStringLiteral("version"), 1);
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    if (!description.isEmpty())
        object.insert(QStringLiteral("description"), description);
    object.insert(QStringLiteral("endianness"),
                  littleEndian ? QStringLiteral("little") : QStringLiteral("big"));
    QJsonArray array;
    for (const StructureField& field : fields)
        array.append(fieldToJson(field));
    object.insert(QStringLiteral("fields"), array);
    return object;
}

QStringList validateStructureTemplate(const StructureTemplate& structure)
{
    QStringList errors;
    if (structure.name.trimmed().isEmpty())
        errors << QStringLiteral("Template name is empty");
    if (structure.fields.isEmpty()) {
        errors << QStringLiteral("Template has no fields");
        return errors;
    }
    int count = 0;
    QSet<QString> names;
    validateFields(structure.fields, QString(), 1, &names, &errors, &count);
    return errors;
}

Result<StructureEvaluation> evaluateStructure(const StructureTemplate& structure,
                                              const HexDocument& document, int maxNodes)
{
    if (!document.isValid())
        return makeError(QStringLiteral("No document is open"));
    const QStringList problems = validateStructureTemplate(structure);
    if (!problems.isEmpty())
        return makeError(QStringLiteral("Invalid structure template"),
                         problems.join(QLatin1Char('\n')));

    StructureEvaluation result;
    result.templateName = structure.name;
    EvalContext context{document, &result};
    context.maxNodes = std::max(1, maxNodes);

    for (const StructureField& field : structure.fields) {
        if (context.budgetExhausted())
            break;
        ++context.nodes;
        auto node = evaluateField(field, 0, structure.littleEndian, QString(), 1, &context);
        if (node) {
            result.roots.append(*node);
            ++result.fieldCount;
        }
    }

    // Union of valid leaf ranges, sorted and merged.
    auto ranges = context.leafRanges;
    std::sort(ranges.begin(), ranges.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    quint64 covered = 0;
    quint64 currentStart = 0;
    quint64 currentEnd = 0;
    bool haveRange = false;
    for (const auto& range : ranges) {
        const quint64 end = range.first + range.second;
        if (!haveRange || range.first > currentEnd) {
            if (haveRange)
                covered += currentEnd - currentStart;
            currentStart = range.first;
            currentEnd = end;
            haveRange = true;
        } else {
            currentEnd = std::max(currentEnd, end);
        }
    }
    if (haveRange)
        covered += currentEnd - currentStart;
    result.coveredBytes = covered;
    result.allFieldsInBounds = result.errors.isEmpty() && !result.truncated;
    return result;
}

QVector<StructureTemplate> builtinStructureTemplates()
{
    QVector<StructureTemplate> templates;
    for (const auto& [json, label] :
         {std::pair{builtinElfJson(), QStringLiteral("ELF64")},
          std::pair{builtinZipJson(), QStringLiteral("ZIP")},
          std::pair{builtinPngJson(), QStringLiteral("PNG")}}) {
        auto structure = parseBuiltin(json, label);
        if (structure)
            templates.append(structure.value());
    }
    return templates;
}

Result<QVector<StructureTemplate>> loadStructureTemplates(const QString& directory,
                                                          QStringList* errors)
{
    QVector<StructureTemplate> templates;
    QDir dir(directory);
    if (!dir.exists())
        return templates;

    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString& file : files) {
        QFile handle(dir.filePath(file));
        if (!handle.open(QIODevice::ReadOnly)) {
            if (errors)
                *errors << QStringLiteral("%1: cannot read file").arg(file);
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(handle.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (errors)
                *errors << QStringLiteral("%1: %2").arg(file, parseError.errorString());
            continue;
        }
        auto structure = StructureTemplate::fromJson(document.object());
        if (!structure) {
            if (errors)
                *errors << QStringLiteral("%1: %2").arg(file, structure.error().detail.isEmpty()
                                                                  ? structure.error().message
                                                                  : structure.error().detail);
            continue;
        }
        templates.append(structure.value());
    }
    return templates;
}

}  // namespace pkg
