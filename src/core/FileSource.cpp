#include "core/FileSource.h"

#include "core/SafeMath.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cstring>
#include <limits>

#if defined(Q_OS_WIN)
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  include <cerrno>
#endif

namespace pkg {

FileSource::~FileSource()
{
    close();
}

FileSource::FileSource(FileSource&& other) noexcept
{
    *this = std::move(other);
}

FileSource& FileSource::operator=(FileSource&& other) noexcept
{
    if (this == &other)
        return *this;
    close();
    m_path = std::move(other.m_path);
    m_size = other.m_size;
    m_data = other.m_data;
    m_open = other.m_open;
#if defined(Q_OS_WIN)
    m_file = other.m_file;
    m_mapping = other.m_mapping;
    other.m_file = nullptr;
    other.m_mapping = nullptr;
#else
    m_fd = other.m_fd;
    other.m_fd = -1;
#endif
    other.m_data = nullptr;
    other.m_size = 0;
    other.m_open = false;
    other.m_path.clear();
    return *this;
}

void FileSource::close()
{
#if defined(Q_OS_WIN)
    if (m_data != nullptr)
        ::UnmapViewOfFile(m_data);
    if (m_mapping != nullptr)
        ::CloseHandle(static_cast<HANDLE>(m_mapping));
    if (m_file != nullptr)
        ::CloseHandle(static_cast<HANDLE>(m_file));
    m_file = nullptr;
    m_mapping = nullptr;
#else
    if (m_data != nullptr && m_size > 0)
        ::munmap(const_cast<std::byte*>(m_data), static_cast<std::size_t>(m_size));
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
#endif
    m_data = nullptr;
    m_size = 0;
    m_open = false;
}

Result<FileSource> FileSource::open(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return makeError(QStringLiteral("File does not exist"), path);
    if (!info.isFile())
        return makeError(QStringLiteral("Not a regular file"), path);

#if defined(Q_OS_WIN)
    const HANDLE file = ::CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),
                                      GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL,
                                      nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return makeError(QStringLiteral("Cannot open file"), path);

    LARGE_INTEGER fileSize{};
    if (!::GetFileSizeEx(file, &fileSize)) {
        ::CloseHandle(file);
        return makeError(QStringLiteral("Cannot determine file size"), path);
    }

    FileSource source;
    source.m_path = path;
    source.m_file = file;
    source.m_size = static_cast<quint64>(fileSize.QuadPart);

    if (source.m_size > 0) {
        const HANDLE mapping = ::CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping == nullptr) {
            ::CloseHandle(file);
            return makeError(QStringLiteral("Cannot map file into memory"), path);
        }
        const void* view = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        if (view == nullptr) {
            ::CloseHandle(mapping);
            ::CloseHandle(file);
            return makeError(QStringLiteral("Cannot map file into memory"), path);
        }
        source.m_mapping = mapping;
        source.m_data = static_cast<const std::byte*>(view);
    }
    source.m_open = true;
    return source;
#else
    const QByteArray native = QFile::encodeName(path);
    const int fd = ::open(native.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return makeError(QStringLiteral("Cannot open file"),
                         QString::fromLocal8Bit(std::strerror(errno)));

    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        return makeError(QStringLiteral("Cannot stat file"),
                         QString::fromLocal8Bit(std::strerror(errno)));
    }
    if (!S_ISREG(st.st_mode)) {
        ::close(fd);
        return makeError(QStringLiteral("Not a regular file"), path);
    }
    if (st.st_size < 0) {
        ::close(fd);
        return makeError(QStringLiteral("Invalid file size"), path);
    }

    FileSource source;
    source.m_path = path;
    source.m_fd = fd;
    source.m_size = static_cast<quint64>(st.st_size);

    if (source.m_size > 0) {
        if (source.m_size > std::numeric_limits<std::size_t>::max()) {
            ::close(fd);
            return makeError(QStringLiteral("File is too large to map on this platform"), path);
        }
        void* ptr = ::mmap(nullptr, static_cast<std::size_t>(source.m_size), PROT_READ,
                           MAP_PRIVATE, fd, 0);
        if (ptr == MAP_FAILED) {
            ::close(fd);
            return makeError(QStringLiteral("Cannot map file into memory"),
                             QString::fromLocal8Bit(std::strerror(errno)));
        }
        source.m_data = static_cast<const std::byte*>(ptr);
    }
    source.m_open = true;
    return source;
#endif
}

Result<std::span<const std::byte>> FileSource::view(quint64 offset, quint64 length) const
{
    if (!m_open)
        return makeError(QStringLiteral("File is not open"));
    if (!safe::inRange(offset, length, m_size))
        return makeError(QStringLiteral("Range outside file"),
                         QStringLiteral("offset %1, length %2, file size %3")
                             .arg(offset)
                             .arg(length)
                             .arg(m_size));
    if (length == 0)
        return std::span<const std::byte>{};
    return std::span<const std::byte>{m_data + offset, static_cast<std::size_t>(length)};
}

Result<QByteArray> FileSource::read(quint64 offset, quint64 length) const
{
    auto sp = view(offset, length);
    if (!sp)
        return sp.error();
    const auto bytes = sp.value();
    if (bytes.empty())
        return QByteArray();
    return QByteArray(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<qsizetype>(bytes.size()));
}

Result<QByteArray> FileSource::readAllCapped(quint64 maxBytes, const CancelToken& token) const
{
    if (!m_open)
        return makeError(QStringLiteral("File is not open"));
    if (token.isCancelled())
        return makeError(QStringLiteral("Operation cancelled"));

    QByteArray out;
    const quint64 total = std::min(m_size, maxBytes);
    out.reserve(static_cast<qsizetype>(total));

    constexpr quint64 kChunk = 1U << 20U;  // 1 MiB
    quint64 offset = 0;
    while (offset < total) {
        if (token.isCancelled())
            return makeError(QStringLiteral("Operation cancelled"));
        const quint64 length = std::min(kChunk, total - offset);
        auto chunk = read(offset, length);
        if (!chunk)
            return chunk.error();
        out.append(chunk.value());
        offset += length;
    }
    return out;
}

}  // namespace pkg
