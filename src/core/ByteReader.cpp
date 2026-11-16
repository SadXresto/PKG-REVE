#include "core/ByteReader.h"

namespace pkg {

Result<quint8> ByteReader::u8At(quint64 offset) const
{
    ByteReader copy = *this;
    if (!copy.seek(offset))
        return makeError(QStringLiteral("Offset outside buffer"));
    return copy.u8();
}

Result<quint16> ByteReader::u16beAt(quint64 offset) const
{
    ByteReader copy = *this;
    if (!copy.seek(offset))
        return makeError(QStringLiteral("Offset outside buffer"));
    return copy.u16be();
}

Result<quint16> ByteReader::u16leAt(quint64 offset) const
{
    ByteReader copy = *this;
    if (!copy.seek(offset))
        return makeError(QStringLiteral("Offset outside buffer"));
    return copy.u16le();
}

Result<quint32> ByteReader::u32beAt(quint64 offset) const
{
    ByteReader copy = *this;
    if (!copy.seek(offset))
        return makeError(QStringLiteral("Offset outside buffer"));
    return copy.u32be();
}

Result<quint32> ByteReader::u32leAt(quint64 offset) const
{
    ByteReader copy = *this;
    if (!copy.seek(offset))
        return makeError(QStringLiteral("Offset outside buffer"));
    return copy.u32le();
}

Result<quint64> ByteReader::u64beAt(quint64 offset) const
{
    ByteReader copy = *this;
    if (!copy.seek(offset))
        return makeError(QStringLiteral("Offset outside buffer"));
    return copy.u64be();
}

Result<quint64> ByteReader::u64leAt(quint64 offset) const
{
    ByteReader copy = *this;
    if (!copy.seek(offset))
        return makeError(QStringLiteral("Offset outside buffer"));
    return copy.u64le();
}

Result<QString> ByteReader::fixedString(quint64 length, quint64 maxLength)
{
    const quint64 capped = qMin(length, maxLength);
    auto raw = bytes(capped);
    if (!raw)
        return raw.error();
    if (length > capped && !skip(length - capped))
        return makeError(QStringLiteral("String extends past end of data"));

    const auto sp = raw.value();
    QByteArray out;
    out.reserve(static_cast<qsizetype>(sp.size()));
    for (const auto b : sp) {
        const auto c = static_cast<char>(b);
        if (c == '\0')
            break;
        out.append(c);
    }
    return QString::fromLatin1(out);
}

}  // namespace pkg
