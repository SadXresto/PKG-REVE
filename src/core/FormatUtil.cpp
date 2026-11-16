#include "core/FormatUtil.h"

#include <QDate>
#include <QTime>
#include <algorithm>

namespace pkg::fmt {

QString hex(quint64 value, int width)
{
    return QStringLiteral("%1").arg(value, width, 16, QLatin1Char('0')).toUpper();
}

QString hexPrefixed(quint64 value, int width)
{
    return QStringLiteral("0x") + hex(value, width);
}

QString humanBytes(quint64 bytes)
{
    static const char* kUnits[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        return QStringLiteral("%1 B").arg(bytes);
    return QStringLiteral("%1 %2").arg(value, 0, 'f', 2).arg(QLatin1String(kUnits[unit]));
}

QString asciiDump(std::span<const std::byte> data)
{
    QString out;
    out.reserve(static_cast<qsizetype>(data.size()));
    for (const auto b : data) {
        const auto c = static_cast<unsigned char>(b);
        out.append((c >= 0x20U && c < 0x7FU) ? QChar(static_cast<char16_t>(c))
                                            : QLatin1Char('.'));
    }
    return out;
}

QString byteDump(std::span<const std::byte> data, quint64 maxBytes, bool ascii)
{
    const quint64 count = std::min<quint64>(maxBytes, data.size());
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(count));
    for (quint64 i = 0; i < count; ++i) {
        parts.append(QStringLiteral("%1").arg(static_cast<unsigned>(data[static_cast<std::size_t>(i)]),
                                              2, 16, QLatin1Char('0')).toUpper());
    }
    QString out = parts.join(QLatin1Char(' '));
    if (ascii) {
        out += QStringLiteral("  |");
        out += asciiDump(data.first(static_cast<std::size_t>(count)));
        out += QLatin1Char('|');
    }
    if (count < data.size())
        out += QStringLiteral(" ...");
    return out;
}

QString printableString(std::span<const std::byte> data)
{
    QString out;
    out.reserve(static_cast<qsizetype>(data.size()));
    for (const auto b : data) {
        const auto c = static_cast<unsigned char>(b);
        if (c == 0)
            continue;
        out.append((c >= 0x20U && c < 0x7FU) ? QChar(static_cast<char16_t>(c))
                                            : QLatin1Char('.'));
    }
    return out;
}

QString dosDateTime(quint16 date, quint16 time)
{
    const int day = date & 0x1F;
    const int month = (date >> 5) & 0x0F;
    const int year = 1980 + ((date >> 9) & 0x7F);
    const int second = (time & 0x1F) * 2;
    const int minute = (time >> 5) & 0x3F;
    const int hour = (time >> 11) & 0x1F;

    const QDate d(year, month, day);
    const QTime t(hour, minute, second);
    if (!d.isValid() || !t.isValid())
        return {};
    return d.toString(QStringLiteral("yyyy-MM-dd")) + QLatin1Char(' ') +
           t.toString(QStringLiteral("HH:mm:ss"));
}

}  // namespace pkg::fmt
