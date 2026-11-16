#include "core/parsers/ZipParser.h"

#include "core/ByteReader.h"
#include "core/parsers/ParserSupport.h"
#include "core/SafeMath.h"

#include <QDateTime>
#include <algorithm>

namespace pkg {
namespace {

constexpr quint32 kLocalHeaderSignature = 0x04034B50U;
constexpr quint32 kCentralHeaderSignature = 0x02014B50U;
constexpr quint32 kEndOfCentralDirSignature = 0x06054B50U;
constexpr quint64 kMaxCentralDirectoryBytes = 4U * 1024U * 1024U;
constexpr quint64 kMaxListedEntries = 200;
constexpr quint64 kMaxEntryCount = 1'000'000;

}  // namespace

int ZipParser::probe(const FileSource& source, const CancelToken& token) const
{
    if (token.isCancelled() || source.size() < 4)
        return 0;
    auto magic = source.read(0, 4);
    if (!magic)
        return 0;
    ByteReader reader(magic.value());
    auto signature = reader.u32le();
    if (!signature)
        return 0;
    switch (signature.value()) {
    case kLocalHeaderSignature:
    case kEndOfCentralDirSignature:
    case 0x08074B50U:  // spanning marker / data descriptor
        return 85;
    default:
        return 0;
    }
}

Result<quint64> ZipParser::findEndOfCentralDirectory(const FileSource& source)
{
    constexpr quint64 kMaxSearch = 65557;  // 64 KiB comment + 22 byte record
    const quint64 searchSize = std::min<quint64>(source.size(), kMaxSearch);
    if (searchSize < 22)
        return makeError(QStringLiteral("File is too small to contain a ZIP directory"));

    const quint64 base = source.size() - searchSize;
    auto tail = source.read(base, searchSize);
    if (!tail)
        return tail.error();
    const QByteArray& data = tail.value();

    for (qsizetype i = data.size() - 22; i >= 0; --i) {
        const auto* p = reinterpret_cast<const unsigned char*>(data.constData() + i);
        if (p[0] == 0x50 && p[1] == 0x4B && p[2] == 0x05 && p[3] == 0x06)
            return base + static_cast<quint64>(i);
    }
    return makeError(QStringLiteral("End of central directory record not found"),
                     QStringLiteral("the archive may be truncated"));
}

Result<QList<ZipParser::Entry>> ZipParser::readCentralDirectory(const FileSource& source,
                                                                const CancelToken& token,
                                                                QStringList* warnings,
                                                                bool* truncated,
                                                                quint64* directoryOffset,
                                                                quint64* directorySize)
{
    if (truncated != nullptr)
        *truncated = false;

    auto eocdOffset = findEndOfCentralDirectory(source);
    if (!eocdOffset)
        return eocdOffset.error();

    auto eocdBytes = source.read(eocdOffset.value(), 22);
    if (!eocdBytes)
        return eocdBytes.error();
    ByteReader eocd(eocdBytes.value());
    if (!eocd.skip(4))
        return makeError(QStringLiteral("Malformed ZIP directory"));
    auto diskNumber = eocd.u16le();
    auto centralDisk = eocd.u16le();
    auto entriesOnDisk = eocd.u16le();
    auto entryCount = eocd.u16le();
    auto directorySizeIn = eocd.u32le();
    auto directoryOffsetIn = eocd.u32le();
    if (!diskNumber || !centralDisk || !entriesOnDisk || !entryCount || !directorySizeIn ||
        !directoryOffsetIn)
        return makeError(QStringLiteral("Malformed ZIP directory"));

    if (warnings != nullptr) {
        if (diskNumber.value() != 0U || centralDisk.value() != 0U)
            *warnings << QStringLiteral("Multi-disk ZIP archives are not supported.");
        if (entryCount.value() == 0xFFFFU)
            *warnings << QStringLiteral("ZIP64 entry count detected; only the classic "
                                        "central directory is parsed.");
        if (directorySizeIn.value() == 0xFFFFFFFFU ||
            directoryOffsetIn.value() == 0xFFFFFFFFU)
            *warnings << QStringLiteral("ZIP64 offsets detected; only the classic central "
                                        "directory is parsed.");
    }

    const quint64 count = static_cast<quint64>(entryCount.value());
    if (count > kMaxEntryCount) {
        if (warnings != nullptr)
            *warnings << QStringLiteral("Declared entry count %1 is implausible and was "
                                        "ignored.")
                             .arg(count);
        return makeError(QStringLiteral("ZIP directory entry count is invalid"));
    }

    const quint64 offset = static_cast<quint64>(directoryOffsetIn.value());
    const quint64 size = static_cast<quint64>(directorySizeIn.value());
    if (directoryOffset != nullptr)
        *directoryOffset = offset;
    if (directorySize != nullptr)
        *directorySize = size;
    if (!safe::inRange(offset, size, source.size())) {
        if (truncated != nullptr)
            *truncated = true;
        return makeError(QStringLiteral("ZIP central directory lies outside the file"));
    }

    const quint64 readable = std::min<quint64>(size, kMaxCentralDirectoryBytes);
    if (readable < size && warnings != nullptr)
        *warnings << QStringLiteral("Central directory is larger than 4 MiB; listing was "
                                    "limited.");

    auto directory = source.read(offset, readable);
    if (!directory)
        return directory.error();

    QList<Entry> entries;
    ByteReader reader(directory.value());
    for (quint64 i = 0; i < count; ++i) {
        if (token.isCancelled())
            return makeError(QStringLiteral("Analysis cancelled"));
        if (reader.remaining() < 46)
            break;
        auto signature = reader.u32le();
        if (!signature || signature.value() != kCentralHeaderSignature)
            break;
        if (!reader.skip(4))  // version made by, version needed
            break;
        auto flags = reader.u16le();
        auto method = reader.u16le();
        auto modTime = reader.u16le();
        auto modDate = reader.u16le();
        Q_UNUSED(modTime)
        Q_UNUSED(modDate)
        auto crc = reader.u32le();
        auto compressed = reader.u32le();
        auto uncompressed = reader.u32le();
        auto nameLength = reader.u16le();
        auto extraLength = reader.u16le();
        auto commentLength = reader.u16le();
        auto diskStart = reader.u16le();
        auto internalAttrs = reader.u16le();
        auto externalAttrs = reader.u32le();
        auto localOffset = reader.u32le();
        Q_UNUSED(crc)
        Q_UNUSED(internalAttrs)
        Q_UNUSED(externalAttrs)
        Q_UNUSED(diskStart)
        if (!flags || !method || !compressed || !uncompressed || !nameLength ||
            !extraLength || !commentLength || !localOffset)
            break;

        auto nameBytes = reader.qbytes(nameLength.value());
        if (!nameBytes)
            break;
        const QByteArray name = nameBytes.value();
        if (!reader.skip(static_cast<quint64>(extraLength.value()) +
                         static_cast<quint64>(commentLength.value())))
            break;

        Entry entry;
        entry.name = QString::fromUtf8(name);
        entry.compressedSize = compressed.value();
        entry.uncompressedSize = uncompressed.value();
        entry.method = method.value();
        entry.localHeaderOffset = localOffset.value();
        entry.encrypted = (flags.value() & 0x0001U) != 0U;
        entries.append(entry);

        if (reader.remaining() == 0 && i + 1 < count)
            break;
    }

    return entries;
}

Result<PackageInfo> ZipParser::parse(const FileSource& source, const CancelToken& token) const
{
    QStringList warnings;
    bool truncated = false;
    quint64 directoryOffset = 0;
    quint64 directorySize = 0;
    auto entriesResult = readCentralDirectory(source, token, &warnings, &truncated,
                                              &directoryOffset, &directorySize);
    if (!entriesResult)
        return entriesResult.error();
    const QList<Entry>& entries = entriesResult.value();

    PackageInfo info;
    info.parserId = id();
    info.parserVersion = version();
    info.warnings = warnings;

    bool hasManifest = false;
    bool hasDex = false;
    for (const Entry& entry : entries) {
        if (entry.name == QLatin1String("AndroidManifest.xml"))
            hasManifest = true;
        if (entry.name == QLatin1String("classes.dex"))
            hasDex = true;
    }

    info.kind = hasManifest ? FormatKind::Apk : FormatKind::Zip;
    info.formatName = hasManifest ? QStringLiteral("Android APK") : QStringLiteral("ZIP archive");

    InfoSection directory;
    directory.title = QStringLiteral("Central directory");
    directory.fields << parsers::field(QStringLiteral("Entries"),
                                       QString::number(entries.size()));
    directory.fields << parsers::hexField(QStringLiteral("Directory offset"),
                                          directoryOffset, 8);
    directory.fields << parsers::sizeField(QStringLiteral("Directory size"),
                                           directorySize);
    info.sections << directory;

    if (truncated) {
        info.partial = true;
        info.warnings << QStringLiteral("The central directory does not fit inside the file; "
                                        "the archive is truncated.");
    }

    if (hasManifest) {
        InfoSection apk;
        apk.title = QStringLiteral("Android package");
        apk.fields << parsers::field(QStringLiteral("AndroidManifest.xml"),
                                     QStringLiteral("present"));
        apk.fields << parsers::field(QStringLiteral("classes.dex"),
                                     hasDex ? QStringLiteral("present")
                                            : QStringLiteral("absent"));
        info.sections << apk;
        info.summary = QStringLiteral("APK archive with %1 entries").arg(entries.size());
    } else {
        info.summary = QStringLiteral("ZIP archive with %1 entries").arg(entries.size());
    }

    if (!entries.isEmpty()) {
        InfoSection listing;
        listing.title = QStringLiteral("Contents");
        const quint64 listed = std::min<quint64>(entries.size(), kMaxListedEntries);
        for (quint64 i = 0; i < listed; ++i) {
            const Entry& entry = entries[static_cast<qsizetype>(i)];
            QString value = parsers::zipMethodName(entry.method) + QStringLiteral(", ") +
                            fmt::humanBytes(entry.uncompressedSize);
            if (entry.encrypted)
                value += QStringLiteral(", encrypted");
            listing.fields << parsers::field(entry.name, value);
        }
        if (entries.size() > static_cast<qsizetype>(listed)) {
            listing.fields << parsers::field(
                QStringLiteral("..."),
                QStringLiteral("%1 more entries not listed").arg(entries.size() - listed));
        }
        info.sections << listing;
    }

    if (hasManifest)
        info.warnings << QStringLiteral("Manifest contents are binary XML and are not decoded; "
                                        "only archive structure is reported.");

    return info;
}

}  // namespace pkg
