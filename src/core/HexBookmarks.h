// Bookmark and annotation model for the hex editor.
//
// Bookmarks are kept out of the file itself: they are navigation aids stored
// alongside a workspace, never written into the bytes being inspected.
#pragma once

#include <QJsonArray>
#include <QList>
#include <QMap>
#include <QString>

#include <optional>

namespace pkg {

struct HexBookmark {
    quint64 offset = 0;
    QString note;

    [[nodiscard]] bool operator==(const HexBookmark& other) const
    {
        return offset == other.offset && note == other.note;
    }
};

class HexBookmarks {
public:
    /// Adds or updates the bookmark at `offset`. Returns false when a bookmark
    /// already exists with exactly the same note.
    bool add(quint64 offset, const QString& note = {});
    bool remove(quint64 offset);
    /// Adds when absent, removes when present. Returns the new state.
    bool toggle(quint64 offset, const QString& note = {});
    [[nodiscard]] bool contains(quint64 offset) const;
    [[nodiscard]] QString note(quint64 offset) const;
    [[nodiscard]] QList<HexBookmark> all() const;
    [[nodiscard]] QVector<quint64> offsets() const;
    [[nodiscard]] int count() const { return static_cast<int>(m_items.size()); }
    [[nodiscard]] bool isEmpty() const { return m_items.isEmpty(); }
    void clear();

    /// Nearest bookmark strictly after `from` (wraps to the first).
    [[nodiscard]] std::optional<quint64> next(quint64 from) const;
    /// Nearest bookmark strictly before `from` (wraps to the last).
    [[nodiscard]] std::optional<quint64> previous(quint64 from) const;

    [[nodiscard]] QJsonArray toJson() const;
    /// Ignores array entries without a numeric, non-negative offset.
    static HexBookmarks fromJson(const QJsonArray& array);

private:
    QMap<quint64, QString> m_items;
};

}  // namespace pkg
