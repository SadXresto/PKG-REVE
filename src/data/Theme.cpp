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

    Theme voidBlack;
    voidBlack.id = QStringLiteral("pkgstudio.voidblack");
    voidBlack.name = QStringLiteral("Void Black");
    voidBlack.author = QStringLiteral("PKG Studio");
    voidBlack.dark = true;
    voidBlack.colors = makeColors({
        {"window", "#0a0a0a"},
        {"windowText", "#e8e8e8"},
        {"base", "#0a0a0a"},
        {"baseText", "#e8e8e8"},
        {"alternateBase", "#101010"},
        {"button", "#151515"},
        {"buttonText", "#e8e8e8"},
        {"highlight", "#123b24"},
        {"highlightedText", "#f2fff5"},
        {"tooltipBase", "#101010"},
        {"tooltipText", "#e8e8e8"},
        {"link", "#00ff41"},
        {"border", "#1c3b28"},
        {"panel", "#101010"},
        {"panelText", "#c9d1c9"},
        {"accent", "#00ff41"},
        {"statusBar", "#101010"},
        {"statusBarText", "#00ff41"},
        {"tabActive", "#101010"},
        {"tabInactive", "#0a0a0a"},
        {"gutter", "#151515"},
        {"hexOffset", "#4ad66d"},
        {"hexBytes", "#e8e8e8"},
        {"hexAscii", "#ff5c7a"},
        {"hexSelection", "#123b24"},
        {"hexSearch", "#4a1420"},
        {"activityBar", "#0a0a0a"},
        {"activityBarText", "#9fb3a4"},
        {"inputBackground", "#151515"},
        {"inputText", "#e8e8e8"},
        {"sidebarHeader", "#101010"},
    });
    themes << voidBlack;

    Theme neonHex;
    neonHex.id = QStringLiteral("pkgstudio.neonhex");
    neonHex.name = QStringLiteral("Neon Hex");
    neonHex.author = QStringLiteral("PKG Studio");
    neonHex.dark = true;
    neonHex.colors = makeColors({
        {"window", "#05060b"},
        {"windowText", "#e6f1ff"},
        {"base", "#05060b"},
        {"baseText", "#e6f1ff"},
        {"alternateBase", "#0b1120"},
        {"button", "#111a2e"},
        {"buttonText", "#e6f1ff"},
        {"highlight", "#12305a"},
        {"highlightedText", "#ffffff"},
        {"tooltipBase", "#0b1120"},
        {"tooltipText", "#e6f1ff"},
        {"link", "#00e5ff"},
        {"border", "#163a5a"},
        {"panel", "#0b1120"},
        {"panelText", "#b8cfe6"},
        {"accent", "#00e5ff"},
        {"statusBar", "#0b1120"},
        {"statusBarText", "#00e5ff"},
        {"tabActive", "#0b1120"},
        {"tabInactive", "#05060b"},
        {"gutter", "#0b1120"},
        {"hexOffset", "#7aa2ff"},
        {"hexBytes", "#e6f1ff"},
        {"hexAscii", "#ffb000"},
        {"hexSelection", "#12305a"},
        {"hexSearch", "#4a2a00"},
        {"activityBar", "#05060b"},
        {"activityBarText", "#8fb3c9"},
        {"inputBackground", "#111a2e"},
        {"inputText", "#e6f1ff"},
        {"sidebarHeader", "#0b1120"},
    });
    themes << neonHex;

    return themes;
}

}  // namespace pkg
