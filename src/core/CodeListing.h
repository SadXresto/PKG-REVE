// Human-readable byte listings that round-trip back into bytes.
//
// A listing is a plain-text, editable representation of a range of a file:
//
//   00000010  48 65 6C 6C 6F 2C 20 77 6F 72 6C 64 21 00 00 00  |Hello, world!...|
//
// Any file can be disassembled this way, including PS4/PSP/Vita PKG headers and
// Android APK/ZIP containers, and a modified listing can be assembled back into
// the document byte for byte. This is a *byte-level* listing: it does not claim
// to decode CPU instructions. See docs/CODE-LISTING.md.
#pragma once

#include "core/HexDocument.h"
#include "core/Result.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace pkg {

/// One contiguous run of bytes recovered from a listing.
struct ListingEntry {
    quint64 offset = 0;
    QByteArray bytes;
};

/// Builds a listing for [start, start + length). `length` is clamped to the
/// file. Returns an empty string for a zero-length range.
[[nodiscard]] Result<QString> buildListing(const HexDocument& document, quint64 start,
                                           quint64 length, int bytesPerRow = 16,
                                           const QString& header = QString());

/// Parses listing text back into byte runs. Accepted per line:
///   00000010  90 90 90          (offset, one or more spaces, hex pairs)
///   0x00000010: 90 90 90        (offset, colon, hex pairs)
/// Everything after '|' or '#' is treated as a comment/ASCII column. Lines that
/// do not start with an offset are ignored. Fails when no byte run is found.
[[nodiscard]] Result<QVector<ListingEntry>> parseListing(const QString& text,
                                                         quint64 fileSize = 0);

/// Applies parsed runs to `document` as undoable, in-memory edits. Editing is
/// not saved to disk; the caller decides when to save. Returns the number of
/// bytes that actually changed.
[[nodiscard]] Result<int> applyListing(HexDocument& document,
                                       const QVector<ListingEntry>& entries);

/// Convenience wrapper: the total byte count covered by `entries`.
[[nodiscard]] quint64 listingByteCount(const QVector<ListingEntry>& entries);

}  // namespace pkg
