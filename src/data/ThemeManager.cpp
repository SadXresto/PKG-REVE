#include "data/ThemeManager.h"

#include "core/AtomicFile.h"
#include "core/Logging.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QPalette>
#include <QStyleFactory>

#include <algorithm>
#include <cmath>

namespace pkg {
namespace {

constexpr auto kLogCategory = "theme";

/// Blends two colours; `amount` 0 keeps `from`, 1 returns `to`.
QColor blend(const QColor& from, const QColor& to, double amount)
{
    const double t = std::clamp(amount, 0.0, 1.0);
    const auto channel = [t](int a, int b) {
        return static_cast<int>(std::lround(static_cast<double>(a) + (b - a) * t));
    };
    return QColor(channel(from.red(), to.red()), channel(from.green(), to.green()),
                  channel(from.blue(), to.blue()));
}

/// Primary-tinted glass: `colour` at `alphaPercent` of opacity.
QString glass(const QColor& colour, int alphaPercent)
{
    return QStringLiteral("rgba(%1, %2, %3, %4%)")
        .arg(colour.red())
        .arg(colour.green())
        .arg(colour.blue())
        .arg(std::clamp(alphaPercent, 0, 100));
}

QString sheetTemplate()
{
    // @token placeholders are replaced with the active theme's colours and the
    // generated glass values (translucent surfaces, hairlines, glow).
    return QStringLiteral(R"(
QWidget {
    background-color: @window;
    color: @windowText;
}
QMainWindow#pkgstudioMainWindow { background-color: @window; }
QMainWindow::separator { background: @edge; width: 1px; height: 1px; }
QMenuBar { background-color: transparent; color: @panelText; border-bottom: 1px solid @edge; padding: 2px; }
QMenuBar::item { padding: 4px 10px; background: transparent; border-radius: 5px; }
QMenuBar::item:selected { background-color: @glassButton; color: @highlightedText; }
QMenu { background-color: @glassPanel; color: @panelText; border: 1px solid @edge; padding: 6px; }
QMenu::item { padding: 5px 24px 5px 20px; border-radius: 5px; }
QMenu::item:selected { background-color: @accentWash; color: @highlightedText; }
QMenu::separator { height: 1px; background: @edge; margin: 4px 6px; }
QToolTip { background-color: @glassPanel; color: @tooltipText; border: 1px solid @edge; padding: 4px; }

QWidget#activityBar { background-color: @glassBar; border-right: 1px solid @edge; }
QToolButton#activityButton {
    background: transparent; color: @activityBarText; border: none;
    border-radius: 9px; padding: 6px; margin: 3px 6px;
}
QToolButton#activityButton:hover { background-color: @glassButton; }
QToolButton#activityButton:checked {
    background-color: @glassButton; border-left: 2px solid @accent; color: @accent;
}

QWidget#sideBar { background-color: @glassPanel; border-right: 1px solid @edge; }
QLabel#sideBarHeader {
    background-color: transparent; color: @panelText; padding: 8px 10px;
    font-weight: 600; letter-spacing: 0.4px; border-bottom: 1px solid @edge;
}
QLabel#sideBarHint { color: @panelText; padding: 8px; }

QTreeView, QListView, QTableView {
    background-color: @glassBase; color: @baseText; border: 1px solid @edge;
    border-radius: 8px;
    selection-background-color: @accentWash; selection-color: @highlightedText;
    alternate-background-color: @glassRow;
}
QTreeView::item, QListView::item { padding: 3px 4px; border-radius: 4px; }
QTreeView::item:hover, QListView::item:hover { background-color: @glassRow; }
QHeaderView::section {
    background-color: transparent; color: @panelText; border: none;
    border-bottom: 1px solid @edge; padding: 5px;
}

QTabWidget::pane {
    border: 1px solid @edge; background-color: @glassBase;
    border-radius: 10px; top: 2px;
}
QTabBar { background-color: transparent; qproperty-drawBase: 0; }
QTabBar::tab {
    background-color: @glassTab; color: @windowText; padding: 7px 14px;
    border: 1px solid transparent; border-bottom: none;
    border-top-left-radius: 8px; border-top-right-radius: 8px; margin-right: 2px;
}
QTabBar::tab:selected {
    background-color: @glassBase; border-color: @edge; border-top: 2px solid @accent;
}
QTabBar::tab:hover { background-color: @glassRow; }
QTabBar::close-button { margin: 3px; }

QWidget#fileEditorHeader {
    background-color: @glassHeader; border-bottom: 1px solid @edge;
}
QLabel#editorPath { color: @panelText; padding-left: 6px; }
QLabel#editorPathMuted { color: @gutter; }

QPlainTextEdit, QTextEdit, QTextBrowser, QLineEdit, QSpinBox, QComboBox, QKeySequenceEdit {
    background-color: @glassInput; color: @inputText; border: 1px solid @edge;
    border-radius: 8px; padding: 4px 8px;
    selection-background-color: @accentWash; selection-color: @highlightedText;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QTextBrowser:focus,
QComboBox:focus, QSpinBox:focus {
    border: 1px solid @focusEdge;
}
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
    background-color: @glassPanel; color: @panelText; border: 1px solid @edge;
    selection-background-color: @accentWash;
}
QPushButton {
    background-color: @glassButton; color: @buttonText; border: 1px solid @edge;
    padding: 6px 14px; border-radius: 8px;
}
QPushButton:hover { background-color: @glassRow; border-color: @focusEdge; }
QPushButton:pressed { background-color: @accentWash; }
QPushButton:default { background-color: @accent; color: @highlightedText; border-color: @focusEdge; }
QPushButton:disabled { color: @gutter; background-color: @glassButton; border-color: @edge; }
QCheckBox, QRadioButton { background: transparent; color: @windowText; spacing: 6px; }
QDockWidget { color: @panelText; }
QDockWidget::title {
    background-color: @glassHeader; color: @panelText;
    border-bottom: 1px solid @edge; padding: 6px 10px;
    border-top-left-radius: 8px; border-top-right-radius: 8px;
}
QGroupBox { border: 1px solid @edge; border-radius: 10px; margin-top: 14px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }

QSplitter::handle { background-color: @edge; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }

QScrollBar:vertical { background: transparent; width: 11px; margin: 2px; }
QScrollBar::handle:vertical { background: @scrollThumb; min-height: 26px; border-radius: 4px; }
QScrollBar::handle:vertical:hover { background: @focusEdge; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 2px; }
QScrollBar::handle:horizontal { background: @scrollThumb; min-width: 26px; border-radius: 4px; }
QScrollBar::handle:horizontal:hover { background: @focusEdge; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QStatusBar#statusBar {
    background-color: @glassStatus; color: @statusBarText; border-top: 1px solid @edge;
}
QStatusBar#statusBar QLabel { color: @statusBarText; padding: 0 6px; }
QStatusBar::item { border: none; }

QProgressBar {
    background-color: @glassInput; border: 1px solid @edge;
    border-radius: 7px; text-align: center;
}
QProgressBar::chunk { background-color: @accent; border-radius: 6px; }
QDialog { background-color: @window; }
QDialogButtonBox QPushButton { min-width: 80px; }
QListWidget#commandList { background-color: @glassBase; border: 1px solid @edge; border-radius: 8px; }
QLabel#sectionLabel { color: @panelText; font-weight: 600; }
QLabel#hintLabel { color: @panelText; }
QLabel#hintLabel:disabled { color: @gutter; }

/* Design-system components: inspector, badges, replace bar. */
QFrame#byteInspector {
    background-color: @glassPanel; border: 1px solid @edge; border-radius: 10px;
}
QLabel#inspectorTitle {
    color: @accent; font-weight: 600; letter-spacing: 0.6px;
}
QLabel#inspectorKey { color: @panelText; }
QLabel#inspectorValue { color: @baseText; }
QToolButton#inspectorClose {
    background: transparent; color: @panelText; border: none; padding: 2px 4px;
}
QToolButton#inspectorClose:hover { color: @accent; }
QToolButton#editToggle {
    background: transparent; color: @accent; border: 1px solid @edge;
    padding: 3px 9px; border-radius: 7px;
}
QToolButton#editToggle:hover { border-color: @focusEdge; background-color: @glassButton; }
QLabel#signatureBadge {
    color: @accent; border: 1px solid @edge; border-radius: 7px; padding: 1px 7px;
}
QWidget#replaceBar {
    background-color: @glassPanel; border-top: 1px solid @edge;
}
QWidget#hexInfoStrip {
    background-color: @glassHeader; border-top: 1px solid @edge;
}
QLabel#hexInfoLabel { color: @panelText; }
QLabel#hexInfoSeparator { color: @gutter; }
QListWidget#settingsCategories {
    background-color: transparent; border: none; padding: 4px;
}
QListWidget#settingsCategories::item {
    padding: 8px 10px; border-radius: 7px; margin: 1px 0;
}
QListWidget#settingsCategories::item:selected {
    background-color: @accentWash; color: @highlightedText;
    border-left: 2px solid @accent;
}
QFrame#settingsInfoCard {
    background-color: @glassPanel; border: 1px solid @edge; border-radius: 9px;
}
QWidget#settingsPage QWidget { background: transparent; }
QWidget#devflowHeader {
    background-color: @glassHeader; border-bottom: 1px solid @edge;
}
QLabel#devflowTitle {
    color: @accent; font-weight: 700; letter-spacing: 1.6px;
}
QLabel#devflowStat {
    color: @accent; font-size: 18px; font-weight: 700;
}
QListWidget#devflowList {
    background-color: @glassBase; border: 1px solid @edge; border-radius: 8px;
}
QListWidget#devflowActivity {
    background-color: transparent; border: none;
}
QKeySequenceEdit { padding: 1px 3px; }
)");
}

}  // namespace

ThemeManager& ThemeManager::instance()
{
    static ThemeManager manager;
    return manager;
}

ThemeManager::ThemeManager()
{
    for (Theme theme : Theme::builtins())
        m_themes.insert(theme.id, theme);
    m_currentId = QStringLiteral("pkgstudio.voidblack");
}

ThemeManager::~ThemeManager() = default;

void ThemeManager::initialize(const QString& userThemeDirectory)
{
    m_userDirectory = userThemeDirectory;
    loadUserThemes();
}

void ThemeManager::loadUserThemes()
{
    if (m_userDirectory.isEmpty())
        return;
    QDir directory(m_userDirectory);
    if (!directory.exists() && !QDir().mkpath(m_userDirectory)) {
        logWarning(QString::fromLatin1(kLogCategory),
                   QStringLiteral("Cannot create theme directory: %1").arg(m_userDirectory));
        return;
    }

    const QStringList files = directory.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString& file : files) {
        auto bytes = atomic::readFile(directory.filePath(file), 256 * 1024);
        if (!bytes) {
            logWarning(QString::fromLatin1(kLogCategory),
                       QStringLiteral("Cannot read theme %1: %2").arg(file, bytes.error().message));
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(bytes.value());
        if (!document.isObject()) {
            logWarning(QString::fromLatin1(kLogCategory),
                       QStringLiteral("Theme %1 is not a JSON object").arg(file));
            continue;
        }
        auto theme = Theme::fromJson(document.object());
        if (!theme) {
            logWarning(QString::fromLatin1(kLogCategory),
                       QStringLiteral("Theme %1 rejected: %2")
                           .arg(file, theme.error().detail.isEmpty() ? theme.error().message
                                                                     : theme.error().detail));
            continue;
        }
        m_themes.insert(theme.value().id, theme.value());
    }
}

QList<Theme> ThemeManager::themes() const
{
    QList<Theme> result = m_themes.values();
    std::sort(result.begin(), result.end(), [](const Theme& a, const Theme& b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return result;
}

QStringList ThemeManager::themeIds() const
{
    QStringList ids = m_themes.keys();
    ids.sort();
    return ids;
}

bool ThemeManager::contains(const QString& id) const
{
    return m_themes.contains(id);
}

const Theme& ThemeManager::current() const
{
    const auto it = m_themes.constFind(m_currentId);
    if (it != m_themes.constEnd())
        return it.value();
    return m_themes.constFind(QStringLiteral("pkgstudio.voidblack")).value();
}

const Theme& ThemeManager::fallback() const
{
    return m_themes.constFind(QStringLiteral("pkgstudio.voidblack")).value();
}

Status ThemeManager::setCurrent(const QString& id)
{
    if (!m_themes.contains(id))
        return makeError(QStringLiteral("Unknown theme"), id);
    if (m_currentId == id)
        return okStatus();
    m_currentId = id;
    logInfo(QString::fromLatin1(kLogCategory), QStringLiteral("Theme set to %1").arg(id));
    emit themeChanged(current());
    return okStatus();
}

Status ThemeManager::saveUserTheme(const Theme& theme)
{
    QString reason;
    if (!theme.isValid(&reason))
        return makeError(QStringLiteral("Theme is invalid"), reason);
    if (m_userDirectory.isEmpty())
        return makeError(QStringLiteral("No user theme directory configured"));

    const QJsonDocument document(theme.toJson());
    const QString path = QDir(m_userDirectory).filePath(theme.id + QStringLiteral(".json"));
    const Status written = atomic::writeFile(path, document.toJson(QJsonDocument::Indented));
    if (!written)
        return written;

    m_themes.insert(theme.id, theme);
    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("Saved user theme %1").arg(theme.id));
    return okStatus();
}

Status ThemeManager::importThemeFile(const QString& path)
{
    auto bytes = atomic::readFile(path, 256 * 1024);
    if (!bytes)
        return bytes.error();
    const QJsonDocument document = QJsonDocument::fromJson(bytes.value());
    if (!document.isObject())
        return makeError(QStringLiteral("Theme file is not a JSON object"), path);
    auto theme = Theme::fromJson(document.object());
    if (!theme)
        return theme.error();
    return saveUserTheme(theme.value());
}

Status ThemeManager::setWallpaper(const QString& id, const QString& imagePath, int opacity)
{
    const auto it = m_themes.find(id);
    if (it == m_themes.constEnd())
        return makeError(QStringLiteral("Unknown theme"), id);

    Theme theme = it.value();
    if (imagePath.isEmpty()) {
        theme.backgroundImage.clear();
    } else {
        const QFileInfo source(imagePath);
        if (!source.isFile())
            return makeError(QStringLiteral("Background image not found"), imagePath);
        QImageReader reader(imagePath);
        if (!reader.canRead())
            return makeError(QStringLiteral("Unsupported image format"), imagePath);
        if (m_userDirectory.isEmpty())
            return makeError(QStringLiteral("No user theme directory configured"));

        const QString imagesDirectory = QDir(m_userDirectory).filePath(QStringLiteral("images"));
        if (!QDir().mkpath(imagesDirectory))
            return makeError(QStringLiteral("Cannot create the theme image folder"),
                             imagesDirectory);
        const QString target =
            QDir(imagesDirectory).filePath(theme.id + QLatin1Char('-') + source.fileName());
        if (QFileInfo(target).absoluteFilePath() != source.absoluteFilePath()) {
            QFile::remove(target);
            if (!QFile::copy(source.absoluteFilePath(), target))
                return makeError(QStringLiteral("Cannot copy the background image"), target);
        }
        theme.backgroundImage = target;
    }
    theme.backgroundOpacity = std::clamp(opacity, 20, 100);

    const Status saved = saveUserTheme(theme);
    if (!saved)
        return saved;
    if (m_currentId == id)
        emit themeChanged(current());
    return okStatus();
}

Status ThemeManager::exportThemeFile(const QString& id, const QString& path)
{
    const auto it = m_themes.constFind(id);
    if (it == m_themes.constEnd())
        return makeError(QStringLiteral("Unknown theme"), id);
    QJsonObject object = it.value().toJson();
    object.insert(QStringLiteral("exportedBy"), QStringLiteral("PKG Studio"));
    const QJsonDocument document(object);
    return atomic::writeFile(path, document.toJson(QJsonDocument::Indented));
}

void ThemeManager::apply(const Theme& theme, QApplication& app)
{
    QPalette palette;
    const QColor window = theme.color(QStringLiteral("window"));
    const QColor windowText = theme.color(QStringLiteral("windowText"));
    const QColor base = theme.color(QStringLiteral("base"));
    const QColor baseText = theme.color(QStringLiteral("baseText"));
    const QColor button = theme.color(QStringLiteral("button"));
    const QColor highlight = theme.color(QStringLiteral("highlight"));
    const QColor highlightedText = theme.color(QStringLiteral("highlightedText"));

    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, windowText);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, theme.color(QStringLiteral("alternateBase")));
    palette.setColor(QPalette::Text, baseText);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, theme.color(QStringLiteral("buttonText")));
    palette.setColor(QPalette::BrightText, theme.color(QStringLiteral("buttonText")));
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, highlightedText);
    palette.setColor(QPalette::ToolTipBase, theme.color(QStringLiteral("tooltipBase")));
    palette.setColor(QPalette::ToolTipText, theme.color(QStringLiteral("tooltipText")));
    palette.setColor(QPalette::Link, theme.color(QStringLiteral("link")));
    palette.setColor(QPalette::PlaceholderText, theme.color(QStringLiteral("gutter")));
    palette.setColor(QPalette::Disabled, QPalette::Text, theme.color(QStringLiteral("gutter")));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                     theme.color(QStringLiteral("gutter")));

    app.setPalette(palette);
    app.setStyleSheet(styleSheet(theme));
}

QString ThemeManager::styleSheet(const Theme& theme)
{
    QString sheet = sheetTemplate();
    const bool wallpaper = !theme.backgroundImage.isEmpty()
        && QFileInfo::exists(theme.backgroundImage);
    const int opacity = std::clamp(theme.backgroundOpacity, 20, 100);
    const QColor window = theme.color(QStringLiteral("window"));
    const QColor accent = theme.color(QStringLiteral("accent"));

    // Every surface is translucent over the window backdrop: the wallpaper's
    // own opacity when one is set, otherwise a fixed glass level that still
    // keeps text contrast comfortable.
    const auto surface = [&](const QString& role, int glassAlpha) {
        const QColor colour = theme.color(role);
        return wallpaper ? glass(colour, opacity) : glass(colour, glassAlpha);
    };

    QHash<QString, QString> tokens;
    for (const QString& role : Theme::requiredRoles())
        tokens.insert(role, theme.color(role).name(QColor::HexRgb));
    tokens.insert(QStringLiteral("glassPanel"), surface(QStringLiteral("panel"), 55));
    tokens.insert(QStringLiteral("glassBase"), surface(QStringLiteral("base"), 90));
    tokens.insert(QStringLiteral("glassBar"), surface(QStringLiteral("activityBar"), 45));
    tokens.insert(QStringLiteral("glassHeader"), surface(QStringLiteral("sidebarHeader"), 58));
    tokens.insert(QStringLiteral("glassInput"), surface(QStringLiteral("inputBackground"), 80));
    tokens.insert(QStringLiteral("glassButton"), surface(QStringLiteral("button"), 78));
    tokens.insert(QStringLiteral("glassTab"), surface(QStringLiteral("tabInactive"), 45));
    tokens.insert(QStringLiteral("glassRow"),
                  glass(theme.color(QStringLiteral("alternateBase")),
                        wallpaper ? std::min(opacity, 60) : 40));
    tokens.insert(QStringLiteral("accentWash"),
                  glass(blend(theme.color(QStringLiteral("highlight")), accent, 0.45), 85));
    tokens.insert(QStringLiteral("edge"),
                  theme.dark ? glass(QColor(255, 255, 255), 12) : glass(QColor(0, 0, 0), 12));
    tokens.insert(QStringLiteral("focusEdge"), glass(accent, 70));
    tokens.insert(QStringLiteral("scrollThumb"), glass(theme.color(QStringLiteral("button")), 90));
    // The status bar keeps its own role: its text colour is chosen to contrast
    // with that colour, not with the activity bar's.
    tokens.insert(QStringLiteral("glassStatus"), surface(QStringLiteral("statusBar"), 92));

    // Longest token first: a naive replacement would turn "@windowText" into
    // "<window colour>Text" because "@window" is a prefix of it.
    QStringList keys = tokens.keys();
    std::sort(keys.begin(), keys.end(), [](const QString& left, const QString& right) {
        return left.size() > right.size();
    });
    for (const QString& key : keys)
        sheet.replace(QLatin1Char('@') + key, tokens.value(key));

    // Structural containers of the main window must not paint an opaque layer
    // over the backdrop. Scoped to the main window: dialogs keep their normal
    // opaque splitters and stacks.
    sheet.prepend(QStringLiteral(
        "QMainWindow#pkgstudioMainWindow QWidget#centralHost,\n"
        "QMainWindow#pkgstudioMainWindow QDockWidget,\n"
        "QMainWindow#pkgstudioMainWindow QSplitter,\n"
        "QMainWindow#pkgstudioMainWindow QStackedWidget,\n"
        "QWidget#aiChatPanel { background: transparent; }\n"));

    if (wallpaper) {
        QString path = QDir::fromNativeSeparators(theme.backgroundImage);
        path.replace(QLatin1Char('"'), QStringLiteral("%22"));
        sheet.prepend(QStringLiteral("QMainWindow#pkgstudioMainWindow {\n"
                                     "    background-image: url(\"%1\");\n"
                                     "    background-position: center;\n"
                                     "    background-repeat: no-repeat;\n"
                                     "    background-color: %2;\n"
                                     "}\n")
                          .arg(path, window.name(QColor::HexRgb)));
    }
    // Without a wallpaper the backdrop (the accent bloom behind the translucent
    // surfaces) is painted by MainWindow::paintEvent; a style-sheet background
    // on QMainWindow alone does not paint here.
    return sheet;
}
}  // namespace pkg
