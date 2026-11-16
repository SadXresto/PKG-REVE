#include "core/parsers/ParserSupport.h"

namespace pkg::parsers {

QString elfMachineName(quint16 machine)
{
    switch (machine) {
    case 0x03:
        return QStringLiteral("Intel 80386");
    case 0x08:
        return QStringLiteral("MIPS");
    case 0x14:
        return QStringLiteral("PowerPC");
    case 0x28:
        return QStringLiteral("ARM");
    case 0x2B:
        return QStringLiteral("SPARC");
    case 0x3E:
        return QStringLiteral("AMD x86-64");
    case 0xB7:
        return QStringLiteral("AArch64");
    case 0xF3:
        return QStringLiteral("RISC-V");
    default:
        return QStringLiteral("Unrecognized");
    }
}

QString elfTypeName(quint16 type)
{
    switch (type) {
    case 0:
        return QStringLiteral("NONE");
    case 1:
        return QStringLiteral("REL (relocatable)");
    case 2:
        return QStringLiteral("EXEC (executable)");
    case 3:
        return QStringLiteral("DYN (shared object / PIE)");
    case 4:
        return QStringLiteral("CORE (core dump)");
    default:
        return QStringLiteral("Unrecognized");
    }
}

QString peMachineName(quint16 machine)
{
    switch (machine) {
    case 0x014C:
        return QStringLiteral("x86");
    case 0x8664:
        return QStringLiteral("x64");
    case 0x01C0:
        return QStringLiteral("ARM");
    case 0x01C4:
        return QStringLiteral("ARMv7");
    case 0xAA64:
        return QStringLiteral("ARM64");
    default:
        return QStringLiteral("Unrecognized");
    }
}

QString peSubsystemName(quint16 subsystem)
{
    switch (subsystem) {
    case 1:
        return QStringLiteral("Native");
    case 2:
        return QStringLiteral("Windows GUI");
    case 3:
        return QStringLiteral("Windows console");
    case 5:
        return QStringLiteral("OS/2 console");
    case 7:
        return QStringLiteral("POSIX console");
    case 9:
        return QStringLiteral("Windows CE GUI");
    case 10:
        return QStringLiteral("EFI application");
    case 16:
        return QStringLiteral("Windows boot application");
    default:
        return QStringLiteral("Unrecognized");
    }
}

QString zipMethodName(quint16 method)
{
    switch (method) {
    case 0:
        return QStringLiteral("Stored");
    case 8:
        return QStringLiteral("Deflate");
    case 12:
        return QStringLiteral("BZip2");
    case 14:
        return QStringLiteral("LZMA");
    case 93:
        return QStringLiteral("Zstandard");
    case 99:
        return QStringLiteral("AES encrypted");
    default:
        return QStringLiteral("Method %1").arg(method);
    }
}

}  // namespace pkg::parsers
