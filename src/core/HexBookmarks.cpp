#include "core/HexBookmarks.h"

#include <QJsonObject>
#include <QJsonValue>

namespace pkg {

bool HexBookmarks::add(quint64 offset, const QString& note)
{
    const auto it = m_items.constFind(offset);
    if (it != m_items.constEnd() && it.value() == note)
        return false;
    m_items.insert(offset, note);
    return true;
}

bool HexBookmarks::remove(quint64 offset)
{
    return m_items.remove(offset) > 0;
}

bool HexBookmarks::toggle(quint64 offset, const QString& note)
{
    if (m_items.contains(offset)) {
        m_items.remove(offset);
        return false;
    }
    m_items.insert(offset, note);
    return true;
}

bool HexBookmarks::contains(quint64 offset) const
{
    return m_items.contains(offset);
}

QString HexBookmarks::note(quint64 offset) const
{
    return m_items.value(offset);
}

QList<HexBookmark> HexBookmarks::all() const
{
    QList<HexBookmark> result;
    result.reserve(m_items.size());
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it)
        result.append({it.key(), it.value()});
    return result;
}

QVector<quint64> HexBookmarks::offsets() const
{
    QVector<quint64> result;
    result.reserve(m_items.size());
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it)
        result.append(it.key());
    return result;
}

void HexBookmarks::clear()
{
    m_items.clear();
}

std::optional<quint64> HexBookmarks::next(quint64 from) const
{
    if (m_items.isEmpty())
        return std::nullopt;
    const auto it = m_items.upperBound(from);
    if (it != m_items.constEnd())
        return it.key();
    return m_items.constBegin().key();
}

std::optional<quint64> HexBookmarks::previous(quint64 from) const
{
    if (m_items.isEmpty())
        return std::nullopt;
    auto it = m_items.lowerBound(from);
    if (it != m_items.constBegin()) {
        --it;
        return it.key();
    }
    return (m_items.constEnd() - 1).key();
}

QJsonArray HexBookmarks::toJson() const
{
    QJsonArray array;
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
        QJsonObject object;
        object.insert(QStringLiteral("offset"), static_cast<double>(it.key()));
        if (!it.value().isEmpty())
            object.insert(QStringLiteral("note"), it.value());
        array.append(object);
    }
    return array;
}

HexBookmarks HexBookmarks::fromJson(const QJsonArray& array)
{
    HexBookmarks bookmarks;
    for (const QJsonValue& value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const QJsonValue offset = object.value(QStringLiteral("offset"));
        if (!offset.isDouble() || offset.toDouble() < 0)
            continue;
        bookmarks.add(static_cast<quint64>(offset.toDouble()),
                      object.value(QStringLiteral("note")).toString());
    }
    return bookmarks;
}

}  // namespace pkg
