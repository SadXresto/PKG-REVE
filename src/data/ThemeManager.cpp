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

namespace pkg {
namespace {

constexpr auto kLogCategory = "theme";

QString sheetTemplate()
{
    // @role tokens are replaced with the active theme's colours.
    return QStringLiteral(R"(
QWidget {
    background-color: @window;
    color: @windowText;
}
QMainWindow::separator { background: @border; width: 1px; height: 1px; }
QMenuBar { background-color: @panel; color: @panelText; border-bottom: 1px solid @border; }
QMenuBar::item { padding: 4px 10px; background: transparent; }
QMenuBar::item:selected { background-color: @highlight; color: @highlightedText; }
QMenu { background-color: @panel; color: @panelText; border: 1px solid @border; padding: 4px; }
QMenu::item { padding: 5px 24px 5px 20px; }
QMenu::item:selected { background-color: @highlight; color: @highlightedText; }
QMenu::separator { height: 1px; background: @border; margin: 4px 6px; }
QToolTip { background-color: @tooltipBase; color: @tooltipText; border: 1px solid @border; padding: 3px; }

QWidget#activityBar { background-color: @activityBar; border-right: 1px solid @border; }
QToolButton#activityButton {
    background: transparent; color: @activityBarText; border: none;
    border-left: 2px solid transparent; padding: 6px; margin: 2px 0;
}
QToolButton#activityButton:hover { background-color: @panel; }
QToolButton#activityButton:checked { border-left: 2px solid @accent; background-color: @panel; }

QWidget#sideBar { background-color: @panel; }
QLabel#sideBarHeader {
    background-color: @sidebarHeader; color: @panelText; padding: 6px 8px;
    font-weight: 600; letter-spacing: 0.4px;
}
QLabel#sideBarHint { color: @panelText; padding: 8px; }

QTreeView, QListView, QTableView {
    background-color: @base; color: @baseText; border: none;
    selection-background-color: @highlight; selection-color: @highlightedText;
    alternate-background-color: @alternateBase;
}
QTreeView::item, QListView::item { padding: 2px; }
QTreeView::item:hover, QListView::item:hover { background-color: @alternateBase; }
QHeaderView::section {
    background-color: @panel; color: @panelText; border: none;
    border-right: 1px solid @border; border-bottom: 1px solid @border; padding: 4px;
}

QTabWidget::pane { border: none; background-color: @base; }
QTabBar { background-color: @window; qproperty-drawBase: 0; }
QTabBar::tab {
    background-color: @tabInactive; color: @windowText; padding: 7px 14px;
    border: none; border-right: 1px solid @border;
}
QTabBar::tab:selected { background-color: @tabActive; border-top: 1px solid @accent; }
QTabBar::tab:hover { background-color: @alternateBase; }
QTabBar::close-button { margin: 3px; }

QWidget#fileEditorHeader {
    background-color: @panel; border-bottom: 1px solid @border;
}
QLabel#editorPath { color: @panelText; padding-left: 6px; }
QLabel#editorPathMuted { color: @gutter; }

QPlainTextEdit, QTextEdit, QTextBrowser, QLineEdit, QSpinBox, QComboBox, QKeySequenceEdit {
    background-color: @inputBackground; color: @inputText; border: 1px solid @border;
    selection-background-color: @highlight; selection-color: @highlightedText;
    padding: 2px 4px;
}
QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus {
    border: 1px solid @accent;
}
QComboBox QAbstractItemView {
    background-color: @panel; color: @panelText; selection-background-color: @highlight;
}
QPushButton {
    background-color: @button; color: @buttonText; border: 1px solid @border;
    padding: 5px 12px; border-radius: 2px;
}
QPushButton:hover { border-color: @accent; }
QPushButton:default { background-color: @accent; color: @highlightedText; }
QPushButton:disabled { color: @gutter; }
QCheckBox, QRadioButton { background: transparent; color: @windowText; spacing: 6px; }
QGroupBox { border: 1px solid @border; margin-top: 12px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }

QSplitter::handle { background-color: @border; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }

QScrollBar:vertical { background: @window; width: 12px; margin: 0; }
QScrollBar::handle:vertical { background: @button; min-height: 24px; border-radius: 3px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: @accent; }
QScrollBar:horizontal { background: @window; height: 12px; margin: 0; }
QScrollBar::handle:horizontal { background: @button; min-width: 24px; border-radius: 3px; margin: 2px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QStatusBar#statusBar { background-color: @statusBar; color: @statusBarText; }
QStatusBar#statusBar QLabel { color: @statusBarText; padding: 0 6px; }
QStatusBar::item { border: none; }

QProgressBar { background-color: @panel; border: 1px solid @border; text-align: center; }
QProgressBar::chunk { background-color: @accent; }
QDialog { background-color: @window; }
QDialogButtonBox QPushButton { min-width: 80px; }
QListWidget#commandList { background-color: @base; border: 1px solid @border; }
QLabel#sectionLabel { color: @panelText; font-weight: 600; }
QLabel#hintLabel { color: @panelText; }
QLabel#hintLabel:disabled { color: @gutter; }

/* Design-system components: inspector, badges, replace bar. */
QFrame#byteInspector {
    background-color: @panel; border: 1px solid @border;
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
    background: transparent; color: @accent; border: 1px solid @border;
    padding: 2px 8px; border-radius: 2px;
}
QToolButton#editToggle:hover { border-color: @accent; }
QLabel#signatureBadge {
    color: @accent; border: 1px solid @border; padding: 1px 6px;
}
QWidget#replaceBar { background-color: @panel; border-top: 1px solid @border; }
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

    // Background roles become translucent over a wallpaper so the image is
    // visible through the panels while text colours stay unchanged.
    static const QStringList translucentRoles = {
        QStringLiteral("window"),       QStringLiteral("panel"),
        QStringLiteral("base"),         QStringLiteral("alternateBase"),
        QStringLiteral("button"),       QStringLiteral("activityBar"),
        QStringLiteral("statusBar"),    QStringLiteral("tabActive"),
        QStringLiteral("tabInactive"),  QStringLiteral("gutter"),
        QStringLiteral("inputBackground"),
        QStringLiteral("sidebarHeader"),
    };
    const int opacity = std::clamp(theme.backgroundOpacity, 20, 100);

    // Longest role first: a naive replacement would turn "@windowText" into
    // "<window colour>Text" because "@window" is a prefix of it.
    QStringList roles = Theme::requiredRoles();
    std::sort(roles.begin(), roles.end(), [](const QString& left, const QString& right) {
        return left.size() > right.size();
    });
    for (const QString& role : roles) {
        const QColor color = theme.color(role);
        QString value = color.name(QColor::HexRgb);
        if (wallpaper && translucentRoles.contains(role)) {
            value = QStringLiteral("rgba(%1, %2, %3, %4%)")
                        .arg(color.red())
                        .arg(color.green())
                        .arg(color.blue())
                        .arg(opacity);
        }
        sheet.replace(QLatin1Char('@') + role, value);
    }

    if (wallpaper) {
        // Structural containers must not paint an opaque layer over the image
        // the main window draws underneath them.
        sheet.prepend(QStringLiteral(
            "QWidget#centralHost, QWidget#centralHost > QWidget, QSplitter, "
            "QStackedWidget { background: transparent; }\n"));
        QString path = QDir::fromNativeSeparators(theme.backgroundImage);
        path.replace(QLatin1Char('"'), QStringLiteral("%22"));
        sheet.prepend(QStringLiteral("QMainWindow#pkgstudioMainWindow {\n"
                                     "    background-image: url(\"%1\");\n"
                                     "    background-position: center;\n"
                                     "    background-repeat: no-repeat;\n"
                                     "    background-color: %2;\n"
                                     "}\n")
                          .arg(path, theme.color(QStringLiteral("window")).name(QColor::HexRgb)));
    }
    return sheet;
}

}  // namespace pkg
