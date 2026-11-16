#include "core/parsers/PkgParsers.h"

#include "core/ByteReader.h"
#include "core/FormatUtil.h"
#include "core/parsers/ParserSupport.h"
#include "core/SafeMath.h"

#include <QDateTime>
#include <algorithm>

namespace pkg {
namespace {

constexpr quint32 kVitaMagic = 0x7F504B47U;  // ".PKG"
constexpr quint32 kPs4Magic = 0x7F434E54U;   // ".CNT"
constexpr quint64 kVitaHeaderSize = 0xC0;
constexpr quint64 kPs4HeaderSize = 0x1000;
constexpr quint64 kMaxDisplayedItems = 1'000'000;

QString contentIdIfPrintable(const QByteArray& raw)
{
    const auto span = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(raw.constData()),
        static_cast<std::size_t>(raw.size()));
    QString id = fmt::printableString(span);
    return id.trimmed();
}

Result<QByteArray> readPrefix(const FileSource& source, quint64 length)
{
    return source.read(0, std::min<quint64>(source.size(), length));
}

}  // namespace

// ---------------------------------------------------------------------------
// PSP / PS Vita PKG
// ---------------------------------------------------------------------------
int VitaPkgParser::probe(const FileSource& source, const CancelToken& token) const
{
    if (token.isCancelled() || source.size() < 4)
        return 0;
    auto magicBytes = source.read(0, 4);
    if (!magicBytes)
        return 0;
    ByteReader reader(magicBytes.value());
    auto magic = reader.u32be();
    if (magic && magic.value() == kVitaMagic)
        return 100;
    return 0;
}

Result<PackageInfo> VitaPkgParser::parse(const FileSource& source, const CancelToken& token) const
{
    if (token.isCancelled())
        return makeError(QStringLiteral("Analysis cancelled"));
    auto prefix = readPrefix(source, kVitaHeaderSize);
    if (!prefix)
        return prefix.error();
    if (prefix.value().size() < 0x30)
        return makeError(QStringLiteral("PKG header is truncated"),
                         QStringLiteral("file is only %1 bytes").arg(source.size()));

    PackageInfo info;
    info.kind = FormatKind::VitaPkg;
    info.formatName = QStringLiteral("PSP/PS Vita PKG");
    info.parserId = id();
    info.parserVersion = version();

    ByteReader reader(prefix.value());
    auto magic = reader.u32be();
    auto revision = reader.u16be();
    auto pkgType = reader.u16be();
    auto metaOffset = reader.u32be();
    auto metaCount = reader.u32be();
    auto metaSize = reader.u32be();
    auto itemCount = reader.u32be();
    auto totalSize = reader.u64be();
    auto dataOffset = reader.u64be();
    auto dataSize = reader.u64be();
    if (!magic || !revision || !pkgType || !metaOffset || !metaCount || !metaSize ||
        !itemCount || !totalSize || !dataOffset || !dataSize)
        return makeError(QStringLiteral("PKG header fields could not be read"));

    if (magic.value() != kVitaMagic)
        return makeError(QStringLiteral("Not a PKG file"));

    QString platform;
    switch (pkgType.value()) {
    case 1:
        platform = QStringLiteral("PS3");
        break;
    case 2:
        platform = QStringLiteral("PSP / PS Vita");
        break;
    default:
        platform = QStringLiteral("Unrecognized type");
        break;
    }

    InfoSection header;
    header.title = QStringLiteral("PKG header");
    header.fields << parsers::field(QStringLiteral("Magic"),
                                    QStringLiteral("0x%1 (\".PKG\")").arg(fmt::hex(magic.value(), 8)))
                  << parsers::field(QStringLiteral("Package type"),
                                    QStringLiteral("%1 (%2)").arg(platform).arg(pkgType.value()))
                  << parsers::field(QStringLiteral("Finalized"),
                                    revision.value() == 0x8000 ? QStringLiteral("yes")
                                                               : QStringLiteral("no (debug)"))
                  << parsers::hexField(QStringLiteral("Metadata offset"), metaOffset.value(), 8)
                  << parsers::field(QStringLiteral("Metadata entries"),
                                    QString::number(metaCount.value()))
                  << parsers::sizeField(QStringLiteral("Metadata size"), metaSize.value())
                  << parsers::field(QStringLiteral("File items"),
                                    QString::number(itemCount.value()))
                  << parsers::sizeField(QStringLiteral("Declared total size"),
                                        totalSize.value())
                  << parsers::hexField(QStringLiteral("Data offset"), dataOffset.value(), 8)
                  << parsers::sizeField(QStringLiteral("Data size"), dataSize.value());
    info.sections << header;

    if (prefix.value().size() >= 0x60) {
        auto contentId = source.read(0x30, 0x30);
        if (contentId) {
            InfoSection ids;
            ids.title = QStringLiteral("Identity");
            ids.fields << parsers::field(QStringLiteral("Content ID"),
                                         contentIdIfPrintable(contentId.value()));
            info.sections << ids;
        }
    }

    if (totalSize.value() != source.size()) {
        info.warnings << QStringLiteral("Declared total size (%1 bytes) differs from the actual "
                                        "file size (%2 bytes).")
                             .arg(totalSize.value())
                             .arg(source.size());
        info.partial = true;
    }
    const auto payloadEnd = safe::add(dataOffset.value(), dataSize.value());
    if (!payloadEnd || *payloadEnd > source.size()) {
        info.warnings << QStringLiteral("Declared payload range lies outside the file.");
        info.partial = true;
    }
    const auto metaEnd = safe::add(metaOffset.value(), metaSize.value());
    if (!metaEnd || *metaEnd > source.size()) {
        info.warnings << QStringLiteral("Declared metadata range lies outside the file.");
        info.partial = true;
    }
    if (itemCount.value() > kMaxDisplayedItems) {
        info.warnings << QStringLiteral("Item count %1 is implausible.").arg(itemCount.value());
        info.partial = true;
    }

    info.warnings << QStringLiteral("Package payload is encrypted with console-specific keys. "
                                    "PKG Studio does not decrypt or extract protected content.");
    info.summary = QStringLiteral("%1 package, %2 file items, %3")
                       .arg(platform)
                       .arg(itemCount.value())
                       .arg(fmt::humanBytes(source.size()));
    return info;
}

// ---------------------------------------------------------------------------
// PS4 PKG
// ---------------------------------------------------------------------------
int Ps4PkgParser::probe(const FileSource& source, const CancelToken& token) const
{
    if (token.isCancelled() || source.size() < 4)
        return 0;
    auto magicBytes = source.read(0, 4);
    if (!magicBytes)
        return 0;
    ByteReader reader(magicBytes.value());
    auto magic = reader.u32be();
    if (magic && magic.value() == kPs4Magic)
        return 100;
    return 0;
}

Result<PackageInfo> Ps4PkgParser::parse(const FileSource& source, const CancelToken& token) const
{
    if (token.isCancelled())
        return makeError(QStringLiteral("Analysis cancelled"));
    auto prefix = readPrefix(source, kPs4HeaderSize);
    if (!prefix)
        return prefix.error();
    if (prefix.value().size() < 0x80)
        return makeError(QStringLiteral("PS4 PKG header is truncated"),
                         QStringLiteral("file is only %1 bytes").arg(source.size()));

    const QByteArray header = prefix.value();
    ByteReader reader(header);

    PackageInfo info;
    info.kind = FormatKind::Ps4Pkg;
    info.formatName = QStringLiteral("PS4 PKG");
    info.parserId = id();
    info.parserVersion = version();

    auto magic = reader.u32be();
    auto pkgType = reader.u32be();
    auto unknown0x08 = reader.u32be();
    auto fileCount = reader.u32be();
    auto entryCount = reader.u32be();
    auto scEntryCount = reader.u16be();
    auto entryCount2 = reader.u16be();
    auto tableOffset = reader.u32be();
    auto entryDataSize = reader.u32be();
    auto bodyOffset = reader.u64be();
    auto bodySize = reader.u64be();
    auto contentOffset = reader.u64be();
    auto contentSize = reader.u64be();
    if (!magic || !pkgType || !unknown0x08 || !fileCount || !entryCount || !scEntryCount ||
        !entryCount2 || !tableOffset || !entryDataSize || !bodyOffset || !bodySize ||
        !contentOffset || !contentSize)
        return makeError(QStringLiteral("PS4 PKG header fields could not be read"));

    if (magic.value() != kPs4Magic)
        return makeError(QStringLiteral("Not a PS4 PKG file"));

    auto contentIdBytes = reader.qbytes(0x24);
    if (!contentIdBytes)
        return contentIdBytes.error();
    if (!reader.skip(0x0C))  // padding
        return makeError(QStringLiteral("PS4 PKG header is truncated"));

    auto drmType = reader.u32be();
    auto contentType = reader.u32be();
    auto contentFlags = reader.u32be();
    auto promoteSize = reader.u32be();
    auto versionDate = reader.u32be();
    auto versionHash = reader.u32be();
    if (!drmType || !contentType || !contentFlags || !promoteSize || !versionDate ||
        !versionHash)
        return makeError(QStringLiteral("PS4 PKG header fields could not be read"));

    InfoSection headerSection;
    headerSection.title = QStringLiteral("PKG header (big endian)");
    headerSection.fields
        << parsers::field(QStringLiteral("Magic"),
                          QStringLiteral("0x%1 (\".CNT\")").arg(fmt::hex(magic.value(), 8)))
        << parsers::field(QStringLiteral("Package type"), fmt::hex(pkgType.value(), 8))
        << parsers::field(QStringLiteral("Unknown field 0x008"),
                          fmt::hex(unknown0x08.value(), 8))
        << parsers::field(QStringLiteral("File count"), QString::number(fileCount.value()))
        << parsers::field(QStringLiteral("Entry count"), QString::number(entryCount.value()))
        << parsers::field(QStringLiteral("Secondary entry count"),
                          QString::number(scEntryCount.value()))
        << parsers::field(QStringLiteral("Entry count copy 0x016"),
                          QString::number(entryCount2.value()))
        << parsers::hexField(QStringLiteral("Entry table offset"), tableOffset.value(), 8)
        << parsers::sizeField(QStringLiteral("Entry data size"), entryDataSize.value())
        << parsers::hexField(QStringLiteral("Body offset"), bodyOffset.value(), 8)
        << parsers::sizeField(QStringLiteral("Body size"), bodySize.value())
        << parsers::hexField(QStringLiteral("Content offset"), contentOffset.value(), 8)
        << parsers::sizeField(QStringLiteral("Content size"), contentSize.value())
        << parsers::field(QStringLiteral("Content ID"),
                          contentIdIfPrintable(contentIdBytes.value()))
        << parsers::field(QStringLiteral("DRM type"), fmt::hex(drmType.value(), 8))
        << parsers::field(QStringLiteral("Content type"), fmt::hex(contentType.value(), 8))
        << parsers::field(QStringLiteral("Content flags"), fmt::hex(contentFlags.value(), 8))
        << parsers::field(QStringLiteral("Promote size"), QString::number(promoteSize.value()))
        << parsers::field(QStringLiteral("Version date"),
                          fmt::hex(versionDate.value(), 8))
        << parsers::field(QStringLiteral("Version hash"), fmt::hex(versionHash.value(), 8));
    info.sections << headerSection;

    if (entryCount.value() > kMaxDisplayedItems) {
        info.warnings << QStringLiteral("Entry count %1 is implausible; the file table was not "
                                        "interpreted.")
                             .arg(entryCount.value());
        info.partial = true;
    }
    if (entryCount2.value() != scEntryCount.value() &&
        entryCount2.value() != 0) {
        if (static_cast<quint32>(entryCount2.value()) != entryCount.value())
            info.warnings << QStringLiteral("Entry count fields are inconsistent (%1 vs %2 vs %3).")
                                 .arg(scEntryCount.value())
                                 .arg(entryCount.value())
                                 .arg(entryCount2.value());
    }
    const auto tableEnd = safe::add(tableOffset.value(), entryDataSize.value());
    if (!tableEnd || *tableEnd > source.size()) {
        info.warnings << QStringLiteral("Declared entry table lies outside the file.");
        info.partial = true;
    }
    const auto bodyEnd = safe::add(bodyOffset.value(), bodySize.value());
    if (!bodyEnd || *bodyEnd > source.size()) {
        info.warnings << QStringLiteral("Declared body range lies outside the file.");
        info.partial = true;
    }

    if (header.size() >= static_cast<qsizetype>(kPs4HeaderSize)) {
        InfoSection image;
        image.title = QStringLiteral("PFS image");
        if (auto imageCount = reader.u32beAt(0x404)) {
            image.fields << parsers::field(QStringLiteral("Image count"),
                                           QString::number(imageCount.value()));
        }
        if (auto imageFlags = reader.u64beAt(0x408)) {
            image.fields << parsers::field(QStringLiteral("Image flags"),
                                           fmt::hex(imageFlags.value(), 16));
        }
        if (auto imageOffset = reader.u64beAt(0x410)) {
            image.fields << parsers::hexField(QStringLiteral("Image offset"),
                                              imageOffset.value(), 16);
        }
        if (auto imageSize = reader.u64beAt(0x418)) {
            image.fields << parsers::sizeField(QStringLiteral("Image size"),
                                                imageSize.value());
        }
        if (auto declaredSize = reader.u64beAt(0x430)) {
            image.fields << parsers::sizeField(QStringLiteral("Declared package size"),
                                                declaredSize.value());
            if (declaredSize.value() != source.size()) {
                info.warnings << QStringLiteral("Declared package size (%1 bytes) differs from "
                                                "the actual file size (%2 bytes).")
                                     .arg(declaredSize.value())
                                     .arg(source.size());
                info.partial = true;
            }
        }
        if (!image.fields.isEmpty())
            info.sections << image;
    } else {
        info.warnings << QStringLiteral("File is shorter than the 0x1000 byte PS4 header; "
                                        "PFS fields were not read.");
        info.partial = true;
    }

    info.warnings << QStringLiteral("PS4 package bodies are encrypted and signed. PKG Studio "
                                    "describes the container header only and does not decrypt, "
                                    "extract or modify protected content.");
    info.summary = QStringLiteral("PS4 package, %1 entries, %2")
                       .arg(entryCount.value())
                       .arg(fmt::humanBytes(source.size()));
    return info;
}

// ---------------------------------------------------------------------------
// Raw fallback
// ---------------------------------------------------------------------------
int RawFallbackParser::probe(const FileSource&, const CancelToken& token) const
{
    return token.isCancelled() ? 0 : 1;
}

Result<PackageInfo> RawFallbackParser::parse(const FileSource& source,
                                             const CancelToken& token) const
{
    if (token.isCancelled())
        return makeError(QStringLiteral("Analysis cancelled"));

    auto prefix = readPrefix(source, 256);
    if (!prefix)
        return prefix.error();
    const QByteArray data = prefix.value();
    const auto span = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(data.constData()),
        static_cast<std::size_t>(data.size()));

    PackageInfo info;
    info.kind = FormatKind::RawBinary;
    info.formatName = QStringLiteral("Unrecognized data");
    info.parserId = id();
    info.parserVersion = version();
    info.summary = QStringLiteral("No documented format signature was found.");

    InfoSection section;
    section.title = QStringLiteral("Raw bytes");
    section.fields << parsers::sizeField(QStringLiteral("File size"), source.size())
                   << parsers::field(QStringLiteral("First bytes"), fmt::byteDump(span, 32))
                   << parsers::field(QStringLiteral("ASCII"),
                                     fmt::asciiDump(span.first(std::min<std::size_t>(span.size(), 64))));
    info.sections << section;
    info.warnings << QStringLiteral("PKG Studio only reports signature matched formats; this "
                                    "file is shown as raw bytes. Custom formats can be added "
                                    "through the future plugin API.");
    return info;
}

}  // namespace pkg
