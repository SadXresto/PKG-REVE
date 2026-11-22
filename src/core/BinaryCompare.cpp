#include "core/BinaryCompare.h"

#include "core/FileSource.h"

#include <algorithm>
#include <span>

namespace pkg {
namespace {

constexpr quint64 kChunk = 256ULL * 1024ULL;

}  // namespace

Result<CompareResult> compareFiles(const QString& pathA, const QString& pathB, quint64 maxBytes,
                                   const CancelToken& token, int maxRuns)
{
    auto first = FileSource::open(pathA);
    if (!first)
        return first.error();
    auto second = FileSource::open(pathB);
    if (!second)
        return second.error();

    CompareResult result;
    result.sizeA = first.value().size();
    result.sizeB = second.value().size();

    const quint64 shared = std::min(result.sizeA, result.sizeB);
    const quint64 budget = std::min(shared, maxBytes);
    result.truncated = shared > budget;

    bool openRun = false;
    quint64 runStart = 0;
    quint64 previousDifference = 0;

    const auto closeRun = [&](quint64 endOffset) {
        if (!openRun)
            return;
        if (static_cast<int>(result.runs.size()) < maxRuns) {
            DiffRun run;
            run.offset = runStart;
            run.length = endOffset - runStart;
            result.runs.append(run);
        }
        openRun = false;
    };

    for (quint64 offset = 0; offset < budget;) {
        if (token.isCancelled())
            return makeError(QStringLiteral("Comparison cancelled"));
        const quint64 length = std::min(kChunk, budget - offset);
        auto a = first.value().view(offset, length);
        auto b = second.value().view(offset, length);
        if (!a)
            return a.error();
        if (!b)
            return b.error();
        const auto left = a.value();
        const auto right = b.value();
        for (quint64 index = 0; index < length; ++index) {
            if (left[static_cast<size_t>(index)] == right[static_cast<size_t>(index)])
                continue;
            const quint64 position = offset + index;
            ++result.differingBytes;
            if (result.firstDifference == 0 && result.differingBytes == 1)
                result.firstDifference = position;
            if (!openRun) {
                openRun = true;
                runStart = position;
            } else if (position != previousDifference + 1) {
                closeRun(previousDifference + 1);
                openRun = true;
                runStart = position;
            }
            previousDifference = position;
        }
        offset += length;
    }
    closeRun(previousDifference + 1);
    result.comparedBytes = budget;

    // Differing sizes are a difference too: report the unmatched tail as a
    // run that belongs to one file only.
    if (result.sizeA != result.sizeB && !result.truncated) {
        DiffRun tail;
        tail.offset = shared;
        tail.length = std::max(result.sizeA, result.sizeB) - shared;
        tail.onlyInOne = true;
        result.runs.append(tail);
        result.differingBytes += tail.length;
    }

    return result;
}

}  // namespace pkg
