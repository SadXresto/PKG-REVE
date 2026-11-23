// Vector icons drawn with QPainter so the application needs no icon theme or
// image assets at runtime.
#pragma once

#include <QColor>
#include <QIcon>

namespace pkg::ui {

enum class IconKind {
    Explorer,
    Search,
    Recent,
    Settings,
    Palette,
    Diagnostics,
    GameForge,
    DevFlow,
    Navigator,
    Tests,
    Plugins,
    Close,
};

[[nodiscard]] QIcon makeIcon(IconKind kind, const QColor& color, int size = 18);

}  // namespace pkg::ui
