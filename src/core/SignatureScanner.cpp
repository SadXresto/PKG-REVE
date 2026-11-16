#include "core/SignatureScanner.h"

#include "core/SafeMath.h"

#include <QByteArray>
#include <QList>

#include <algorithm>

namespace pkg {
namespace {

constexpr int kMaxHits = 24;
constexpr quint64 kZipTailBytes = 64U * 1024U;

struct Pattern {
    const char* name;
    const char* category;
    QByteArray magic;
    int confidence;
    const char* note;
};

const QList<Pattern>& patterns()
{
    static const QList<Pattern> list = {
        {"ZIP archive", "archive", QByteArray("PK\x03\x04", 4), 85,
         "local file header found; central directory validation is separate"},
        {"PNG image", "image", QByteArray("\x89PNG\r\n\x1a\n", 8), 99,
         "exact 8-byte PNG signature"},
        {"JPEG image", "image", QByteArray("\xff\xd8\xff", 3), 90,
         "JPEG start-of-image marker"},
        {"PDF document", "document", QByteArray("%PDF-", 5), 90, "PDF header"},
        {"GZIP stream", "archive", QByteArray("\x1f\x8b\x08", 3), 90, "GZIP header"},
        {"7-Zip archive", "archive", QByteArray("7z\xbc\xaf\x27\x1c", 6), 99,
         "exact 7-Zip signature"},
        {"RAR archive", "archive", QByteArray("Rar!\x1a\x07", 6), 90, "RAR signature"},
        {"RAR5 archive", "archive", QByteArray("Rar!\x1a\x07\x01\x00", 8), 99,
         "exact RAR5 signature"},
        {"SQLite database", "database", QByteArray("SQLite format 3\x00", 16), 99,
         "exact SQLite header"},
        {"WebAssembly module", "binary", QByteArray("\x00asm", 4), 99,
         "exact WebAssembly magic"},
        {"GIF image", "image", QByteArray("GIF8", 4), 95, "GIF87a/GIF89a header"},
        {"Mach-O binary", "binary", QByteArray("\xcf\xfa\xed\xfe", 4), 90,
         "64-bit little-endian Mach-O magic"},
        {"Mach-O binary", "binary", QByteArray("\xfe\xed\xfa\xcf", 4), 90,
         "64-bit big-endian Mach-O magic"},
        {"PSP / PS Vita PKG", "package", QByteArray("\x7fPKG", 4), 99,
         "documented PKG container magic"},
        {"PS4 PKG", "package", QByteArray("\x7f" "CNT", 4), 99,
         "documented PS4 PKG container magic"},
        {"Java class / universal binary", "binary", QByteArray("\xca\xfe\xba\xbe", 4), 70,
         "ambiguous 0xCAFEBABE magic (Java class or Mach-O universal)"},
    };
    return list;
}

QByteArray readPrefix(const FileSource& source, quint64 maxBytes)
{
    if (!source.isOpen() || source.size() == 0)
        return {};
    const quint64 length = std::min<quint64>(maxBytes, source.size());
    auto bytes = source.read(0, length);
    return bytes ? bytes.value() : QByteArray();
}

bool validateZipCentralDirectory(const FileSource& source)
{
    const quint64 tail = std::min<quint64>(kZipTailBytes, source.size());
    if (tail < 4)
        return false;
    auto bytes = source.read(source.size() - tail, tail);
    if (!bytes)
        return false;
    return bytes.value().indexOf(QByteArray("PK\x05\x06", 4)) >= 0;
}

bool validateElf(const QByteArray& prefix)
{
    if (prefix.size() < 16)
        return false;
    const auto identClass = static_cast<quint8>(prefix.at(4));
    const auto identData = static_cast<quint8>(prefix.at(5));
    const auto identVersion = static_cast<quint8>(prefix.at(6));
    return (identClass == 1 || identClass == 2) && (identData == 1 || identData == 2) &&
           identVersion == 1;
}

bool validatePe(const FileSource& source, const QByteArray& prefix)
{
    if (prefix.size() < 0x40)
        return false;
    const auto* data = reinterpret_cast<const unsigned char*>(prefix.constData());
    const quint32 lfanew = static_cast<quint32>(data[0x3C]) |
                           (static_cast<quint32>(data[0x3D]) << 8) |
                           (static_cast<quint32>(data[0x3E]) << 16) |
                           (static_cast<quint32>(data[0x3F]) << 24);
    if (lfanew == 0 || !safe::inRange(lfanew, 4, source.size()))
        return false;
    QByteArray signature = prefix.mid(static_cast<qsizetype>(lfanew), 4);
    if (signature.size() != 4) {
        auto read = source.read(lfanew, 4);
        if (!read)
            return false;
        signature = read.value();
    }
    return signature == QByteArray("PE\x00\x00", 4);
}

QVector<SignatureHit> scanOffsetZero(const FileSource& source, const QByteArray& prefix)
{
    QVector<SignatureHit> hits;
    if (prefix.isEmpty())
        return hits;

    if (prefix.size() >= 4 && prefix.startsWith(QByteArray("\x7f" "ELF", 4))) {
        SignatureHit hit;
        hit.name = QStringLiteral("ELF binary");
        hit.category = QStringLiteral("binary");
        hit.length = 4;
        const bool valid = validateElf(prefix);
        hit.confidence = valid ? 99 : 60;
        hit.note = valid ? QStringLiteral("ELF identification fields validated")
                         : QStringLiteral("ELF magic only; identification fields are invalid");
        hits.append(hit);
    }

    if (prefix.size() >= 2 && prefix.startsWith(QByteArray("MZ", 2))) {
        SignatureHit hit;
        hit.name = QStringLiteral("Windows PE / DOS executable");
        hit.category = QStringLiteral("binary");
        hit.length = 2;
        const bool valid = validatePe(source, prefix);
        hit.confidence = valid ? 99 : 40;
        hit.note = valid ? QStringLiteral("PE header signature verified at e_lfanew")
                         : QStringLiteral("MZ stub only; PE header was not verified");
        hits.append(hit);
    }

    for (const Pattern& pattern : patterns()) {
        if (prefix.size() < pattern.magic.size() || !prefix.startsWith(pattern.magic))
            continue;

        SignatureHit hit;
        hit.name = QString::fromLatin1(pattern.name);
        hit.category = QString::fromLatin1(pattern.category);
        hit.length = static_cast<quint64>(pattern.magic.size());
        hit.confidence = pattern.confidence;
        hit.note = QString::fromLatin1(pattern.note);

        if (pattern.name == QByteArray("ZIP archive") && validateZipCentralDirectory(source)) {
            hit.confidence = 98;
            hit.note = QStringLiteral(
                "ZIP local header and end-of-central-directory record both present");
        }
        hits.append(hit);
    }

    return hits;
}

void appendEmbedded(QVector<SignatureHit>& hits, const QByteArray& prefix)
{
    static const QList<QByteArray> strong = {
        QByteArray("PK\x03\x04", 4),  QByteArray("\x89PNG\r\n\x1a\n", 8),
        QByteArray("\xff\xd8\xff", 3), QByteArray("%PDF-", 5),
        QByteArray("7z\xbc\xaf\x27\x1c", 6), QByteArray("\x7f" "PKG", 4),
        QByteArray("\x7f" "CNT", 4),  QByteArray("SQLite format 3\x00", 16),
    };

    for (const QByteArray& magic : strong) {
        int from = 0;
        while (hits.size() < kMaxHits) {
            const int found = prefix.indexOf(magic, from);
            if (found <= 0)  // offset 0 is reported by scanOffsetZero
                break;
            SignatureHit hit;
            for (const Pattern& pattern : patterns()) {
                if (pattern.magic == magic) {
                    hit.name = QString::fromLatin1(pattern.name);
                    hit.category = QString::fromLatin1(pattern.category);
                    hit.confidence = std::max(50, pattern.confidence - 20);
                    break;
                }
            }
            if (hit.name.isEmpty()) {
                hit.name = QStringLiteral("Embedded signature");
                hit.category = QStringLiteral("unknown");
                hit.confidence = 50;
            }
            hit.offset = static_cast<quint64>(found);
            hit.length = static_cast<quint64>(magic.size());
            hit.note = QStringLiteral(
                "embedded match inside the scanned prefix; the surrounding container was not "
                "validated");
            hits.append(hit);
            from = found + magic.size();
        }
    }
}

bool sameHit(const SignatureHit& left, const SignatureHit& right)
{
    return left.offset == right.offset && left.name == right.name;
}

}  // namespace

QVector<SignatureHit> SignatureScanner::scan(const FileSource& source, quint64 maxScanBytes)
{
    QVector<SignatureHit> hits;
    if (!source.isOpen())
        return hits;

    const QByteArray prefix = readPrefix(source, maxScanBytes);
    if (prefix.isEmpty())
        return hits;

    hits = scanOffsetZero(source, prefix);
    appendEmbedded(hits, prefix);

    QVector<SignatureHit> unique;
    for (const SignatureHit& hit : hits) {
        const bool duplicate = std::any_of(unique.cbegin(), unique.cend(),
                                           [&hit](const SignatureHit& existing) {
                                               return sameHit(existing, hit);
                                           });
        if (!duplicate)
            unique.append(hit);
    }

    std::sort(unique.begin(), unique.end(), [](const SignatureHit& left, const SignatureHit& right) {
        if (left.offset != right.offset)
            return left.offset < right.offset;
        return left.confidence > right.confidence;
    });
    if (unique.size() > kMaxHits)
        unique.resize(kMaxHits);
    return unique;
}

}  // namespace pkg
