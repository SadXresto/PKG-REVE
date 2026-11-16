#include "core/parsers/PeParser.h"

#include "core/ByteReader.h"
#include "core/parsers/ParserSupport.h"
#include "core/SafeMath.h"

#include <QDateTime>
#include <QTimeZone>
#include <algorithm>

namespace pkg {
namespace {

constexpr quint64 kMaxSectionList = 32;
constexpr quint64 kMaxOptionalHeader = 4096;

QString characteristicsSummary(quint16 characteristics)
{
    QStringList flags;
    if ((characteristics & 0x0001U) != 0U)
        flags << QStringLiteral("relocations stripped");
    if ((characteristics & 0x0002U) != 0U)
        flags << QStringLiteral("executable");
    if ((characteristics & 0x0020U) != 0U)
        flags << QStringLiteral("large address aware");
    if ((characteristics & 0x0100U) != 0U)
        flags << QStringLiteral("32-bit");
    if ((characteristics & 0x2000U) != 0U)
        flags << QStringLiteral("DLL");
    return flags.isEmpty() ? QStringLiteral("none") : flags.join(QStringLiteral(", "));
}

QString timestampText(quint32 stamp)
{
    if (stamp == 0)
        return QStringLiteral("0 (not set)");
    const QDateTime when =
        QDateTime::fromSecsSinceEpoch(static_cast<qint64>(stamp), QTimeZone::UTC);
    if (!when.isValid() || when.date().year() < 1980 || when.date().year() > 2200)
        return fmt::hex(stamp);
    return when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + QStringLiteral(" UTC");
}

}  // namespace

int PeParser::probe(const FileSource& source, const CancelToken& token) const
{
    if (token.isCancelled() || source.size() < 0x40)
        return 0;
    auto dos = source.read(0, 2);
    if (!dos || dos.value() != QByteArrayLiteral("MZ"))
        return 0;
    auto lfanewBytes = source.read(0x3C, 4);
    if (!lfanewBytes)
        return 0;
    ByteReader reader(lfanewBytes.value());
    auto lfanew = reader.u32le();
    if (!lfanew)
        return 0;
    if (lfanew.value() < 0x40 || !safe::inRange(lfanew.value(), 4, source.size()))
        return 0;
    auto signature = source.read(lfanew.value(), 4);
    if (!signature)
        return 0;
    if (signature.value() == QByteArrayLiteral("PE\0\0"))
        return 100;
    return 0;
}

Result<PackageInfo> PeParser::parse(const FileSource& source, const CancelToken& token) const
{
    auto dosBytes = source.read(0, std::min<quint64>(source.size(), 0x40));
    if (!dosBytes)
        return dosBytes.error();
    const QByteArray dos = dosBytes.value();
    if (dos.size() < 0x40)
        return makeError(QStringLiteral("DOS header is truncated"),
                         QStringLiteral("file is only %1 bytes").arg(source.size()));

    ByteReader dosReader(dos);
    auto lfanew = dosReader.u32leAt(0x3C);
    if (!lfanew)
        return lfanew.error();

    PackageInfo info;
    info.kind = FormatKind::Pe;
    info.formatName = QStringLiteral("PE");
    info.parserId = id();
    info.parserVersion = version();

    InfoSection header;
    header.title = QStringLiteral("PE header");
    header.fields << parsers::field(QStringLiteral("DOS signature"), QStringLiteral("MZ"));
    header.fields << parsers::hexField(QStringLiteral("PE header offset"), lfanew.value(), 8);

    if (!safe::inRange(lfanew.value(), 24, source.size())) {
        info.partial = true;
        info.warnings << QStringLiteral("PE header offset 0x%1 lies outside the file.")
                             .arg(fmt::hex(lfanew.value()));
        info.sections << header;
        info.summary = QStringLiteral("Truncated or malformed PE image");
        return info;
    }

    auto coffBytes = source.read(lfanew.value() + 4, 20);
    if (!coffBytes)
        return coffBytes.error();
    ByteReader coff(coffBytes.value());
    auto machine = coff.u16le();
    auto sectionCount = coff.u16le();
    auto timestamp = coff.u32le();
    auto symbolTable = coff.u32le();
    auto symbolCount = coff.u32le();
    auto optionalSize = coff.u16le();
    auto characteristics = coff.u16le();
    if (!machine || !sectionCount || !timestamp || !symbolTable || !symbolCount ||
        !optionalSize || !characteristics)
        return makeError(QStringLiteral("PE COFF header is truncated"));

    header.fields << parsers::field(QStringLiteral("Machine"),
                                    QStringLiteral("%1 (%2)")
                                        .arg(parsers::peMachineName(machine.value()),
                                             fmt::hex(machine.value(), 4)));
    header.fields << parsers::field(QStringLiteral("Sections"),
                                    QString::number(sectionCount.value()));
    header.fields << parsers::field(QStringLiteral("Timestamp"),
                                    timestampText(timestamp.value()));
    header.fields << parsers::field(QStringLiteral("Characteristics"),
                                    characteristicsSummary(characteristics.value()));
    if (symbolCount.value() > 0)
        header.fields << parsers::hexField(QStringLiteral("COFF symbol table"),
                                           symbolTable.value(), 8);

    info.sections << header;

    quint64 imageBase = 0;
    quint64 entryRva = 0;
    quint16 subsystem = 0;
    const quint64 optionalOffset = lfanew.value() + 24;

    if (optionalSize.value() >= 2) {
        const quint64 readSize =
            std::min<quint64>(optionalSize.value(), kMaxOptionalHeader);
        if (!safe::inRange(optionalOffset, readSize, source.size())) {
            info.warnings << QStringLiteral("Optional header lies outside the file.");
            info.partial = true;
        } else {
            auto optBytes = source.read(optionalOffset, readSize);
            if (optBytes) {
                ByteReader opt(optBytes.value());
                auto magic = opt.u16le();
                if (magic) {
                    const bool pe32Plus = magic.value() == 0x20B;
                    const bool pe32 = magic.value() == 0x10B;
                    if (!pe32 && !pe32Plus) {
                        info.warnings << QStringLiteral("Optional header magic 0x%1 is not a "
                                                        "recognised PE32/PE32+ value.")
                                             .arg(fmt::hex(magic.value(), 4));
                        info.partial = true;
                    } else {
                        info.formatName = pe32Plus ? QStringLiteral("PE32+ (x64/ARM64)")
                                                   : QStringLiteral("PE32 (x86/ARM)");
                        if (auto entry = opt.u32leAt(16))
                            entryRva = entry.value();
                        if (pe32Plus) {
                            if (auto base = opt.u64leAt(24))
                                imageBase = base.value();
                        } else {
                            if (auto base = opt.u32leAt(28))
                                imageBase = base.value();
                        }
                        if (opt.size() >= 70) {
                            if (auto sub = opt.u16leAt(68))
                                subsystem = sub.value();
                        }
                        InfoSection optionalSection;
                        optionalSection.title = QStringLiteral("Optional header");
                        optionalSection.fields
                            << parsers::field(QStringLiteral("Format"), info.formatName)
                            << parsers::hexField(QStringLiteral("Image base"), imageBase,
                                                 pe32Plus ? 16 : 8)
                            << parsers::hexField(QStringLiteral("Entry point RVA"), entryRva, 8)
                            << parsers::field(QStringLiteral("Subsystem"),
                                              QStringLiteral("%1 (%2)")
                                                  .arg(parsers::peSubsystemName(subsystem))
                                                  .arg(subsystem));
                        info.sections << optionalSection;
                    }
                }
            }
        }
    } else {
        info.warnings << QStringLiteral("This PE image has no optional header (object file).");
        info.partial = true;
    }

    // Section table.
    if (sectionCount.value() > 0) {
        const quint64 tableOffset = optionalOffset + optionalSize.value();
        auto tableSize = safe::mul(static_cast<quint64>(sectionCount.value()), 40);
        if (!tableSize || !safe::inRange(tableOffset, *tableSize, source.size())) {
            info.warnings << QStringLiteral("Section table lies outside the file.");
            info.partial = true;
        } else {
            const quint64 listed = std::min<quint64>(sectionCount.value(), kMaxSectionList);
            auto table = source.read(tableOffset, listed * 40);
            if (table) {
                InfoSection sections;
                sections.title = QStringLiteral("Sections");
                for (quint64 i = 0; i < listed; ++i) {
                    if (token.isCancelled())
                        return makeError(QStringLiteral("Analysis cancelled"));
                    const quint64 base = i * 40;
                    auto view = source.view(tableOffset + base, 40);
                    if (!view)
                        break;
                    const auto data = view.value();
                    const auto span = [&data](quint64 from, quint64 length) {
                        return data.subspan(static_cast<std::size_t>(from),
                                            static_cast<std::size_t>(length));
                    };
                    QString name = fmt::printableString(span(0, 8));
                    name = name.trimmed();
                    if (name.isEmpty())
                        name = QStringLiteral("<unnamed>");

                    ByteReader entry(table.value());
                    if (!entry.seek(base + 8))
                        break;
                    auto virtualSize = entry.u32le();
                    auto virtualAddress = entry.u32le();
                    auto rawSize = entry.u32le();
                    auto rawPtr = entry.u32le();
                    if (!virtualSize || !virtualAddress || !rawSize || !rawPtr)
                        break;
                    sections.fields << parsers::field(
                        name,
                        QStringLiteral("VA 0x%1, virtual %2, raw %3 @ 0x%4")
                            .arg(fmt::hex(virtualAddress.value(), 8))
                            .arg(virtualSize.value())
                            .arg(rawSize.value())
                            .arg(fmt::hex(rawPtr.value(), 8)));
                }
                if (sectionCount.value() > listed) {
                    sections.fields << parsers::field(
                        QStringLiteral("..."),
                        QStringLiteral("%1 more sections not listed")
                            .arg(sectionCount.value() - listed));
                }
                info.sections << sections;
            }
        }
    }

    info.summary = QStringLiteral("%1, %2, %3 section(s)")
                       .arg(info.formatName, parsers::peMachineName(machine.value()))
                       .arg(sectionCount.value());
    return info;
}

}  // namespace pkg
