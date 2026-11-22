#include "ui/Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <cmath>

namespace pkg::ui {

namespace {
constexpr qreal kPi = 3.14159265358979323846;
}

QIcon makeIcon(IconKind kind, const QColor& color, int size)
{
    const qreal scale = 2.0;  // supersample for crisp HiDPI rendering
    QPixmap pixmap(static_cast<int>(size * scale), static_cast<int>(size * scale));
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(scale, scale);

    QPen pen(color, 1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const qreal s = size;
    switch (kind) {
    case IconKind::Explorer: {
        painter.drawRoundedRect(QRectF(s * 0.12, s * 0.22, s * 0.32, s * 0.18), 1.5, 1.5);
        painter.drawRoundedRect(QRectF(s * 0.12, s * 0.32, s * 0.76, s * 0.46), 2.0, 2.0);
        break;
    }
    case IconKind::Search: {
        painter.drawEllipse(QPointF(s * 0.44, s * 0.42), s * 0.26, s * 0.26);
        painter.drawLine(QPointF(s * 0.63, s * 0.61), QPointF(s * 0.85, s * 0.83));
        break;
    }
    case IconKind::Recent: {
        painter.drawEllipse(QPointF(s * 0.5, s * 0.5), s * 0.34, s * 0.34);
        painter.drawLine(QPointF(s * 0.5, s * 0.5), QPointF(s * 0.5, s * 0.3));
        painter.drawLine(QPointF(s * 0.5, s * 0.5), QPointF(s * 0.66, s * 0.58));
        break;
    }
    case IconKind::Settings: {
        painter.drawEllipse(QPointF(s * 0.5, s * 0.5), s * 0.16, s * 0.16);
        painter.drawEllipse(QPointF(s * 0.5, s * 0.5), s * 0.31, s * 0.31);
        for (int i = 0; i < 6; ++i) {
            const qreal angle = static_cast<qreal>(i) * (kPi / 3.0);
            const QPointF from(s * 0.5 + std::cos(angle) * s * 0.31,
                               s * 0.5 + std::sin(angle) * s * 0.31);
            const QPointF to(s * 0.5 + std::cos(angle) * s * 0.42,
                             s * 0.5 + std::sin(angle) * s * 0.42);
            painter.drawLine(from, to);
        }
        break;
    }
    case IconKind::Palette: {
        painter.drawEllipse(QPointF(s * 0.5, s * 0.5), s * 0.34, s * 0.34);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(s * 0.38, s * 0.4), s * 0.07, s * 0.07);
        painter.drawEllipse(QPointF(s * 0.6, s * 0.38), s * 0.07, s * 0.07);
        painter.drawEllipse(QPointF(s * 0.63, s * 0.6), s * 0.07, s * 0.07);
        break;
    }
    case IconKind::Diagnostics: {
        painter.drawEllipse(QPointF(s * 0.5, s * 0.5), s * 0.34, s * 0.34);
        painter.drawLine(QPointF(s * 0.5, s * 0.29), QPointF(s * 0.5, s * 0.55));
        painter.drawPoint(QPointF(s * 0.5, s * 0.67));
        break;
    }
    case IconKind::GameForge: {
        // Gamepad outline: body, d-pad cross on the left, two buttons right.
        painter.drawRoundedRect(QRectF(s * 0.1, s * 0.32, s * 0.8, s * 0.38), s * 0.16, s * 0.16);
        painter.drawLine(QPointF(s * 0.3, s * 0.42), QPointF(s * 0.3, s * 0.58));
        painter.drawLine(QPointF(s * 0.22, s * 0.5), QPointF(s * 0.38, s * 0.5));
        painter.setBrush(color);
        painter.drawEllipse(QPointF(s * 0.64, s * 0.45), s * 0.045, s * 0.045);
        painter.drawEllipse(QPointF(s * 0.72, s * 0.55), s * 0.045, s * 0.045);
        break;
    }
    case IconKind::DevFlow: {
        // Workflow: three connected nodes with an arrow-like flow.
        painter.drawRoundedRect(QRectF(s * 0.14, s * 0.16, s * 0.3, s * 0.2), s * 0.05, s * 0.05);
        painter.drawRoundedRect(QRectF(s * 0.56, s * 0.4, s * 0.3, s * 0.2), s * 0.05, s * 0.05);
        painter.drawRoundedRect(QRectF(s * 0.14, s * 0.64, s * 0.3, s * 0.2), s * 0.05, s * 0.05);
        painter.drawLine(QPointF(s * 0.44, s * 0.26), QPointF(s * 0.56, s * 0.5));
        painter.drawLine(QPointF(s * 0.29, s * 0.36), QPointF(s * 0.29, s * 0.64));
        painter.drawLine(QPointF(s * 0.44, s * 0.74), QPointF(s * 0.56, s * 0.5));
        break;
    }
    case IconKind::Close: {
        painter.drawLine(QPointF(s * 0.3, s * 0.3), QPointF(s * 0.7, s * 0.7));
        painter.drawLine(QPointF(s * 0.7, s * 0.3), QPointF(s * 0.3, s * 0.7));
        break;
    }
    }
    painter.end();
    pixmap.setDevicePixelRatio(scale);
    return QIcon(pixmap);
}

}  // namespace pkg::ui
