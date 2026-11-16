#include "core/Signature.h"

#include <QJsonValue>

namespace pkg {
namespace {

quint64 readU64(const QJsonValue& value)
{
    if (value.isString())
        return value.toString().toULongLong();
    if (value.isDouble()) {
        const double raw = value.toDouble();
        return raw > 0 ? static_cast<quint64>(raw) : 0;
    }
    return 0;
}

}  // namespace

QJsonObject SignatureHit::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("category"), category);
    object.insert(QStringLiteral("offset"), QString::number(offset));
    object.insert(QStringLiteral("length"), QString::number(length));
    object.insert(QStringLiteral("confidence"), confidence);
    object.insert(QStringLiteral("note"), note);
    return object;
}

SignatureHit SignatureHit::fromJson(const QJsonObject& object)
{
    SignatureHit hit;
    hit.name = object.value(QStringLiteral("name")).toString();
    hit.category = object.value(QStringLiteral("category")).toString();
    hit.offset = readU64(object.value(QStringLiteral("offset")));
    hit.length = readU64(object.value(QStringLiteral("length")));
    hit.confidence = object.value(QStringLiteral("confidence")).toInt();
    hit.note = object.value(QStringLiteral("note")).toString();
    return hit;
}

}  // namespace pkg
