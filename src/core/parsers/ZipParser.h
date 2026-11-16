#pragma once

#include "core/IFormatParser.h"

namespace pkg {

/// Handles ZIP archives and identifies Android APKs by the presence of
/// AndroidManifest.xml in the central directory.
class ZipParser final : public IFormatParser {
public:
    [[nodiscard]] QString id() const override { return QStringLiteral("builtin.zip"); }
    [[nodiscard]] QString displayName() const override
    {
        return QStringLiteral("ZIP / Android APK");
    }
    [[nodiscard]] int probe(const FileSource& source, const CancelToken& token) const override;
    [[nodiscard]] Result<PackageInfo> parse(const FileSource& source,
                                            const CancelToken& token) const override;

    struct Entry {
        QString name;
        quint64 compressedSize = 0;
        quint64 uncompressedSize = 0;
        quint16 method = 0;
        quint32 localHeaderOffset = 0;
        bool encrypted = false;
    };

    /// Reads the central directory. Exposed for tests and future content listing.
    [[nodiscard]] static Result<QList<Entry>> readCentralDirectory(const FileSource& source,
                                                                   const CancelToken& token,
                                                                   QStringList* warnings,
                                                                   bool* truncated,
                                                                   quint64* directoryOffset = nullptr,
                                                                   quint64* directorySize = nullptr);

private:
    static Result<quint64> findEndOfCentralDirectory(const FileSource& source);
};

}  // namespace pkg
