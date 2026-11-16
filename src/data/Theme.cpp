#include "data/Theme.h"

#include <QJsonValue>

#include <algorithm>

namespace pkg {
namespace {

QHash<QString, QColor> makeColors(std::initializer_list<std::pair<const char*, const char*>> list)
{
    QHash<QString, QColor> colors;
    for (const auto& item : list) {
        const QColor color(QLatin1String(item.second));
        colors.insert(QString::fromLatin1(item.first), color);
    }
    return colors;
}

}  // namespace

QStringList Theme::requiredRoles()
{
    static const QStringList roles = {
        QStringLiteral("window"),
        QStringLiteral("windowText"),
        QStringLiteral("base"),
        QStringLiteral("baseText"),
        QStringLiteral("alternateBase"),
        QStringLiteral("button"),
        QStringLiteral("buttonText"),
        QStringLiteral("highlight"),
        QStringLiteral("highlightedText"),
        QStringLiteral("tooltipBase"),
        QStringLiteral("tooltipText"),
        QStringLiteral("link"),
        QStringLiteral("border"),
        QStringLiteral("panel"),
        QStringLiteral("panelText"),
        QStringLiteral("accent"),
        QStringLiteral("statusBar"),
        QStringLiteral("statusBarText"),
        QStringLiteral("tabActive"),
        QStringLiteral("tabInactive"),
        QStringLiteral("gutter"),
        QStringLiteral("hexOffset"),
        QStringLiteral("hexBytes"),
        QStringLiteral("hexAscii"),
        QStringLiteral("hexSelection"),
        QStringLiteral("hexSearch"),
        QStringLiteral("activityBar"),
        QStringLiteral("activityBarText"),
        QStringLiteral("inputBackground"),
        QStringLiteral("inputText"),
        QStringLiteral("sidebarHeader"),
    };
    return roles;
}

QStringList Theme::editableRoles()
{
    return requiredRoles();
}

bool Theme::isValid(QString* reason) const
{
    if (id.trimmed().isEmpty()) {
        if (reason != nullptr)
            *reason = QStringLiteral("Theme id is empty");
        return false;
    }
    if (name.trimmed().isEmpty()) {
        if (reason != nullptr)
            *reason = QStringLiteral("Theme name is empty");
        return false;
    }
    for (const QString& role : requiredRoles()) {
        const QColor value = colors.value(role);
        if (!value.isValid()) {
            if (reason != nullptr)
                *reason = QStringLiteral("Missing or invalid colour role '%1'").arg(role);
            return false;
        }
    }
    return true;
}

QColor Theme::color(const QString& role, const QColor& fallback) const
{
    const QColor value = colors.value(role);
    return value.isValid() ? value : fallback;
}

QJsonObject Theme::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), schemaVersion);
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("author"), author);
    object.insert(QStringLiteral("dark"), dark);

    QJsonObject colorObject;
    QStringList keys = colors.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString& key : keys)
        colorObject.insert(key, colors.value(key).name(QColor::HexRgb));
    object.insert(QStringLiteral("colors"), colorObject);
    return object;
}

Result<Theme> Theme::fromJson(const QJsonObject& object)
{
    if (!object.contains(QStringLiteral("colors")))
        return makeError(QStringLiteral("Theme has no colors object"));

    Theme theme;
    theme.schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt(1);
    theme.id = object.value(QStringLiteral("id")).toString().trimmed();
    theme.name = object.value(QStringLiteral("name")).toString().trimmed();
    theme.author = object.value(QStringLiteral("author")).toString();
    theme.dark = object.value(QStringLiteral("dark")).toBool(true);

    const QJsonObject colorObject = object.value(QStringLiteral("colors")).toObject();
    for (auto it = colorObject.begin(); it != colorObject.end(); ++it) {
        const QColor color(it.value().toString());
        if (!color.isValid())
            return makeError(QStringLiteral("Invalid colour value for role '%1'").arg(it.key()),
                             it.value().toString());
        theme.colors.insert(it.key(), color);
    }

    QString reason;
    if (!theme.isValid(&reason))
        return makeError(QStringLiteral("Theme is incomplete"), reason);
    return theme;
}

QList<Theme> Theme::builtins()
{
    QList<Theme> themes;

    Theme dark;
    dark.id = QStringLiteral("pkgstudio.dark");
    dark.name = QStringLiteral("PKG Studio Dark");
    dark.author = QStringLiteral("PKG Studio");
    dark.dark = true;
    dark.colors = makeColors({
        {"window", "#1e1e1e"},
        {"windowText", "#d4d4d4"},
        {"base", "#1e1e1e"},
        {"baseText", "#d4d4d4"},
        {"alternateBase", "#252526"},
        {"button", "#333333"},
        {"buttonText", "#e7e7e7"},
        {"highlight", "#264f78"},
        {"highlightedText", "#ffffff"},
        {"tooltipBase", "#252526"},
        {"tooltipText", "#d4d4d4"},
        {"link", "#4daafc"},
        {"border", "#3c3c3c"},
        {"panel", "#252526"},
        {"panelText", "#cccccc"},
        {"accent", "#0e639c"},
        {"statusBar", "#007acc"},
        {"statusBarText", "#ffffff"},
        {"tabActive", "#1e1e1e"},
        {"tabInactive", "#2d2d2d"},
        {"gutter", "#1a1a1a"},
        {"hexOffset", "#569cd6"},
        {"hexBytes", "#d4d4d4"},
        {"hexAscii", "#ce9178"},
        {"hexSelection", "#264f78"},
        {"hexSearch", "#613214"},
        {"activityBar", "#333333"},
        {"activityBarText", "#c8c8c8"},
        {"inputBackground", "#3c3c3c"},
        {"inputText", "#cccccc"},
        {"sidebarHeader", "#252526"},
    });
    themes << dark;

    Theme light;
    light.id = QStringLiteral("pkgstudio.light");
    light.name = QStringLiteral("PKG Studio Light");
    light.author = QStringLiteral("PKG Studio");
    light.dark = false;
    light.colors = makeColors({
        {"window", "#f3f3f3"},
        {"windowText", "#1f1f1f"},
        {"base", "#ffffff"},
        {"baseText", "#1f1f1f"},
        {"alternateBase", "#f7f7f7"},
        {"button", "#e5e5e5"},
        {"buttonText", "#1f1f1f"},
        {"highlight", "#cde3f7"},
        {"highlightedText", "#10233a"},
        {"tooltipBase", "#ffffe1"},
        {"tooltipText", "#1f1f1f"},
        {"link", "#005fb8"},
        {"border", "#d4d4d4"},
        {"panel", "#ffffff"},
        {"panelText", "#333333"},
        {"accent", "#0066b8"},
        {"statusBar", "#0066b8"},
        {"statusBarText", "#ffffff"},
        {"tabActive", "#ffffff"},
        {"tabInactive", "#ececec"},
        {"gutter", "#f0f0f0"},
        {"hexOffset", "#0b5394"},
        {"hexBytes", "#1f1f1f"},
        {"hexAscii", "#a31515"},
        {"hexSelection", "#cde3f7"},
        {"hexSearch", "#ffe9a8"},
        {"activityBar", "#e8e8e8"},
        {"activityBarText", "#3a3a3a"},
        {"inputBackground", "#ffffff"},
        {"inputText", "#1f1f1f"},
        {"sidebarHeader", "#ececec"},
    });
    themes << light;

    Theme darcula;
    darcula.id = QStringLiteral("pkgstudio.darcula");
    darcula.name = QStringLiteral("Darcula");
    darcula.author = QStringLiteral("PKG Studio");
    darcula.dark = true;
    darcula.colors = makeColors({
        {"window", "#2b2b2b"},
        {"windowText", "#a9b7c6"},
        {"base", "#2b2b2b"},
        {"baseText", "#a9b7c6"},
        {"alternateBase", "#323232"},
        {"button", "#4c5052"},
        {"buttonText", "#e8e8e8"},
        {"highlight", "#214283"},
        {"highlightedText", "#ffffff"},
        {"tooltipBase", "#3c3f41"},
        {"tooltipText", "#a9b7c6"},
        {"link", "#6897bb"},
        {"border", "#191919"},
        {"panel", "#3c3f41"},
        {"panelText", "#a9b7c6"},
        {"accent", "#4a88c7"},
        {"statusBar", "#3c3f41"},
        {"statusBarText", "#a9b7c6"},
        {"tabActive", "#2b2b2b"},
        {"tabInactive", "#3c3f41"},
        {"gutter", "#313335"},
        {"hexOffset", "#9876aa"},
        {"hexBytes", "#a9b7c6"},
        {"hexAscii", "#6a8759"},
        {"hexSelection", "#214283"},
        {"hexSearch", "#32593d"},
        {"activityBar", "#3c3f41"},
        {"activityBarText", "#a9b7c6"},
        {"inputBackground", "#45494a"},
        {"inputText", "#a9b7c6"},
        {"sidebarHeader", "#3c3f41"},
    });
    themes << darcula;

    Theme cyberpunk;
    cyberpunk.id = QStringLiteral("pkgstudio.cyberpunk");
    cyberpunk.name = QStringLiteral("Cyberpunk");
    cyberpunk.author = QStringLiteral("PKG Studio");
    cyberpunk.dark = true;
    cyberpunk.colors = makeColors({
        {"window", "#0a0014"},
        {"windowText", "#d7f9ff"},
        {"base", "#0d0019"},
        {"baseText", "#d7f9ff"},
        {"alternateBase", "#14002b"},
        {"button", "#1e0038"},
        {"buttonText", "#d7f9ff"},
        {"highlight", "#3d005e"},
        {"highlightedText", "#ffffff"},
        {"tooltipBase", "#14002b"},
        {"tooltipText", "#d7f9ff"},
        {"link", "#00f0ff"},
        {"border", "#33104d"},
        {"panel", "#120022"},
        {"panelText", "#c9d7e8"},
        {"accent", "#ff2bd6"},
        {"statusBar", "#24003d"},
        {"statusBarText", "#00f0ff"},
        {"tabActive", "#1a0030"},
        {"tabInactive", "#0f001c"},
        {"gutter", "#0d0019"},
        {"hexOffset", "#00f0ff"},
        {"hexBytes", "#d7f9ff"},
        {"hexAscii", "#ff8ad8"},
        {"hexSelection", "#3d005e"},
        {"hexSearch", "#5a2a00"},
        {"activityBar", "#120022"},
        {"activityBarText", "#c9a7ff"},
        {"inputBackground", "#1e0038"},
        {"inputText", "#d7f9ff"},
        {"sidebarHeader", "#1a0030"},
    });
    themes << cyberpunk;

    return themes;
}

}  // namespace pkg
