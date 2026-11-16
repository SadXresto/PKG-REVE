#include "core/IFormatParser.h"

#include "core/FormatUtil.h"
#include "core/Logging.h"
#include "core/SignatureScanner.h"
#include "core/parsers/ElfParser.h"
#include "core/parsers/PeParser.h"
#include "core/parsers/PkgParsers.h"
#include "core/parsers/ZipParser.h"

#include <algorithm>
#include <exception>

namespace pkg {
namespace {
constexpr auto kLogCategory = "analysis";
}

ParserRegistry& ParserRegistry::instance()
{
    static ParserRegistry registry;
    return registry;
}

void ParserRegistry::registerParser(std::unique_ptr<IFormatParser> parser)
{
    if (!parser)
        return;
    const QString id = parser->id();
    const bool duplicate = std::any_of(m_parsers.begin(), m_parsers.end(),
                                       [&id](const auto& existing) {
                                           return existing->id() == id;
                                       });
    if (duplicate) {
        logWarning(QString::fromLatin1(kLogCategory),
                   QStringLiteral("Parser id '%1' is already registered; ignoring").arg(id));
        return;
    }
    logDebug(QString::fromLatin1(kLogCategory),
             QStringLiteral("Registered parser %1 (%2)").arg(id, parser->displayName()));
    m_parsers.push_back(std::move(parser));
}

Result<PackageInfo> ParserRegistry::analyze(const FileSource& source,
                                            const CancelToken& token) const
{
    if (!source.isOpen())
        return makeError(QStringLiteral("No file is open"));

    IFormatParser* best = nullptr;
    int bestScore = 0;
    for (const auto& parser : m_parsers) {
        if (token.isCancelled())
            return makeError(QStringLiteral("Analysis cancelled"));
        int score = 0;
        try {
            score = parser->probe(source, token);
        } catch (const std::exception& error) {
            logError(QString::fromLatin1(kLogCategory),
                     QStringLiteral("Parser %1 probe failed: %2")
                         .arg(parser->id(), QString::fromUtf8(error.what())));
            score = 0;
        }
        if (score > bestScore) {
            bestScore = score;
            best = parser.get();
        }
    }

    if (best == nullptr)
        return makeError(QStringLiteral("No parser recognized this file"));

    try {
        auto info = best->parse(source, token);
        if (!info) {
            return makeError(QStringLiteral("Analysis of %1 failed").arg(source.path()),
                             info.error().detail.isEmpty()
                                 ? info.error().message
                                 : QStringLiteral("%1 (%2)")
                                       .arg(info.error().message, info.error().detail));
        }

        info.value().signatures = SignatureScanner::scan(source);
        if (!info.value().signatures.isEmpty()) {
            InfoSection section;
            section.title = QStringLiteral("Detected signatures");
            for (const SignatureHit& hit : info.value().signatures) {
                InfoField field;
                field.name = hit.name;
                field.value = QStringLiteral("offset %1, %2 byte(s)")
                                  .arg(fmt::hexPrefixed(hit.offset, 8))
                                  .arg(hit.length);
                field.note = QStringLiteral("confidence %1% \u2014 %2")
                                 .arg(hit.confidence)
                                 .arg(hit.note);
                section.fields << field;
            }
            info.value().sections << section;
        }
        return info;
    } catch (const std::exception& error) {
        logError(QString::fromLatin1(kLogCategory),
                 QStringLiteral("Parser %1 threw: %2")
                     .arg(best->id(), QString::fromUtf8(error.what())));
        return makeError(QStringLiteral("Parser failed"),
                         QString::fromUtf8(error.what()));
    }
}

std::vector<IFormatParser*> ParserRegistry::parsers() const
{
    std::vector<IFormatParser*> result;
    result.reserve(m_parsers.size());
    for (const auto& parser : m_parsers)
        result.push_back(parser.get());
    return result;
}

QStringList ParserRegistry::parserIds() const
{
    QStringList ids;
    for (const auto& parser : m_parsers)
        ids << parser->id();
    return ids;
}

void ParserRegistry::registerBuiltins()
{
    ParserRegistry& registry = instance();
    if (!registry.m_parsers.empty())
        return;
    registry.registerParser(std::make_unique<ElfParser>());
    registry.registerParser(std::make_unique<PeParser>());
    registry.registerParser(std::make_unique<ZipParser>());
    registry.registerParser(std::make_unique<VitaPkgParser>());
    registry.registerParser(std::make_unique<Ps4PkgParser>());
    registry.registerParser(std::make_unique<RawFallbackParser>());
}

}  // namespace pkg
