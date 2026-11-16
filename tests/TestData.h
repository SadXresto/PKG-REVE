// Synthetic fixtures for the automated test suite.
//
// Every byte here is produced by this file: no third-party or commercial
// package content is used, so the suite is reproducible and redistributable.
#pragma once

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QPair>
#include <QString>

#include <cstdint>

namespace pkg::testdata {

inline void putLE16(QByteArray& out, int offset, quint16 value)
{
    out[offset] = static_cast<char>(value & 0xFF);
    out[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
}

inline void putLE32(QByteArray& out, int offset, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        out[offset + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
}

inline void putLE64(QByteArray& out, int offset, quint64 value)
{
    for (int i = 0; i < 8; ++i)
        out[offset + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
}

inline void putBE16(QByteArray& out, int offset, quint16 value)
{
    out[offset] = static_cast<char>((value >> 8) & 0xFF);
    out[offset + 1] = static_cast<char>(value & 0xFF);
}

inline void putBE32(QByteArray& out, int offset, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        out[offset + i] = static_cast<char>((value >> (8 * (3 - i))) & 0xFF);
}

inline void putBE64(QByteArray& out, int offset, quint64 value)
{
    for (int i = 0; i < 8; ++i)
        out[offset + i] = static_cast<char>((value >> (8 * (7 - i))) & 0xFF);
}

inline QByteArray makeElf64(bool withInterpreter = true)
{
    QByteArray data(0x200, '\0');
    data[0] = '\x7f';
    data[1] = 'E';
    data[2] = 'L';
    data[3] = 'F';
    data[4] = 2;  // ELF64
    data[5] = 1;  // little endian
    data[6] = 1;
    data[7] = 0;  // System V

    putLE16(data, 16, 2);         // ET_EXEC
    putLE16(data, 18, 0x3E);      // x86-64
    putLE32(data, 20, 1);
    putLE64(data, 24, 0x401000);  // entry
    putLE64(data, 32, 0x40);      // phoff
    putLE64(data, 40, 0);         // shoff
    putLE32(data, 48, 0);
    putLE16(data, 52, 64);        // ehsize
    putLE16(data, 54, 56);        // phentsize
    putLE16(data, 56, withInterpreter ? 1 : 0);
    putLE16(data, 58, 64);
    putLE16(data, 60, 0);
    putLE16(data, 62, 0);

    if (withInterpreter) {
        const QByteArray interpreter("/lib64/ld-linux-x86-64.so.2");
        putLE32(data, 0x40, 3);          // PT_INTERP
        putLE64(data, 0x48, 0x100);      // p_offset
        putLE64(data, 0x60, interpreter.size() + 1);  // p_filesz
        for (int i = 0; i < interpreter.size(); ++i)
            data[0x100 + i] = interpreter.at(i);
    }
    return data;
}

inline QByteArray makePe64()
{
    QByteArray data(0x800, '\0');
    data[0] = 'M';
    data[1] = 'Z';
    const int peOffset = 0x80;
    putLE32(data, 0x3C, static_cast<quint32>(peOffset));
    data[peOffset] = 'P';
    data[peOffset + 1] = 'E';
    data[peOffset + 2] = '\0';
    data[peOffset + 3] = '\0';

    const int coff = peOffset + 4;
    putLE16(data, coff, 0x8664);      // x64
    putLE16(data, coff + 2, 2);       // two sections
    putLE32(data, coff + 4, 0x60000000);
    putLE16(data, coff + 16, 240);    // optional header size
    putLE16(data, coff + 18, 0x0022);

    const int optional = coff + 20;
    putLE16(data, optional, 0x20B);       // PE32+
    putLE32(data, optional + 16, 0x1000);
    putLE64(data, optional + 24, 0x140000000);
    putLE16(data, optional + 68, 3);      // console

    const int sections = optional + 240;
    const QByteArray textName(".text");
    for (int i = 0; i < textName.size(); ++i)
        data[sections + i] = textName.at(i);
    putLE32(data, sections + 8, 0x200);
    putLE32(data, sections + 12, 0x1000);
    putLE32(data, sections + 16, 0x200);
    putLE32(data, sections + 20, 0x400);

    const int second = sections + 40;
    const QByteArray dataName(".data");
    for (int i = 0; i < dataName.size(); ++i)
        data[second + i] = dataName.at(i);
    putLE32(data, second + 8, 0x100);
    putLE32(data, second + 12, 0x2000);
    return data;
}

/// Minimal ZIP writer (stored entries only) used for APK/ZIP parser tests.
inline QByteArray makeZip(const QList<QPair<QString, QByteArray>>& entries)
{
    QByteArray output;
    struct CentralRecord {
        QByteArray name;
        QByteArray data;
        quint32 offset = 0;
        quint32 crc = 0;
    };
    QList<CentralRecord> records;

    for (const auto& entry : entries) {
        CentralRecord record;
        record.name = entry.first.toUtf8();
        record.data = entry.second;
        record.offset = static_cast<quint32>(output.size());

        QByteArray local(30, '\0');
        putLE32(local, 0, 0x04034B50);
        putLE16(local, 4, 20);
        putLE16(local, 8, 0);  // stored
        putLE32(local, 14, static_cast<quint32>(record.data.size()));
        putLE32(local, 18, static_cast<quint32>(record.data.size()));
        putLE16(local, 26, static_cast<quint16>(record.name.size()));
        local += record.name;
        local += record.data;
        output += local;
        records.append(record);
    }

    const int centralStart = output.size();
    for (const CentralRecord& record : records) {
        QByteArray header(46, '\0');
        putLE32(header, 0, 0x02014B50);
        putLE16(header, 4, 20);
        putLE16(header, 6, 20);
        putLE16(header, 10, 0);
        putLE32(header, 20, static_cast<quint32>(record.data.size()));
        putLE32(header, 24, static_cast<quint32>(record.data.size()));
        putLE16(header, 28, static_cast<quint16>(record.name.size()));
        putLE32(header, 42, record.offset);
        output += header;
        output += record.name;
    }
    const int centralSize = output.size() - centralStart;

    QByteArray end(22, '\0');
    putLE32(end, 0, 0x06054B50);
    putLE16(end, 8, static_cast<quint16>(records.size()));
    putLE16(end, 10, static_cast<quint16>(records.size()));
    putLE32(end, 12, static_cast<quint32>(centralSize));
    putLE32(end, 16, static_cast<quint32>(centralStart));
    output += end;
    return output;
}

inline QByteArray makeApk()
{
    return makeZip({
        {QStringLiteral("AndroidManifest.xml"), QByteArray("\x03\x00\x08\x00manifest", 12)},
        {QStringLiteral("classes.dex"), QByteArray("dex\n035\x00", 8)},
    });
}

inline QByteArray makeVitaPkg()
{
    const QByteArray contentId = QByteArray("UP0000-PCSG00001_00-0000000000000001").leftJustified(0x30, '\0');
    const quint64 dataOffset = 0x1000;
    const QByteArray payload(0x200, '\0');
    const quint64 total = dataOffset + static_cast<quint64>(payload.size());

    QByteArray data(static_cast<int>(total), '\0');
    putBE32(data, 0x00, 0x7F504B47);
    putBE16(data, 0x04, 0x8000);
    putBE16(data, 0x06, 2);
    putBE32(data, 0x08, 0x280);
    putBE32(data, 0x0C, 8);
    putBE32(data, 0x10, 0x160);
    putBE32(data, 0x14, 3);
    putBE64(data, 0x18, total);
    putBE64(data, 0x20, dataOffset);
    putBE64(data, 0x28, static_cast<quint64>(payload.size()));
    for (int i = 0; i < contentId.size(); ++i)
        data[0x30 + i] = contentId.at(i);
    return data;
}

inline QByteArray makePs4Pkg()
{
    const QByteArray contentId = QByteArray("UP0000-CUSA00001_00-0000000000000001");
    const quint64 total = 0x1800;
    QByteArray data(static_cast<int>(total), '\0');
    putBE32(data, 0x00, 0x7F434E54);
    putBE32(data, 0x04, 0x1A);
    putBE32(data, 0x0C, 5);
    putBE32(data, 0x10, 5);
    putBE16(data, 0x14, 0);
    putBE16(data, 0x16, 5);
    putBE32(data, 0x18, 0x1000);
    putBE32(data, 0x1C, 0x500);
    putBE64(data, 0x20, 0x2000);
    putBE64(data, 0x28, 0x1000);
    putBE64(data, 0x30, 0x3000);
    putBE64(data, 0x38, 0x800);
    for (int i = 0; i < contentId.size(); ++i)
        data[0x40 + i] = contentId.at(i);
    putBE32(data, 0x70, 1);
    putBE32(data, 0x74, 0x1A);
    putBE32(data, 0x78, 0);
    putBE32(data, 0x404, 1);
    putBE64(data, 0x410, 0x3000);
    putBE64(data, 0x418, 0x800);
    putBE64(data, 0x430, total);
    return data;
}

inline QString writeTempFile(const QString& directory, const QString& name,
                             const QByteArray& content)
{
    const QString path = directory + QLatin1Char('/') + name;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(content);
    file.close();
    return path;
}

}  // namespace pkg::testdata
