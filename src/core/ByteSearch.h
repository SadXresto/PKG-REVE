// Streaming byte pattern search over a mapped file.
#pragma once

#include "core/CancelToken.h"
#include "core/FileSource.h"
#include "core/Result.h"

#include <QByteArray>
#include <QVector>

namespace pkg {

class HexDocument;

struct SearchOptions {
    quint64 startOffset = 0;
    quint64 maxHits = 5000;
    /// ASCII-only case folding; non-ASCII bytes are compared exactly.
    bool caseInsensitive = false;
};

struct SearchResult {
    QVector<quint64> offsets;
    bool truncated = false;
    quint64 scannedBytes = 0;
};

/// Finds every occurrence of `needle`, reading at most one chunk at a time so
/// memory use stays flat for multi-gigabyte files.
[[nodiscard]] Result<SearchResult> searchBytes(const FileSource& source,
                                              const QByteArray& needle,
                                              const SearchOptions& options,
                                              const CancelToken& token);

/// Applies an equal-length replacement at pre-computed match offsets. Changing
/// the file length is intentionally unsupported until insert/delete lands, so
/// a size mismatch is an error rather than a silent truncation. Every changed
/// byte is journaled, so undo works byte by byte.
[[nodiscard]] Result<int> replaceAllEqualLength(HexDocument& document,
                                                const QVector<quint64>& offsets,
                                                const QByteArray& pattern,
                                                const QByteArray& replacement);

}  // namespace pkg
