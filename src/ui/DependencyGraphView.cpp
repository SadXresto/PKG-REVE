#include "ui/DependencyGraphView.h"

#include "data/ThemeManager.h"

#include <QFontMetrics>
#include <QHash>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace pkg::ui {
namespace {

constexpr qreal kNodeWidth = 154.0;
constexpr qreal kNodeHeight = 44.0;
constexpr qreal kGapX = 86.0;
constexpr qreal kGapY = 18.0;
constexpr qreal kMargin = 28.0;
constexpr int kMaxLayoutDepth = 12;

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

} // namespace

DependencyGraphView::DependencyGraphView(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("dependencyGraphView"));
    setMinimumSize(260, 180);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
}

void DependencyGraphView::setGraph(const gameforge::DependencyGraph& graph)
{
    m_graph = graph;
    m_selected.clear();
    m_hovered.clear();
    relayout();
    fitToWidget();
    update();
}

void DependencyGraphView::setFilter(const QString& text)
{
    m_filter = text.trimmed();
    update();
}

int DependencyGraphView::visibleNodeCount() const
{
    int count = 0;
    for (const auto& node : m_graph.nodes) {
        if (matchesFilter(node))
            ++count;
    }
    return count;
}

bool DependencyGraphView::matchesFilter(const gameforge::GraphNode& node) const
{
    if (m_filter.isEmpty())
        return true;
    return node.label.contains(m_filter, Qt::CaseInsensitive)
        || node.file.contains(m_filter, Qt::CaseInsensitive)
        || gameforge::nodeKindName(node.kind).contains(m_filter, Qt::CaseInsensitive);
}

void DependencyGraphView::relayout()
{
    m_centers = QVector<QPointF>(m_graph.nodes.size());
    m_bounds = QRectF();
    if (m_graph.nodes.isEmpty())
        return;

    QHash<QString, int> indexById;
    indexById.reserve(m_graph.nodes.size());
    for (int i = 0; i < m_graph.nodes.size(); ++i)
        indexById.insert(m_graph.nodes.at(i).id, i);

    QVector<QHash<int, bool>> outgoing(m_graph.nodes.size());
    QVector<QVector<int>> next(m_graph.nodes.size());
    for (const gameforge::GraphEdge& edge : m_graph.edges) {
        const int from = indexById.value(edge.from, -1);
        const int to = indexById.value(edge.to, -1);
        if (from < 0 || to < 0 || from == to)
            continue;
        if (outgoing[from].contains(to))
            continue;
        outgoing[from].insert(to, true);
        next[from].append(to);
    }

    QVector<int> depth(m_graph.nodes.size(), -1);
    QVector<int> queue;
    // The project node (or the first node when absent) is the root column.
    const int root = m_graph.find(QStringLiteral("project")) != nullptr
        ? indexById.value(QStringLiteral("project"))
        : 0;
    depth[root] = 0;
    queue.append(root);
    for (int head = 0; head < queue.size(); ++head) {
        const int current = queue.at(head);
        for (int child : next.at(current)) {
            if (depth.at(child) >= 0)
                continue;
            depth[child] = std::min(depth.at(current) + 1, kMaxLayoutDepth);
            queue.append(child);
        }
    }
    int maxDepth = 0;
    for (int i = 0; i < depth.size(); ++i) {
        if (depth.at(i) < 0)
            depth[i] = kMaxLayoutDepth; // unreachable nodes share the last column
        maxDepth = std::max(maxDepth, depth.at(i));
    }

    QVector<QVector<int>> columns(maxDepth + 1);
    for (int i = 0; i < depth.size(); ++i) {
        columns[depth.at(i)].append(i);
    }
    for (QVector<int>& column : columns) {
        std::sort(column.begin(), column.end(), [this](int left, int right) {
            return QString::compare(m_graph.nodes.at(left).label,
                                    m_graph.nodes.at(right).label,
                                    Qt::CaseInsensitive)
                < 0;
        });
    }

    qreal widest = 0;
    for (int columnIndex = 0; columnIndex < columns.size(); ++columnIndex) {
        const QVector<int>& column = columns.at(columnIndex);
        const qreal x = kMargin + columnIndex * (kNodeWidth + kGapX) + kNodeWidth / 2.0;
        for (int row = 0; row < column.size(); ++row) {
            const qreal y = kMargin + row * (kNodeHeight + kGapY) + kNodeHeight / 2.0;
            m_centers[column.at(row)] = QPointF(x, y);
            widest = std::max(widest, x + kNodeWidth / 2.0);
        }
        widest = std::max(widest, x + kNodeWidth / 2.0);
    }

    qreal tallest = 0;
    for (int columnIndex = 0; columnIndex < columns.size(); ++columnIndex)
        tallest = std::max(
            tallest,
            static_cast<qreal>(columns.at(columnIndex).size()) * (kNodeHeight + kGapY));
    m_bounds = QRectF(0, 0, widest + kMargin, tallest + kMargin * 2.0);
}

QRectF DependencyGraphView::nodeRect(int index) const
{
    if (index < 0 || index >= m_centers.size())
        return QRectF();
    const QPointF center = m_centers.at(index);
    return QRectF(center.x() - kNodeWidth / 2.0, center.y() - kNodeHeight / 2.0, kNodeWidth,
                  kNodeHeight);
}

QPointF DependencyGraphView::toScreen(const QPointF& world) const
{
    return world * m_zoom + m_offset;
}

QPointF DependencyGraphView::toWorld(const QPointF& screen) const
{
    return (screen - m_offset) / m_zoom;
}

void DependencyGraphView::fitToWidget()
{
    if (m_bounds.isEmpty() || width() <= 0 || height() <= 0) {
        m_zoom = 1.0;
        m_offset = QPointF(0, 0);
        return;
    }
    const qreal availableWidth = std::max(80.0, width() - 16.0);
    const qreal availableHeight = std::max(80.0, height() - 16.0);
    const qreal zoom = std::min(
        {1.15, availableWidth / m_bounds.width(), availableHeight / m_bounds.height()});
    m_zoom = std::clamp(zoom, 0.12, 2.0);
    const QPointF center = m_bounds.center();
    m_offset = QPointF(width() / 2.0, height() / 2.0) - center * m_zoom;
}

void DependencyGraphView::resetView()
{
    fitToWidget();
    update();
}

void DependencyGraphView::selectNode(const QString& id)
{
    if (m_selected == id)
        return;
    m_selected = id;
    update();
}

int DependencyGraphView::nodeAt(const QPointF& screen) const
{
    for (int i = static_cast<int>(m_graph.nodes.size()) - 1; i >= 0; --i) {
        const QRectF rect = nodeRect(i);
        if (rect.isEmpty())
            continue;
        const QRectF screenRect(toScreen(rect.topLeft()), toScreen(rect.bottomRight()));
        if (screenRect.contains(screen))
            return i;
    }
    return -1;
}

void DependencyGraphView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const Theme& theme = ThemeManager::instance().current();
    const QColor accent = theme.color(QStringLiteral("accent"), QColor(0x35, 0xe0, 0xa0));
    const QColor panel = theme.color(QStringLiteral("panel"), QColor(0x1a, 0x1d, 0x22));
    const QColor border = theme.color(QStringLiteral("border"), QColor(0x3a, 0x40, 0x48));
    const QColor baseText = theme.color(QStringLiteral("baseText"), QColor(0xe6, 0xe9, 0xec));
    const QColor mutedText = theme.color(QStringLiteral("panelText"), QColor(0xa8, 0xb0, 0xba));

    if (m_graph.nodes.isEmpty()) {
        painter.setPen(mutedText);
        painter.drawText(rect(), Qt::AlignCenter,
                         QStringLiteral("Scan a project to build its dependency graph."));
        return;
    }

    QHash<QString, int> indexById;
    for (int i = 0; i < m_graph.nodes.size(); ++i)
        indexById.insert(m_graph.nodes.at(i).id, i);

    // Edges first so nodes sit on top.
    for (const gameforge::GraphEdge& edge : m_graph.edges) {
        const int from = indexById.value(edge.from, -1);
        const int to = indexById.value(edge.to, -1);
        if (from < 0 || to < 0)
            continue;
        const bool fromVisible = matchesFilter(m_graph.nodes.at(from));
        const bool toVisible = matchesFilter(m_graph.nodes.at(to));
        const bool dimmed = !m_filter.isEmpty() && !(fromVisible && toVisible);

        QPointF start = toScreen(nodeRect(from).center());
        QPointF end = toScreen(nodeRect(to).center());
        const QPointF delta = end - start;
        const qreal length = std::hypot(delta.x(), delta.y());
        if (length < 1.0)
            continue;
        const QPointF direction(delta.x() / length, delta.y() / length);
        const qreal halfWidth = (kNodeWidth / 2.0) * m_zoom + 2.0;
        const qreal halfHeight = (kNodeHeight / 2.0) * m_zoom + 2.0;
        const qreal offsetX = std::abs(direction.x()) > 0.001 ? halfWidth / std::abs(direction.x())
                                                              : 1e6;
        const qreal offsetY = std::abs(direction.y()) > 0.001 ? halfHeight / std::abs(direction.y())
                                                              : 1e6;
        const qreal trim = std::min(offsetX, offsetY);
        start += direction * trim;
        end -= direction * trim;

        QPen pen(edge.confirmed ? withAlpha(accent, dimmed ? 50 : 175)
                                : withAlpha(border, dimmed ? 40 : 190));
        pen.setWidthF(edge.confirmed ? 1.6 : 1.2);
        if (!edge.confirmed)
            pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.drawLine(start, end);

        // Arrow head.
        const QPointF normal(-direction.y(), direction.x());
        QPainterPath head;
        head.moveTo(end);
        head.lineTo(end - direction * 8.0 + normal * 4.0);
        head.lineTo(end - direction * 8.0 - normal * 4.0);
        head.closeSubpath();
        painter.setBrush(edge.confirmed ? withAlpha(accent, dimmed ? 50 : 200)
                                        : withAlpha(border, dimmed ? 40 : 200));
        painter.setPen(Qt::NoPen);
        painter.drawPath(head);
    }

    QFont labelFont = font();
    labelFont.setPointSizeF(std::max(7.0, font().pointSizeF() - 0.5));
    QFont kindFont = labelFont;
    kindFont.setPointSizeF(std::max(6.5, labelFont.pointSizeF() - 1.0));
    const QFontMetrics labelMetrics(labelFont);

    for (int i = 0; i < m_graph.nodes.size(); ++i) {
        const gameforge::GraphNode& node = m_graph.nodes.at(i);
        const QRectF worldRect = nodeRect(i);
        if (worldRect.isEmpty())
            continue;
        const QRectF screenRect(toScreen(worldRect.topLeft()), toScreen(worldRect.bottomRight()));
        if (!screenRect.intersects(QRectF(rect()).adjusted(-4, -4, 4, 4)))
            continue;

        const bool visible = matchesFilter(node);
        const bool selected = node.id == m_selected;
        const bool hovered = node.id == m_hovered;

        QColor fill = panel;
        QColor outline = border;
        QColor text = baseText;
        bool dashed = false;
        switch (node.kind) {
        case gameforge::NodeKind::Project:
            fill = withAlpha(accent, 55);
            outline = accent;
            text = baseText;
            break;
        case gameforge::NodeKind::Target:
            fill = withAlpha(accent, 28);
            outline = withAlpha(accent, 170);
            break;
        case gameforge::NodeKind::Dependency:
            fill = withAlpha(panel, 210);
            outline = node.confirmed ? border : withAlpha(accent, 120);
            text = node.confirmed ? baseText : mutedText;
            dashed = !node.confirmed;
            break;
        }
        if (selected) {
            outline = accent;
            fill = withAlpha(accent, 70);
        } else if (hovered) {
            outline = withAlpha(accent, 220);
        }
        if (!visible) {
            fill = withAlpha(fill, 30);
            outline = withAlpha(outline, 45);
            text = withAlpha(text, 70);
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(screenRect, 8, 8);
        QPen outlinePen(outline);
        outlinePen.setWidthF(selected ? 2.2 : 1.2);
        if (dashed)
            outlinePen.setStyle(Qt::DashLine);
        painter.setPen(outlinePen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(screenRect, 8, 8);

        if (m_zoom < 0.35)
            continue;
        painter.setPen(text);
        painter.setFont(labelFont);
        const QRectF textRect = screenRect.adjusted(8, 5, -8, -12);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                         labelMetrics.elidedText(node.label, Qt::ElideRight,
                                                 static_cast<int>(textRect.width())));
        painter.setFont(kindFont);
        painter.setPen(withAlpha(visible ? mutedText : text, 190));
        painter.drawText(screenRect.adjusted(8, 0, -8, -5), Qt::AlignLeft | Qt::AlignBottom,
                         gameforge::nodeKindName(node.kind));
    }
}

void DependencyGraphView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        const int hit = nodeAt(event->position());
        if (hit >= 0) {
            const QString id = m_graph.nodes.at(hit).id;
            m_selected = id;
            update();
            emit nodeSelected(id);
            return;
        }
    }
    m_panning = true;
    m_pressPos = event->position();
    m_pressOffset = m_offset;
    setCursor(Qt::ClosedHandCursor);
}

void DependencyGraphView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_panning) {
        m_offset = m_pressOffset + (event->position() - m_pressPos);
        update();
        return;
    }
    const int hit = nodeAt(event->position());
    const QString id = hit >= 0 ? m_graph.nodes.at(hit).id : QString();
    if (id != m_hovered) {
        m_hovered = id;
        setToolTip(hit >= 0 ? m_graph.nodes.at(hit).detail : QString());
        setCursor(hit >= 0 ? Qt::PointingHandCursor : Qt::OpenHandCursor);
        update();
    }
}

void DependencyGraphView::mouseReleaseEvent(QMouseEvent*)
{
    m_panning = false;
    setCursor(Qt::OpenHandCursor);
}

void DependencyGraphView::mouseDoubleClickEvent(QMouseEvent* event)
{
    const int hit = nodeAt(event->position());
    if (hit >= 0)
        emit nodeActivated(m_graph.nodes.at(hit).id);
}

void DependencyGraphView::wheelEvent(QWheelEvent* event)
{
    if (m_graph.nodes.isEmpty()) {
        event->ignore();
        return;
    }
    const qreal steps = event->angleDelta().y() / 120.0;
    if (qFuzzyIsNull(steps)) {
        event->ignore();
        return;
    }
    const qreal factor = std::pow(1.18, steps);
    const qreal newZoom = std::clamp(m_zoom * factor, 0.12, 3.0);
    if (qFuzzyCompare(newZoom, m_zoom)) {
        event->accept();
        return;
    }
    const QPointF cursor = event->position();
    const QPointF world = toWorld(cursor);
    m_zoom = newZoom;
    m_offset = cursor - world * m_zoom;
    update();
    event->accept();
}

void DependencyGraphView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_bounds.isEmpty())
        return;
    // Keep the content on screen after a panel resize; zoom is preserved unless
    // the user has never zoomed, in which case a fresh fit is more useful.
    const bool untouched = qFuzzyCompare(m_zoom, 1.0) && m_offset.isNull();
    if (untouched)
        fitToWidget();
    update();
}

} // namespace pkg::ui
