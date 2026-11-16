// Human readable formatting for byte counts, offsets and byte dumps.
#pragma once

#include <QString>
#include <QStringList>
#include <span>
#include <cstddef>

namespace pkg::fmt {

/// Uppercase hexadecimal, zero padded to `width` characters.
[[nodiscard]] QString hex(quint64 value, int width = 0);
/// "0x1F" with width 2 -> "0x001F" is produced by hex(0x1F, 2).
[[nodiscard]] QString hexPrefixed(quint64 value, int width = 0);
/// 1_234_567 -> "1.18 MiB" (binary units, 2 decimals).
[[nodiscard]] QString humanBytes(quint64 bytes);
/// Space separated uppercase byte dump, optional ASCII column.
[[nodiscard]] QString byteDump(std::span<const std::byte> data, quint64 maxBytes = 64,
                               bool ascii = true);
/// Printable ASCII rendering, '.' for non printable bytes.
[[nodiscard]] QString asciiDump(std::span<const std::byte> data);
/// Removes trailing NULs and non printable characters from fixed size fields.
[[nodiscard]] QString printableString(std::span<const std::byte> data);
/// Date from a DOS date/time pair, or an empty string when invalid.
[[nodiscard]] QString dosDateTime(quint16 date, quint16 time);

}  // namespace pkg::fmt
