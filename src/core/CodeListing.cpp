#include "core/CodeListing.h"

#include <QChar>
#include <QStringList>

#include <algorithm>

namespace pkg {
namespace {

bool isHexDigit(QChar character)
{
    return (character >= QLatin1Char('0') && character <= QLatin1Char('9'))
        || (character >= QLatin1Char('a') && character <= QLatin1Char('f'))
        || (character >= QLatin1Char('A') && character <= QLatin1Char('F'));
}

int hexValue(QChar character)
{
    if (character >= QLatin1Char('0') && character <= QLatin1Char('9'))
        return character.unicode() - '0';
    if (character >= QLatin1Char('a') && character <= QLatin1Char('f'))
        return character.unicode() - 'a' + 10;
    return character.unicode() - 'A' + 10;
}

QChar printableByte(char value)
{
    const auto byte = static_cast<unsigned char>(value);
    if (byte >= 0x20U && byte <= 0x7EU)
        return QChar(static_cast<ushort>(byte));
    return QLatin1Char('.');
}

}  // namespace

Result<QString> buildListing(const HexDocument& document, quint64 start, quint64 length,
                             int bytesPerRow, const QString& header)
{
    if (!document.isValid())
        return makeError(QStringLiteral("No file is open"));
    const int width = std::clamp(bytesPerRow, 4, 64);
    const quint64 size = document.size();
    if (start > size)
        return makeError(QStringLiteral("Listing start is past the end of the file"),
                         QString::number(start));
    if (length == 0 || start == size)
        return QString();

    const quint64 end = std::min(size, start + std::min(length, size - start));
    const quint64 total = end - start;

    QString text;
    if (!header.isEmpty())
        text += QStringLiteral("# %1\n").arg(header);
    text += QStringLiteral("# offset    bytes%1  ascii\n").arg(QString());
    text.reserve(static_cast<int>(std::min<quint64>(total, 64U * 1024U)) * 5);

    quint64 offset = start;
    while (offset < end) {
        const quint64 rowLength = std::min<quint64>(static_cast<quint64>(width), end - offset);
        auto row = document.readMerged(offset, rowLength);
        if (!row)
            return row.error();

        const QByteArray bytes = row.value();
        text += QStringLiteral("%1  ").arg(offset, 8, 16, QLatin1Char('0')).toUpper();
        for (int i = 0; i < width; ++i) {
            if (i < bytes.size())
                text += QStringLiteral("%1 ").arg(static_cast<quint8>(bytes.at(i)), 2, 16,
                                                  QLatin1Char('0'))
                            .toUpper();
            else
                text += QStringLiteral("   ");
        }
        text += QStringLiteral(" |");
        for (const char byte : bytes)
            text += printableByte(byte);
        text += QLatin1Char('|');
        text += QLatin1Char('\n');
        offset += rowLength;
    }
    return text;
}

Result<QVector<ListingEntry>> parseListing(const QString& text, quint64 fileSize)
{
    QVector<ListingEntry> entries;
    QStringList problems;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int lineNumber = 1; lineNumber <= lines.size(); ++lineNumber) {
        QString line = lines.at(lineNumber - 1).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || line.startsWith(QLatin1Char(';')) || line.startsWith(QLatin1String("```")))
            continue;

        // The ASCII column and trailing comments are not part of the byte data.
        for (const QChar marker : {QLatin1Char('|'), QLatin1Char('#'), QLatin1Char(';')}) {
            const qsizetype index = line.indexOf(marker);
            if (index > 0)
                line = line.left(index).trimmed();
        }
        if (line.isEmpty())
            continue;

        int cursor = 0;
        if (line.startsWith(QLatin1String("0x")) || line.startsWith(QLatin1String("0X")))
            cursor = 2;
        const int offsetStart = cursor;
        while (cursor < line.size() && isHexDigit(line.at(cursor)))
            ++cursor;
        if (cursor == offsetStart) {
            problems << QStringLiteral("line %1: missing offset").arg(lineNumber);
            continue;
        }
        bool offsetOk = false;
        const quint64 offset = line.mid(offsetStart, cursor - offsetStart).toULongLong(&offsetOk, 16);
        if (!offsetOk) {
            problems << QStringLiteral("line %1: invalid offset").arg(lineNumber);
            continue;
        }

        QByteArray bytes;
        while (cursor < line.size()) {
            const QChar character = line.at(cursor);
            if (character.isSpace() || character == QLatin1Char(':')
                || character == QLatin1Char(',')) {
                ++cursor;
                continue;
            }
            if (cursor + 1 >= line.size() || !isHexDigit(character)
                || !isHexDigit(line.at(cursor + 1))) {
                break;  // start of an annotation we do not understand
            }
            const int high = hexValue(character);
            const int low = hexValue(line.at(cursor + 1));
            bytes.append(static_cast<char>((high << 4) | low));
            cursor += 2;
        }

        if (bytes.isEmpty()) {
            problems << QStringLiteral("line %1: no bytes").arg(lineNumber);
            continue;
        }
        if (fileSize > 0 && offset + static_cast<quint64>(bytes.size()) > fileSize) {
            problems << QStringLiteral("line %1: past the end of the file").arg(lineNumber);
            continue;
        }

        if (!entries.isEmpty()) {
            ListingEntry& last = entries.last();
            if (last.offset + static_cast<quint64>(last.bytes.size()) == offset) {
                last.bytes.append(bytes);
                continue;
            }
        }
        ListingEntry entry;
        entry.offset = offset;
        entry.bytes = bytes;
        entries.append(entry);
    }

    if (entries.isEmpty()) {
        QString detail = problems.value(0);
        if (detail.isEmpty())
            detail = QStringLiteral("no 'OFFSET  HEX BYTES' lines were found");
        return makeError(QStringLiteral("Nothing to assemble"), detail);
    }
    return entries;
}

Result<int> applyListing(HexDocument& document, const QVector<ListingEntry>& entries)
{
    if (!document.isValid())
        return makeError(QStringLiteral("No file is open"));
    for (const ListingEntry& entry : entries) {
        if (entry.offset + static_cast<quint64>(entry.bytes.size()) > document.size())
            return makeError(QStringLiteral("Listing writes past the end of the file"),
                             QStringLiteral("offset %1").arg(entry.offset));
    }

    int changed = 0;
    for (const ListingEntry& entry : entries) {
        for (int i = 0; i < entry.bytes.size(); ++i) {
            const quint64 offset = entry.offset + static_cast<quint64>(i);
            if (document.setByte(offset, static_cast<quint8>(entry.bytes.at(i))))
                ++changed;
        }
    }
    return changed;
}

quint64 listingByteCount(const QVector<ListingEntry>& entries)
{
    quint64 total = 0;
    for (const ListingEntry& entry : entries)
        total += static_cast<quint64>(entry.bytes.size());
    return total;
}

}  // namespace pkg
