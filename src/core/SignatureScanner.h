// Bounded signature detection used for annotations and analysis reports.
#pragma once

#include "core/FileSource.h"
#include "core/Signature.h"

#include <QVector>

namespace pkg {

class SignatureScanner {
public:
    /// Scans at most `maxScanBytes` from the start (default 4 MiB) for a fixed,
    /// documented set of signatures. Never scans or allocates the whole file.
    [[nodiscard]] static QVector<SignatureHit> scan(const FileSource& source,
                                                    quint64 maxScanBytes = 4U * 1024U * 1024U);
};

}  // namespace pkg
