// Small helpers shared by the built-in format parsers.
#pragma once

#include "core/FormatInfo.h"
#include "core/FormatUtil.h"

#include <QString>

#include <span>

namespace pkg::parsers {

inline InfoField field(const QString& name, const QString& value, const QString& note = {})
{
    return InfoField{name, value, note};
}

inline InfoField hexField(const QString& name, quint64 value, int width = 0)
{
    return InfoField{name, QStringLiteral("0x") + fmt::hex(value, width), {}};
}

inline InfoField sizeField(const QString& name, quint64 value)
{
    return InfoField{name, fmt::humanBytes(value),
                     QStringLiteral("%1 bytes").arg(value)};
}

inline InfoField stringField(const QString& name, std::span<const std::byte> data)
{
    const QString text = fmt::printableString(data);
    return InfoField{name, text.isEmpty() ? QStringLiteral("<empty>") : text, {}};
}

/// Machine readable names for the values we are confident about.
[[nodiscard]] QString elfMachineName(quint16 machine);
[[nodiscard]] QString elfTypeName(quint16 type);
[[nodiscard]] QString peMachineName(quint16 machine);
[[nodiscard]] QString peSubsystemName(quint16 subsystem);
[[nodiscard]] QString zipMethodName(quint16 method);

}  // namespace pkg::parsers
