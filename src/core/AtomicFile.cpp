#include "core/AtomicFile.h"

#include "core/FileSource.h"
#include "core/Logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace pkg::atomic {

Status writeFile(const QString& path, const QByteArray& data)
{
    if (path.isEmpty())
        return makeError(QStringLiteral("Empty destination path"));

    const QFileInfo info(path);
    const QString directory = info.absolutePath();
    if (!QDir().mkpath(directory))
        return makeError(QStringLiteral("Cannot create directory"), directory);

    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return makeError(QStringLiteral("Cannot open file for writing"),
                         file.errorString());

    const qint64 written = file.write(data);
    if (written != static_cast<qint64>(data.size())) {
        file.cancelWriting();
        return makeError(QStringLiteral("Short write"),
                         QStringLiteral("wrote %1 of %2 bytes").arg(written).arg(data.size()));
    }
    if (!file.commit())
        return makeError(QStringLiteral("Cannot commit file"), file.errorString());
    return okStatus();
}

Result<QByteArray> readFile(const QString& path, quint64 maxBytes)
{
    const QFileInfo info(path);
    if (!info.exists())
        return makeError(QStringLiteral("File does not exist"), path);
    if (info.isFile() && static_cast<quint64>(info.size()) > maxBytes)
        return makeError(QStringLiteral("File exceeds the size limit"),
                         QStringLiteral("%1 bytes > %2 bytes").arg(info.size()).arg(maxBytes));

    auto source = FileSource::open(path);
    if (!source)
        return source.error();
    CancelToken token;
    return source.value().readAllCapped(maxBytes, token);
}

Result<quint64> backupFile(const QString& source, const QString& backupPath,
                           const CancelToken& token,
                           const std::function<void(quint64, quint64)>& progress)
{
    if (token.isCancelled())
        return makeError(QStringLiteral("Backup cancelled"), source);

    auto src = FileSource::open(source);
    if (!src)
        return src.error();

    const QFileInfo info(source);
    const QString directory = QFileInfo(backupPath).absolutePath();
    if (!QDir().mkpath(directory))
        return makeError(QStringLiteral("Cannot create backup directory"), directory);

    QSaveFile file(backupPath);
    if (!file.open(QIODevice::WriteOnly))
        return makeError(QStringLiteral("Cannot create backup"), file.errorString());

    constexpr quint64 kChunk = 4U * 1024U * 1024U;
    const quint64 totalSize = static_cast<quint64>(std::max<qint64>(0, info.size()));
    quint64 copied = 0;
    if (progress)
        progress(0, totalSize);
    while (copied < totalSize) {
        if (token.isCancelled()) {
            file.cancelWriting();
            return makeError(QStringLiteral("Backup cancelled"), backupPath);
        }
        const quint64 length = std::min<quint64>(kChunk, totalSize - copied);
        auto chunk = src.value().view(copied, length);
        if (!chunk) {
            file.cancelWriting();
            return chunk.error();
        }
        const auto bytes = chunk.value();
        if (file.write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<qint64>(bytes.size())) < 0) {
            file.cancelWriting();
            return makeError(QStringLiteral("Cannot write backup"), file.errorString());
        }
        copied += length;
        if (progress)
            progress(copied, totalSize);
    }
    if (token.isCancelled()) {
        file.cancelWriting();
        return makeError(QStringLiteral("Backup cancelled"), backupPath);
    }
    if (!file.commit())
        return makeError(QStringLiteral("Cannot commit backup"), file.errorString());

    if (progress)
        progress(totalSize, totalSize);
    logInfo(QStringLiteral("file"),
            QStringLiteral("Backup written: %1 (%2 bytes, cancellable)")
                .arg(backupPath)
                .arg(copied));
    return copied;
}

}  // namespace pkg::atomic
