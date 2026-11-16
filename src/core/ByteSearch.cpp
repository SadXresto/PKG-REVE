#include "core/ByteSearch.h"

#include "core/HexDocument.h"
#include "core/SafeMath.h"

#include "core/SafeMath.h"

#include <algorithm>

namespace pkg {
namespace {

constexpr quint64 kChunkSize = 1U << 20U;  // 1 MiB

QByteArray asciiLower(QByteArray data)
{
    for (qsizetype i = 0; i < data.size(); ++i) {
        const char c = data.at(i);
        if (c >= 'A' && c <= 'Z')
            data[i] = static_cast<char>(c - 'A' + 'a');
    }
    return data;
}

}  // namespace

Result<SearchResult> searchBytes(const FileSource& source,
                                 const QByteArray& needle,
                                 const SearchOptions& options,
                                 const CancelToken& token)
{
    if (!source.isOpen())
        return makeError(QStringLiteral("No file is open"));
    if (needle.isEmpty())
        return makeError(QStringLiteral("Search pattern is empty"));
    if (options.startOffset >= source.size() && source.size() > 0)
        return makeError(QStringLiteral("Search start offset is outside the file"));

    const QByteArray pattern = options.caseInsensitive ? asciiLower(needle) : needle;
    const quint64 overlap = static_cast<quint64>(pattern.size()) - 1;

    SearchResult result;
    quint64 cursor = options.startOffset;
    quint64 lastReported = options.startOffset;

    while (cursor < source.size()) {
        if (token.isCancelled())
            return makeError(QStringLiteral("Search cancelled"));

        const quint64 remaining = source.size() - cursor;
        const quint64 chunkLength = std::min<quint64>(kChunkSize + overlap, remaining);
        auto chunk = source.read(cursor, chunkLength);
        if (!chunk)
            return chunk.error();

        QByteArray haystack = options.caseInsensitive ? asciiLower(chunk.value()) : chunk.value();
        result.scannedBytes += chunkLength;

        qsizetype index = 0;
        while (index <= haystack.size() - pattern.size()) {
            const qsizetype found = haystack.indexOf(pattern, index);
            if (found < 0)
                break;
            const quint64 absolute =
                cursor + static_cast<quint64>(found);
            if (absolute >= lastReported) {
                result.offsets.append(absolute);
                lastReported = absolute + 1;
                if (result.offsets.size() >= static_cast<qsizetype>(options.maxHits)) {
                    result.truncated = true;
                    return result;
                }
            }
            index = found + 1;
        }

        if (chunkLength < kChunkSize + overlap)
            break;
        cursor += kChunkSize;
    }

    return result;
}

Result<int> replaceAllEqualLength(HexDocument& document, const QVector<quint64>& offsets,
                                  const QByteArray& pattern, const QByteArray& replacement)
{
    if (!document.isValid())
        return makeError(QStringLiteral("No document is open"));
    if (pattern.isEmpty())
        return makeError(QStringLiteral("The search pattern is empty"));
    if (replacement.size() != pattern.size()) {
        return makeError(
            QStringLiteral("Replacement must be the same length as the pattern"),
            QStringLiteral("pattern %1 byte(s), replacement %2 byte(s); insert/delete is not "
                           "implemented")
                .arg(pattern.size())
                .arg(replacement.size()));
    }

    int replaced = 0;
    for (const quint64 offset : offsets) {
        if (!safe::inRange(offset, static_cast<quint64>(pattern.size()), document.size())) {
            return makeError(QStringLiteral("Match offset is outside the file"),
                             QStringLiteral("offset %1").arg(offset));
        }
        for (qsizetype i = 0; i < replacement.size(); ++i) {
            document.setByte(offset + static_cast<quint64>(i),
                             static_cast<quint8>(replacement.at(i)));
        }
        ++replaced;
    }
    return replaced;
}

}  // namespace pkg
