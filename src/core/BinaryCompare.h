// Bounded binary file comparison used by the hex editor's compare view.
#pragma once

#include "core/CancelToken.h"
#include "core/Result.h"

#include <QString>
#include <QVariant>
#include <QVector>

namespace pkg {

/// One contiguous region where the two files differ (or where only one file
/// has bytes, when their sizes differ).
struct DiffRun {
    quint64 offset = 0;
    quint64 length = 0;
    /// True for the tail of the longer file beyond the shorter file's end.
    bool onlyInOne = false;
};

struct CompareResult {
    quint64 sizeA = 0;
    quint64 sizeB = 0;
    quint64 comparedBytes = 0;
    quint64 differingBytes = 0;
    /// True when the comparison stopped at the byte budget; the result then
    /// describes the compared prefix only.
    bool truncated = false;
    /// First differing offset, or 0 when identical.
    quint64 firstDifference = 0;
    QVector<DiffRun> runs;

    [[nodiscard]] bool identical() const
    {
        return differingBytes == 0 && sizeA == sizeB && !truncated;
    }
};

/// Compares two files chunk by chunk without loading either into memory.
/// `maxBytes` bounds the work; the result reports truncation instead of
/// pretending the whole file was compared.
[[nodiscard]] Result<CompareResult> compareFiles(const QString& pathA, const QString& pathB,
                                                quint64 maxBytes = 64ULL * 1024ULL * 1024ULL,
                                                const CancelToken& token = {},
                                                int maxRuns = 5000);

}  // namespace pkg
