#include "core/parsers/ElfParser.h"

#include "core/ByteReader.h"
#include "core/parsers/ParserSupport.h"
#include "core/SafeMath.h"

#include <algorithm>

namespace pkg {
namespace {

constexpr quint64 kElf32HeaderSize = 52;
constexpr quint64 kElf64HeaderSize = 64;
constexpr quint64 kMaxProgramHeaderTable = 64 * 1024;
constexpr quint64 kMaxProgramHeaders = 1024;

QString osAbiName(quint8 abi)
{
    switch (abi) {
    case 0:
        return QStringLiteral("System V");
    case 1:
        return QStringLiteral("HP-UX");
    case 2:
        return QStringLiteral("NetBSD");
    case 3:
        return QStringLiteral("Linux");
    case 6:
        return QStringLiteral("Solaris");
    case 9:
        return QStringLiteral("FreeBSD");
    default:
        return QStringLiteral("Unrecognized");
    }
}

struct Endian {
    bool little = true;

    [[nodiscard]] Result<quint16> u16(ByteReader& reader) const
    {
        return little ? reader.u16le() : reader.u16be();
    }
    [[nodiscard]] Result<quint32> u32(ByteReader& reader) const
    {
        return little ? reader.u32le() : reader.u32be();
    }
    [[nodiscard]] Result<quint64> u64(ByteReader& reader) const
    {
        return little ? reader.u64le() : reader.u64be();
    }
};

}  // namespace

int ElfParser::probe(const FileSource& source, const CancelToken& token) const
{
    if (token.isCancelled() || source.size() < 4)
        return 0;
    auto magic = source.read(0, 4);
    if (!magic)
        return 0;
    const QByteArray& m = magic.value();
    if (static_cast<quint8>(m.at(0)) == 0x7FU && m.at(1) == 'E' && m.at(2) == 'L' &&
        m.at(3) == 'F')
        return 100;
    return 0;
}

Result<PackageInfo> ElfParser::parse(const FileSource& source, const CancelToken& token) const
{
    auto identBytes = source.read(0, std::min<quint64>(source.size(), 16));
    if (!identBytes)
        return identBytes.error();
    const QByteArray ident = identBytes.value();
    if (ident.size() < 16) {
        // The signature matched, so report a truncated image instead of an
        // unrelated parse error; the UI can then explain what is missing.
        PackageInfo truncated;
        truncated.kind = FormatKind::Elf;
        truncated.formatName = QStringLiteral("ELF");
        truncated.parserId = id();
        truncated.parserVersion = version();
        truncated.summary = QStringLiteral("Truncated ELF image");
        truncated.partial = true;
        truncated.warnings << QStringLiteral("ELF identification is truncated: only %1 of the 16 "
                                             "identification bytes are present.")
                                  .arg(ident.size());
        return truncated;
    }

    const auto elfClass = static_cast<quint8>(ident.at(4));
    const auto elfData = static_cast<quint8>(ident.at(5));
    const auto identVersion = static_cast<quint8>(ident.at(6));
    const auto osAbi = static_cast<quint8>(ident.at(7));
    const auto abiVersion = static_cast<quint8>(ident.at(8));

    if (elfClass != 1 && elfClass != 2)
        return makeError(QStringLiteral("Unsupported ELF class"),
                         QStringLiteral("e_ident[EI_CLASS] = %1").arg(elfClass));
    if (elfData != 1 && elfData != 2)
        return makeError(QStringLiteral("Unsupported ELF byte order"),
                         QStringLiteral("e_ident[EI_DATA] = %1").arg(elfData));

    const bool is64 = elfClass == 2;
    const Endian endian{elfData == 1};
    const quint64 headerSize = is64 ? kElf64HeaderSize : kElf32HeaderSize;

    PackageInfo info;
    info.kind = FormatKind::Elf;
    info.formatName = is64 ? QStringLiteral("ELF64") : QStringLiteral("ELF32");
    info.parserId = id();
    info.parserVersion = version();

    InfoSection header;
    header.title = QStringLiteral("ELF header");
    header.fields << parsers::field(QStringLiteral("Class"), info.formatName);
    header.fields << parsers::field(QStringLiteral("Byte order"),
                                    endian.little ? QStringLiteral("Little endian")
                                                  : QStringLiteral("Big endian"));
    header.fields << parsers::field(QStringLiteral("Ident version"), fmt::hex(identVersion));
    header.fields << parsers::field(QStringLiteral("OS ABI"), osAbiName(osAbi));
    if (abiVersion != 0)
        header.fields << parsers::field(QStringLiteral("ABI version"), fmt::hex(abiVersion));

    if (source.size() < headerSize) {
        info.partial = true;
        info.warnings << QStringLiteral("ELF header is truncated: file is %1 bytes but an %2 "
                                        "header needs %3 bytes.")
                             .arg(source.size())
                             .arg(info.formatName)
                             .arg(headerSize);
        info.sections << header;
        info.summary = QStringLiteral("Truncated ELF image");
        return info;

    }

    auto headerBytes = source.read(0, headerSize);
    if (!headerBytes)
        return headerBytes.error();
    ByteReader reader(headerBytes.value());
    auto seek = [&reader](quint64 offset) {
        const bool ok = reader.seek(offset);
        Q_ASSERT(ok);
        Q_UNUSED(ok)
    };

    seek(16);
    auto eType = endian.u16(reader);
    auto eMachine = endian.u16(reader);
    auto eVersion = endian.u32(reader);
    Q_UNUSED(eVersion)
    quint64 entry = 0;
    quint64 phoff = 0;
    quint64 shoff = 0;
    quint16 phentsize = 0;
    quint16 phnum = 0;
    quint16 shentsize = 0;
    quint16 shnum = 0;
    quint16 ehsize = 0;

    if (is64) {
        seek(24);
        auto vEntry = endian.u64(reader);
        auto vPhoff = endian.u64(reader);
        auto vShoff = endian.u64(reader);
        auto eFlags = endian.u32(reader);
        auto vEhsize = endian.u16(reader);
        auto vPhentsize = endian.u16(reader);
        auto vPhnum = endian.u16(reader);
        auto vShentsize = endian.u16(reader);
        auto vShnum = endian.u16(reader);
        auto eShstrndx = endian.u16(reader);
        Q_UNUSED(eFlags)
        Q_UNUSED(eShstrndx)
        if (!vEntry || !vPhoff || !vShoff || !vEhsize || !vPhentsize || !vPhnum ||
            !vShentsize || !vShnum) {
            info.partial = true;
            info.warnings << QStringLiteral("ELF64 header fields could not be read completely.");
            info.sections << header;
            return info;
        }
        entry = vEntry.value();
        phoff = vPhoff.value();
        shoff = vShoff.value();
        ehsize = vEhsize.value();
        phentsize = vPhentsize.value();
        phnum = vPhnum.value();
        shentsize = vShentsize.value();
        shnum = vShnum.value();
    } else {
        seek(24);
        auto vEntry = endian.u32(reader);
        auto vPhoff = endian.u32(reader);
        auto vShoff = endian.u32(reader);
        auto eFlags = endian.u32(reader);
        auto vEhsize = endian.u16(reader);
        auto vPhentsize = endian.u16(reader);
        auto vPhnum = endian.u16(reader);
        auto vShentsize = endian.u16(reader);
        auto vShnum = endian.u16(reader);
        auto eShstrndx = endian.u16(reader);
        Q_UNUSED(eFlags)
        Q_UNUSED(eShstrndx)
        if (!vEntry || !vPhoff || !vShoff || !vEhsize || !vPhentsize || !vPhnum ||
            !vShentsize || !vShnum) {
            info.partial = true;
            info.warnings << QStringLiteral("ELF32 header fields could not be read completely.");
            info.sections << header;
            return info;
        }
        entry = vEntry.value();
        phoff = vPhoff.value();
        shoff = vShoff.value();
        ehsize = vEhsize.value();
        phentsize = vPhentsize.value();
        phnum = vPhnum.value();
        shentsize = vShentsize.value();
        shnum = vShnum.value();
    }

    if (!eType || !eMachine) {
        info.partial = true;
        info.warnings << QStringLiteral("ELF type or machine field is unreadable.");
        info.sections << header;
        return info;
    }

    header.fields << parsers::field(QStringLiteral("Type"),
                                    QStringLiteral("%1 (%2)")
                                        .arg(parsers::elfTypeName(eType.value()),
                                             fmt::hex(eType.value(), 4)));
    header.fields << parsers::field(QStringLiteral("Machine"),
                                    QStringLiteral("%1 (%2)")
                                        .arg(parsers::elfMachineName(eMachine.value()),
                                             fmt::hex(eMachine.value(), 4)));
    header.fields << parsers::hexField(QStringLiteral("Entry point"), entry, 16);
    header.fields << parsers::hexField(QStringLiteral("Program header offset"), phoff, 8);
    header.fields << parsers::field(QStringLiteral("Program headers"),
                                    QStringLiteral("%1 x %2 bytes")
                                        .arg(phnum)
                                        .arg(phentsize));
    header.fields << parsers::hexField(QStringLiteral("Section header offset"), shoff, 8);
    header.fields << parsers::field(QStringLiteral("Section headers"),
                                    QStringLiteral("%1 x %2 bytes")
                                        .arg(shnum)
                                        .arg(shentsize));
    header.fields << parsers::field(QStringLiteral("ELF header size"),
                                    QStringLiteral("%1 bytes").arg(ehsize));
    info.sections << header;

    info.summary = QStringLiteral("%1 %2, %3, entry 0x%4")
                       .arg(info.formatName,
                            endian.little ? QStringLiteral("LE") : QStringLiteral("BE"),
                            parsers::elfMachineName(eMachine.value()),
                            fmt::hex(entry));

    // Walk the program header table (bounded) to find PT_INTERP.
    const quint64 entrySize = phentsize;
    const quint64 count = phnum;
    if (count == 0)
        return info;

    if (count > kMaxProgramHeaders) {
        info.warnings << QStringLiteral("Program header count %1 exceeds the analysis cap of "
                                        "%2; headers were not walked.")
                             .arg(count)
                             .arg(kMaxProgramHeaders);
        info.partial = true;
        return info;
    }
    auto tableSize = safe::mul(count, entrySize);
    if (!tableSize || entrySize < (is64 ? 56U : 32U)) {
        info.warnings << QStringLiteral("Program header entry size %1 is invalid; headers were "
                                        "not walked.")
                             .arg(entrySize);
        info.partial = true;
        return info;
    }
    const auto tableEnd = safe::add(phoff, *tableSize);
    if (!tableEnd || *tableEnd > source.size()) {
        info.warnings << QStringLiteral("Program header table lies outside the file.");
        info.partial = true;
        return info;
    }
    if (*tableSize > kMaxProgramHeaderTable) {
        info.warnings << QStringLiteral("Program header table is larger than 64 KiB; headers "
                                        "were not walked.");
        info.partial = true;
        return info;
    }

    auto table = source.read(phoff, *tableSize);
    if (!table)
        return table.error();

    const quint64 offsetField = is64 ? 8 : 4;
    const quint64 fileSizeField = is64 ? 32 : 16;

    for (quint64 i = 0; i < count; ++i) {
        if (token.isCancelled())
            return makeError(QStringLiteral("Analysis cancelled"));
        ByteReader ph(table.value());
        const quint64 entryStart = i * entrySize;
        if (!ph.seek(entryStart))
            break;
        auto type = endian.u32(ph);
        if (!type)
            break;
        if (type.value() != 3)  // PT_INTERP
            continue;

        if (!ph.seek(entryStart + offsetField))
            break;
        Result<quint64> interpOffset = is64
                                           ? endian.u64(ph)
                                           : Result<quint64>(static_cast<quint64>(
                                                 endian.u32(ph).valueOr(0)));
        if (!ph.seek(entryStart + fileSizeField))
            break;
        Result<quint64> interpSize = is64
                                         ? endian.u64(ph)
                                         : Result<quint64>(static_cast<quint64>(
                                               endian.u32(ph).valueOr(0)));
        if (!interpOffset || !interpSize)
            break;

        const quint64 length = std::min<quint64>(interpSize.value(), 1024);
        if (!safe::inRange(interpOffset.value(), length, source.size())) {
            info.warnings << QStringLiteral("PT_INTERP range lies outside the file.");
            info.partial = true;
            break;
        }
        auto text = source.read(interpOffset.value(), length);
        if (text) {
            const QByteArray& path = text.value();
            const auto bytes = std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(path.constData()),
                static_cast<std::size_t>(path.size()));
            info.sections.insert(0, InfoSection{QStringLiteral("Interpreter"),
                                                {parsers::stringField(
                                                    QStringLiteral("Path"), bytes)}});
        }
        break;
    }

    return info;
}

}  // namespace pkg
