#include "core/HexDocument.h"

#include "core/Logging.h"
#include "core/SafeMath.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace pkg {

HexLayout HexLayout::normalized() const
{
    HexLayout layout;
    layout.groupSize = std::clamp(groupSize, 1, 16);
    const int clamped = std::clamp(bytesPerRow, 8, 64);
    // Keep the row a multiple of the group size so columns line up.
    layout.bytesPerRow = (clamped / layout.groupSize) * layout.groupSize;
    if (layout.bytesPerRow < layout.groupSize)
        layout.bytesPerRow = layout.groupSize;
    return layout;
}

HexDocument::HexDocument(FileSource source) : m_source(std::move(source)) {}

quint64 HexDocument::rowCount(const HexLayout& layout) const
{
    const int bpr = layout.normalized().bytesPerRow;
    if (bpr <= 0 || m_source.size() == 0)
        return 0;
    const quint64 perRow = static_cast<quint64>(bpr);
    return (m_source.size() + perRow - 1) / perRow;
}

Result<QByteArray> HexDocument::rowBytes(quint64 row, const HexLayout& layout) const
{
    const int bpr = layout.normalized().bytesPerRow;
    if (bpr <= 0)
        return makeError(QStringLiteral("Invalid hex layout"));
    const quint64 perRow = static_cast<quint64>(bpr);
    const auto start = safe::mul(row, perRow);
    if (!start)
        return makeError(QStringLiteral("Row offset overflow"));
    if (*start >= m_source.size())
        return QByteArray();
    const quint64 length = std::min<quint64>(perRow, m_source.size() - *start);
    return readMerged(*start, length);
}

Result<quint8> HexDocument::byteAt(quint64 offset) const
{
    if (offset >= m_source.size())
        return makeError(QStringLiteral("Offset outside file"));
    const auto override = m_overrides.constFind(offset);
    if (override != m_overrides.constEnd())
        return override.value();
    return originalByteAt(offset);
}

Result<quint8> HexDocument::originalByteAt(quint64 offset) const
{
    auto data = m_source.read(offset, 1);
    if (!data)
        return data.error();
    if (data.value().isEmpty())
        return makeError(QStringLiteral("Offset outside file"));
    return static_cast<quint8>(data.value().at(0));
}

quint64 HexDocument::clampOffset(quint64 offset) const
{
    if (m_source.size() == 0)
        return 0;
    return std::min(offset, m_source.size() - 1);
}

bool HexDocument::setByte(quint64 offset, quint8 value)
{
    if (offset >= m_source.size())
        return false;

    const auto current = byteAt(offset);
    if (!current)
        return false;
    if (current.value() == value)
        return false;

    // A new edit after undoing discards the redo branch.
    if (m_editIndex < m_edits.size())
        m_edits.resize(m_editIndex);

    HexEdit edit;
    edit.offset = offset;
    edit.before = current.value();
    edit.after = value;
    m_edits.append(edit);
    m_editIndex = static_cast<int>(m_edits.size());
    storeOverride(offset, value);
    return true;
}

void HexDocument::storeOverride(quint64 offset, quint8 value)
{
    const auto original = originalByteAt(offset);
    if (original && original.value() == value)
        m_overrides.remove(offset);
    else
        m_overrides.insert(offset, value);
}

bool HexDocument::undo()
{
    if (m_editIndex <= 0)
        return false;
    --m_editIndex;
    const HexEdit& edit = m_edits.at(m_editIndex);
    storeOverride(edit.offset, edit.before);
    return true;
}

bool HexDocument::redo()
{
    if (m_editIndex >= m_edits.size())
        return false;
    const HexEdit& edit = m_edits.at(m_editIndex);
    ++m_editIndex;
    storeOverride(edit.offset, edit.after);
    return true;
}

void HexDocument::clearEdits()
{
    m_overrides.clear();
    m_edits.clear();
    m_editIndex = 0;
}

Result<QByteArray> HexDocument::readMerged(quint64 offset, quint64 length) const
{
    if (!safe::inRange(offset, length, m_source.size()))
        return makeError(QStringLiteral("Read outside file"),
                         QStringLiteral("offset %1 length %2 size %3")
                             .arg(offset)
                             .arg(length)
                             .arg(m_source.size()));

    auto bytes = m_source.read(offset, length);
    if (!bytes)
        return bytes.error();
    QByteArray merged = bytes.value();
    if (m_overrides.isEmpty() || merged.isEmpty())
        return merged;

    const quint64 end = offset + length;
    auto it = m_overrides.lowerBound(offset);
    while (it != m_overrides.constEnd() && it.key() < end) {
        const auto index = static_cast<qsizetype>(it.key() - offset);
        if (index >= 0 && index < merged.size())
            merged[index] = static_cast<char>(it.value());
        ++it;
    }
    return merged;
}

Result<quint64> HexDocument::saveTo(const QString& destination, const CancelToken& token,
                                    const SaveProgress& progress) const
{
    if (!isValid())
        return makeError(QStringLiteral("No document is open"));
    if (destination.isEmpty())
        return makeError(QStringLiteral("Empty destination path"));
    if (token.isCancelled())
        return makeError(QStringLiteral("Save cancelled"), destination);

    const QFileInfo info(destination);
    const QString directory = info.absolutePath();
    if (!QDir().mkpath(directory))
        return makeError(QStringLiteral("Cannot create destination directory"), directory);

    QSaveFile file(destination);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return makeError(QStringLiteral("Cannot open file for writing"), file.errorString());

    constexpr quint64 kChunk = 4U * 1024U * 1024U;
    quint64 written = 0;
    const quint64 total = m_source.size();
    if (progress)
        progress(0, total);
    while (written < total) {
        if (token.isCancelled()) {
            file.cancelWriting();
            return makeError(QStringLiteral("Save cancelled"), destination);
        }
        const quint64 length = std::min<quint64>(kChunk, total - written);
        auto chunk = readMerged(written, length);
        if (!chunk) {
            file.cancelWriting();
            return chunk.error();
        }
        const QByteArray& bytes = chunk.value();
        if (file.write(bytes) != static_cast<qint64>(bytes.size())) {
            const QString reason = file.errorString();
            file.cancelWriting();
            return makeError(QStringLiteral("Short write while saving"), reason);
        }
        written += length;
        if (progress)
            progress(written, total);
    }

    if (token.isCancelled()) {
        file.cancelWriting();
        return makeError(QStringLiteral("Save cancelled"), destination);
    }
    if (!file.commit())
        return makeError(QStringLiteral("Cannot commit the saved file"), file.errorString());

    if (progress)
        progress(total, total);
    logInfo(QStringLiteral("file"),
            QStringLiteral("Saved %1 bytes to %2 (%3 edited byte(s), cancellable)")
                .arg(written)
                .arg(destination)
                .arg(m_overrides.size()));
    return written;
}

Status HexDocument::reloadFrom(const QString& path)
{
    auto source = FileSource::open(path);
    if (!source)
        return source.error();
    m_source = std::move(source.value());
    clearEdits();
    return okStatus();
}

}  // namespace pkg
