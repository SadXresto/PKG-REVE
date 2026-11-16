// Read-only memory mapped file access.
//
// Packages can be many gigabytes; the editor and parsers stream through this
// class instead of loading file contents into RAM. The mapping is virtual
// memory, so only touched pages are resident.
#pragma once

#include "core/CancelToken.h"
#include "core/Result.h"

#include <QString>
#include <span>

namespace pkg {

class FileSource {
public:
    FileSource() = default;
    ~FileSource();

    FileSource(FileSource&& other) noexcept;
    FileSource& operator=(FileSource&& other) noexcept;
    FileSource(const FileSource&) = delete;
    FileSource& operator=(const FileSource&) = delete;

    /// Opens `path` read-only and maps it. Directories and special files are
    /// rejected; empty files open successfully with size() == 0.
    [[nodiscard]] static Result<FileSource> open(const QString& path);

    [[nodiscard]] bool isOpen() const { return m_open; }
    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] quint64 size() const { return m_size; }

    /// Zero-copy view. The span stays valid while this FileSource lives.
    [[nodiscard]] Result<std::span<const std::byte>> view(quint64 offset,
                                                          quint64 length) const;

    /// Copying read, intended for small header buffers.
    [[nodiscard]] Result<QByteArray> read(quint64 offset, quint64 length) const;

    /// Copies at most `maxBytes` from the start; used for previews and tests.
    [[nodiscard]] Result<QByteArray> readAllCapped(quint64 maxBytes,
                                                   const CancelToken& token) const;

private:
    void close();

    QString m_path;
    quint64 m_size = 0;
    const std::byte* m_data = nullptr;
    bool m_open = false;

#if defined(Q_OS_WIN)
    void* m_file = nullptr;
    void* m_mapping = nullptr;
#else
    int m_fd = -1;
#endif
};

}  // namespace pkg
