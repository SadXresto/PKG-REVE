// Byte model used by the hex view.
//
// Rows are computed on demand from the memory mapped file; nothing is
// preloaded. Byte edits are kept as a sparse override map plus an undo/redo
// journal, so editing never rewrites the original file until an explicit save.
#pragma once

#include "core/CancelToken.h"
#include "core/FileSource.h"

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVector>

#include <functional>

namespace pkg {

struct HexLayout {
    int bytesPerRow = 16;
    int groupSize = 1;

    /// Clamps both values into the range the editor supports.
    [[nodiscard]] HexLayout normalized() const;
    [[nodiscard]] bool isValid() const { return bytesPerRow >= 8 && bytesPerRow <= 64; }
};

/// One reversible byte change in the undo journal.
struct HexEdit {
    quint64 offset = 0;
    quint8 before = 0;
    quint8 after = 0;
};

class HexDocument {
public:
    HexDocument() = default;
    explicit HexDocument(FileSource source);

    [[nodiscard]] bool isValid() const { return m_source.isOpen(); }
    [[nodiscard]] const FileSource& source() const { return m_source; }
    [[nodiscard]] QString path() const { return m_source.path(); }
    [[nodiscard]] quint64 size() const { return m_source.size(); }

    [[nodiscard]] quint64 rowCount(const HexLayout& layout) const;
    /// Bytes of a row with pending edits applied; a short final row is returned as-is.
    [[nodiscard]] Result<QByteArray> rowBytes(quint64 row, const HexLayout& layout) const;
    /// Effective byte (edit or original).
    [[nodiscard]] Result<quint8> byteAt(quint64 offset) const;

    /// Clamps an offset to the last valid byte (0 for empty files).
    [[nodiscard]] quint64 clampOffset(quint64 offset) const;

    // --- Editing ---------------------------------------------------------
    /// Stores `value` at `offset` and journals the change. Returns false when
    /// the offset is outside the file or the value is unchanged.
    bool setByte(quint64 offset, quint8 value);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    [[nodiscard]] bool canUndo() const { return m_editIndex > 0; }
    [[nodiscard]] bool canRedo() const { return m_editIndex < m_edits.size(); }
    [[nodiscard]] bool isModified() const { return !m_overrides.isEmpty(); }
    [[nodiscard]] int modifiedByteCount() const
    {
        return static_cast<int>(m_overrides.size());
    }
    [[nodiscard]] bool hasOverride(quint64 offset) const { return m_overrides.contains(offset); }

    /// Drops all pending edits and the undo history (used after a reload).
    void clearEdits();

    /// Reads `length` bytes with pending edits applied.
    [[nodiscard]] Result<QByteArray> readMerged(quint64 offset, quint64 length) const;

    /// Called with (bytes processed, total bytes) from the saving thread.
    using SaveProgress = std::function<void(quint64, quint64)>;

    /// Streams the entire file, with edits applied, into `destination` through a
    /// QSaveFile (temporary file + atomic rename). Returns the byte count.
    ///
    /// Safe to run on a worker thread while the UI only paints the document:
    /// callers must not mutate edits during the call. Cancelling `token` (or a
    /// failure) removes the temporary file and leaves the destination untouched.
    [[nodiscard]] Result<quint64> saveTo(const QString& destination,
                                         const CancelToken& token = CancelToken(),
                                         const SaveProgress& progress = {}) const;

    /// Re-opens the file (for example after saving) and clears edit state.
    [[nodiscard]] Status reloadFrom(const QString& path);

private:
    [[nodiscard]] Result<quint8> originalByteAt(quint64 offset) const;
    /// Writes an override, removing it when the value equals the original byte.
    void storeOverride(quint64 offset, quint8 value);

    FileSource m_source;
    QMap<quint64, quint8> m_overrides;
    QVector<HexEdit> m_edits;
    int m_editIndex = 0;
};

}  // namespace pkg
