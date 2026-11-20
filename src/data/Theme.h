// Theme model: named colours used by the palette and style sheet.
#pragma once

#include "core/Result.h"

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace pkg {

struct Theme {
    QString id;
    QString name;
    QString author;
    bool dark = true;
    int schemaVersion = 1;
    QHash<QString, QColor> colors;

    /// Optional wallpaper image (absolute path). Empty means a flat theme. The
    /// image is drawn behind the window while panel colours become translucent.
    QString backgroundImage;
    /// Opacity of background surfaces over the image, 20-100. 100 hides the
    /// image completely and keeps the theme flat.
    int backgroundOpacity = 82;

    /// Colour roles every theme must define.
    [[nodiscard]] static QStringList requiredRoles();
    /// Human readable role labels for the theme editor.
    [[nodiscard]] static QStringList editableRoles();

    [[nodiscard]] bool isValid(QString* reason = nullptr) const;
    [[nodiscard]] QColor color(const QString& role, const QColor& fallback = {}) const;
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static Result<Theme> fromJson(const QJsonObject& object);
    /// Built-in themes; always available even without a data directory.
    [[nodiscard]] static QList<Theme> builtins();
};

}  // namespace pkg
