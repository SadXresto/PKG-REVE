#pragma once

#include "core/IFormatParser.h"

namespace pkg {

/// PSP / PS Vita / PS3 PKG container family (magic 0x7F504B47).
/// Only the documented plain-text header is described; encrypted payloads are
/// never decrypted or extracted.
class VitaPkgParser final : public IFormatParser {
public:
    [[nodiscard]] QString id() const override { return QStringLiteral("builtin.pkg.vita"); }
    [[nodiscard]] QString displayName() const override
    {
        return QStringLiteral("PSP/PS Vita PKG");
    }
    [[nodiscard]] int probe(const FileSource& source, const CancelToken& token) const override;
    [[nodiscard]] Result<PackageInfo> parse(const FileSource& source,
                                            const CancelToken& token) const override;
};

/// PS4 PKG container (magic 0x7F434E54, 0x1000 byte big-endian header).
class Ps4PkgParser final : public IFormatParser {
public:
    [[nodiscard]] QString id() const override { return QStringLiteral("builtin.pkg.ps4"); }
    [[nodiscard]] QString displayName() const override { return QStringLiteral("PS4 PKG"); }
    [[nodiscard]] int probe(const FileSource& source, const CancelToken& token) const override;
    [[nodiscard]] Result<PackageInfo> parse(const FileSource& source,
                                            const CancelToken& token) const override;
};

/// Last resort parser so that unrecognized files still get a useful, honest
/// description instead of an empty window.
class RawFallbackParser final : public IFormatParser {
public:
    [[nodiscard]] QString id() const override { return QStringLiteral("builtin.raw"); }
    [[nodiscard]] QString displayName() const override
    {
        return QStringLiteral("Unrecognized data");
    }
    [[nodiscard]] int probe(const FileSource& source, const CancelToken& token) const override;
    [[nodiscard]] Result<PackageInfo> parse(const FileSource& source,
                                            const CancelToken& token) const override;
};

}  // namespace pkg
