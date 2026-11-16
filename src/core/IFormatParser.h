// Format parser interface. Built-in parsers implement it today; plugins will
// register additional parsers through the same registry (see docs/ROADMAP.md).
#pragma once

#include "core/CancelToken.h"
#include "core/FileSource.h"
#include "core/FormatInfo.h"
#include "core/Result.h"

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace pkg {

class IFormatParser {
public:
    virtual ~IFormatParser() = default;

    [[nodiscard]] virtual QString id() const = 0;
    [[nodiscard]] virtual QString displayName() const = 0;
    [[nodiscard]] virtual QString version() const { return QStringLiteral("1.0"); }

    /// 0 = not this format, 100 = signature matched unambiguously.
    [[nodiscard]] virtual int probe(const FileSource& source,
                                    const CancelToken& token) const = 0;

    /// Describes the file. Must not throw for malformed input; returns an
    /// Error only when nothing useful can be reported.
    [[nodiscard]] virtual Result<PackageInfo> parse(const FileSource& source,
                                                    const CancelToken& token) const = 0;
};

class ParserRegistry {
public:
    static ParserRegistry& instance();

    /// Registers a parser. Ownership transfers to the registry.
    void registerParser(std::unique_ptr<IFormatParser> parser);

    /// Probes every parser, picks the strongest signature match and parses.
    [[nodiscard]] Result<PackageInfo> analyze(const FileSource& source,
                                              const CancelToken& token) const;

    [[nodiscard]] std::vector<IFormatParser*> parsers() const;
    [[nodiscard]] QStringList parserIds() const;

    /// Registers the built-in parsers once. Safe to call repeatedly.
    static void registerBuiltins();

private:
    ParserRegistry() = default;
    std::vector<std::unique_ptr<IFormatParser>> m_parsers;
};

}  // namespace pkg
