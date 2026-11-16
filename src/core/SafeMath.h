// Overflow-checked arithmetic used by every parser and offset calculation.
#pragma once

#include <QString>
#include <QtGlobal>
#include <limits>
#include <optional>

namespace pkg::safe {

/// a + b, or std::nullopt when the addition would overflow quint64.
[[nodiscard]] inline std::optional<quint64> add(quint64 a, quint64 b)
{
    if (a > std::numeric_limits<quint64>::max() - b)
        return std::nullopt;
    return a + b;
}

/// a * b, or std::nullopt when the multiplication would overflow quint64.
[[nodiscard]] inline std::optional<quint64> mul(quint64 a, quint64 b)
{
    if (a != 0 && b > std::numeric_limits<quint64>::max() / a)
        return std::nullopt;
    return a * b;
}

/// True when [offset, offset + length) lies entirely inside [0, total).
/// An empty range at `total` is considered valid.
[[nodiscard]] inline bool inRange(quint64 offset, quint64 length, quint64 total)
{
    const auto end = add(offset, length);
    return end.has_value() && *end <= total;
}

/// Saturating cast helper: values that do not fit qint64 are clamped.
[[nodiscard]] inline qint64 toQInt64Clamped(quint64 value)
{
    constexpr quint64 kMax = static_cast<quint64>(std::numeric_limits<qint64>::max());
    return value > kMax ? std::numeric_limits<qint64>::max() : static_cast<qint64>(value);
}

/// Parse a user supplied offset string ("0x1F", "31", "1_000").
[[nodiscard]] inline std::optional<quint64> parseOffset(const QString& text, bool* isHex = nullptr)
{
    QString s = text.trimmed();
    s.remove(QLatin1Char('_'));
    s.remove(QLatin1Char(' '));
    if (s.isEmpty())
        return std::nullopt;

    bool hex = false;
    if (s.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        s = s.mid(2);
        hex = true;
    }
    if (s.isEmpty())
        return std::nullopt;
    if (isHex)
        *isHex = hex;

    bool ok = false;
    const quint64 value = s.toULongLong(&ok, hex ? 16 : 10);
    if (!ok)
        return std::nullopt;
    return value;
}

}  // namespace pkg::safe
