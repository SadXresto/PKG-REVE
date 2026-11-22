// Layered dependency-graph canvas for the GameForge workspace.
//
// Facts only: positions are layout, not data. Confirmed relationships are drawn
// solid, inferred ones dashed, and the caller decides what a click means.
#pragma once

#include "core/gameforge/DependencyGraph.h"

#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

namespace pkg::ui {

class DependencyGraphView : public QWidget {
    Q_OBJECT

public:
    explicit DependencyGraphView(QWidget* parent = nullptr);

    void setGraph(const gameforge::DependencyGraph& graph);
    void setFilter(const QString& text);
    void resetView();
    void selectNode(const QString& id);

    [[nodiscard]] QString selectedNodeId() const { return m_selected; }
    [[nodiscard]] bool hasGraph() const { return !m_graph.nodes.isEmpty(); }
    [[nodiscard]] int visibleNodeCount() const;
    [[nodiscard]] qreal zoom() const { return m_zoom; }

signals:
    void nodeSelected(const QString& id);
    void nodeActivated(const QString& id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void relayout();
    void fitToWidget();
    [[nodiscard]] QPointF toScreen(const QPointF& world) const;
    [[nodiscard]] QPointF toWorld(const QPointF& screen) const;
    [[nodiscard]] int nodeAt(const QPointF& screen) const;
    [[nodiscard]] QRectF nodeRect(int index) const;
    [[nodiscard]] bool matchesFilter(const gameforge::GraphNode& node) const;

    gameforge::DependencyGraph m_graph;
    QVector<QPointF> m_centers;
    QString m_filter;
    QString m_selected;
    QString m_hovered;
    QRectF m_bounds;
    qreal m_zoom = 1.0;
    QPointF m_offset;
    QPointF m_pressPos;
    QPointF m_pressOffset;
    bool m_panning = false;
};

} // namespace pkg::ui
