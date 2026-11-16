#pragma once

#include "core/IFormatParser.h"

namespace pkg {

class PeParser final : public IFormatParser {
public:
    [[nodiscard]] QString id() const override { return QStringLiteral("builtin.pe"); }
    [[nodiscard]] QString displayName() const override { return QStringLiteral("PE/COFF"); }
    [[nodiscard]] int probe(const FileSource& source, const CancelToken& token) const override;
    [[nodiscard]] Result<PackageInfo> parse(const FileSource& source,
                                            const CancelToken& token) const override;
};

}  // namespace pkg
