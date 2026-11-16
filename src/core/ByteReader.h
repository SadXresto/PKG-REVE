// Bounds-checked reader for big/little endian binary structures.
//
// Every read validates the cursor and the remaining length; a malformed or
// truncated header produces an Error instead of an out-of-bounds access.
#pragma once

#include "core/Result.h"
#include "core/SafeMath.h"

#include <QByteArray>
#include <QString>
#include <array>
#include <cstring>
#include <span>

namespace pkg {

class ByteReader {
public:
    ByteReader() = default;
    explicit ByteReader(std::span<const std::byte> data, quint64 baseOffset = 0)
        : m_data(data), m_base(baseOffset)
    {
    }
    explicit ByteReader(const QByteArray& data, quint64 baseOffset = 0)
        : m_data(reinterpret_cast<const std::byte*>(data.constData()),
                 static_cast<std::size_t>(data.size())),
          m_base(baseOffset)
    {
    }
    ByteReader(const char* data, qsizetype size, quint64 baseOffset = 0)
        : m_data(reinterpret_cast<const std::byte*>(data),
                 static_cast<std::size_t>(size)),
          m_base(baseOffset)
    {
    }

    [[nodiscard]] quint64 size() const { return static_cast<quint64>(m_data.size()); }
    [[nodiscard]] quint64 base() const { return m_base; }
    [[nodiscard]] quint64 position() const { return m_pos; }
    [[nodiscard]] quint64 absolutePosition() const { return m_base + m_pos; }
    [[nodiscard]] quint64 remaining() const { return size() - m_pos; }
    [[nodiscard]] bool atEnd() const { return m_pos >= size(); }

    bool seek(quint64 position)
    {
        if (position > size())
            return false;
        m_pos = position;
        return true;
    }
    bool skip(quint64 count)
    {
        if (!safe::inRange(m_pos, count, size()))
            return false;
        m_pos += count;
        return true;
    }
    /// Signed skip used for backwards navigation; clamps nothing silently.
    bool skipRelative(qint64 delta)
    {
        if (delta >= 0)
            return skip(static_cast<quint64>(delta));
        const quint64 magnitude = static_cast<quint64>(-delta);
        if (magnitude > m_pos)
            return false;
        m_pos -= magnitude;
        return true;
    }

    [[nodiscard]] Result<std::span<const std::byte>> bytes(quint64 count)
    {
        if (!safe::inRange(m_pos, count, size()))
            return makeError(QStringLiteral("Read past end of data"),
                             QStringLiteral("offset %1, requested %2 bytes, available %3")
                                 .arg(absolutePosition())
                                 .arg(count)
                                 .arg(remaining()));
        auto view = m_data.subspan(static_cast<std::size_t>(m_pos),
                                   static_cast<std::size_t>(count));
        m_pos += count;
        return view;
    }

    [[nodiscard]] Result<std::span<const std::byte>> peek(quint64 count) const
    {
        if (!safe::inRange(m_pos, count, size()))
            return makeError(QStringLiteral("Read past end of data"));
        return m_data.subspan(static_cast<std::size_t>(m_pos),
                              static_cast<std::size_t>(count));
    }

    [[nodiscard]] Result<QByteArray> qbytes(quint64 count)
    {
        auto view = bytes(count);
        if (!view)
            return view.error();
        const auto sp = view.value();
        return QByteArray(reinterpret_cast<const char*>(sp.data()),
                          static_cast<qsizetype>(sp.size()));
    }

    [[nodiscard]] Result<quint8> u8()
    {
        auto view = bytes(1);
        if (!view)
            return view.error();
        return static_cast<quint8>(view.value()[0]);
    }

    [[nodiscard]] Result<quint16> u16be() { return readU16<true>(); }
    [[nodiscard]] Result<quint16> u16le() { return readU16<false>(); }
    [[nodiscard]] Result<quint32> u32be() { return readU32<true>(); }
    [[nodiscard]] Result<quint32> u32le() { return readU32<false>(); }
    [[nodiscard]] Result<quint64> u64be() { return readU64<true>(); }
    [[nodiscard]] Result<quint64> u64le() { return readU64<false>(); }

    /// Absolute, cursor-preserving reads.
    [[nodiscard]] Result<quint8> u8At(quint64 offset) const;
    [[nodiscard]] Result<quint16> u16beAt(quint64 offset) const;
    [[nodiscard]] Result<quint16> u16leAt(quint64 offset) const;
    [[nodiscard]] Result<quint32> u32beAt(quint64 offset) const;
    [[nodiscard]] Result<quint32> u32leAt(quint64 offset) const;
    [[nodiscard]] Result<quint64> u64beAt(quint64 offset) const;
    [[nodiscard]] Result<quint64> u64leAt(quint64 offset) const;

    /// ASCII/NUL terminated string, at most `maxLength` bytes.
    [[nodiscard]] Result<QString> fixedString(quint64 length, quint64 maxLength = 256);

    [[nodiscard]] const std::byte* raw() const { return m_data.data(); }

private:
    template <bool BigEndian>
    [[nodiscard]] Result<quint16> readU16()
    {
        auto view = bytes(2);
        if (!view)
            return view.error();
        const auto* p = view.value().data();
        if constexpr (BigEndian) {
            return static_cast<quint16>((static_cast<quint16>(p[0]) << 8) |
                                        static_cast<quint16>(p[1]));
        }
        return static_cast<quint16>((static_cast<quint16>(p[1]) << 8) |
                                    static_cast<quint16>(p[0]));
    }

    template <bool BigEndian>
    [[nodiscard]] Result<quint32> readU32()
    {
        auto view = bytes(4);
        if (!view)
            return view.error();
        const auto* p = view.value().data();
        quint32 v = 0;
        if constexpr (BigEndian) {
            for (int i = 0; i < 4; ++i)
                v = (v << 8) | static_cast<quint8>(p[i]);
        } else {
            for (int i = 3; i >= 0; --i)
                v = (v << 8) | static_cast<quint8>(p[i]);
        }
        return v;
    }

    template <bool BigEndian>
    [[nodiscard]] Result<quint64> readU64()
    {
        auto view = bytes(8);
        if (!view)
            return view.error();
        const auto* p = view.value().data();
        quint64 v = 0;
        if constexpr (BigEndian) {
            for (int i = 0; i < 8; ++i)
                v = (v << 8) | static_cast<quint8>(p[i]);
        } else {
            for (int i = 7; i >= 0; --i)
                v = (v << 8) | static_cast<quint8>(p[i]);
        }
        return v;
    }

    std::span<const std::byte> m_data{};
    quint64 m_base = 0;
    quint64 m_pos = 0;
};

}  // namespace pkg
