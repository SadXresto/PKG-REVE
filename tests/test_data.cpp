#include "core/AtomicFile.h"
#include "core/Logging.h"
#include "data/Database.h"
#include "data/Settings.h"
#include "data/Theme.h"
#include "data/ThemeManager.h"

#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cmath>

using namespace pkg;

namespace {

double relativeLuminance(const QColor& color)
{
    const auto channel = [](double value) {
        value /= 255.0;
        return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) +
           0.0722 * channel(color.blue());
}

double contrastRatio(const QColor& a, const QColor& b)
{
    const double first = relativeLuminance(a);
    const double second = relativeLuminance(b);
    const double lighter = std::max(first, second);
    const double darker = std::min(first, second);
    return (lighter + 0.05) / (darker + 0.05);
}

}  // namespace

class DatabaseTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(dir.isValid());
        const Status opened =
            database.open(dir.filePath(QStringLiteral("pkgstudio.db")));
        ASSERT_TRUE(opened) << opened.error().message.toStdString();
    }

    void TearDown() override
    {
        Settings::instance().attach(nullptr);
        database.close();
    }

    QTemporaryDir dir;
    Database database;
};

TEST_F(DatabaseTest, CreatesCurrentSchema)
{
    EXPECT_TRUE(database.isOpen());
    EXPECT_EQ(database.schemaVersion(), 1);
    EXPECT_GE(database.analysisCacheCount(), 0);
}

TEST_F(DatabaseTest, SettingsRoundTrip)
{
    EXPECT_EQ(database.setting(QStringLiteral("missing"), QStringLiteral("fallback")),
              QStringLiteral("fallback"));
    database.setSetting(QStringLiteral("ui.theme"), QStringLiteral("custom"));
    EXPECT_EQ(database.setting(QStringLiteral("ui.theme")), QStringLiteral("custom"));
    database.removeSetting(QStringLiteral("ui.theme"));
    EXPECT_TRUE(database.setting(QStringLiteral("ui.theme")).isEmpty());

    database.setSetting(QStringLiteral("shortcut.file.openFile"), QStringLiteral("Ctrl+O"));
    database.setSetting(QStringLiteral("shortcut.file.exit"), QStringLiteral("Ctrl+Q"));
    database.setSetting(QStringLiteral("other"), QStringLiteral("keep"));
    database.removeSettingsWithPrefix(QStringLiteral("shortcut."));
    EXPECT_TRUE(database.setting(QStringLiteral("shortcut.file.exit")).isEmpty());
    EXPECT_EQ(database.setting(QStringLiteral("other")), QStringLiteral("keep"));
}

TEST_F(DatabaseTest, RecentEntriesAreOrderedAndDeduplicated)
{
    database.addRecent(QStringLiteral("/tmp/a"), QStringLiteral("file"));
    database.addRecent(QStringLiteral("/tmp/b"), QStringLiteral("project"));
    database.addRecent(QStringLiteral("/tmp/a"), QStringLiteral("file"));

    const QList<RecentEntry> entries = database.recent(10);
    ASSERT_EQ(entries.size(), 2);
    EXPECT_EQ(entries.first().path, QStringLiteral("/tmp/a"));
    EXPECT_EQ(entries.first().kind, QStringLiteral("file"));
    EXPECT_EQ(database.recent(1).size(), 1);

    database.removeRecent(QStringLiteral("/tmp/a"));
    EXPECT_EQ(database.recent(10).size(), 1);
    database.clearRecent();
    EXPECT_TRUE(database.recent(10).isEmpty());
}

TEST_F(DatabaseTest, SessionStateRoundTrip)
{
    QJsonObject session;
    session.insert(QStringLiteral("project"), QStringLiteral("/tmp/project"));
    database.saveSession(QStringLiteral("main"), session);
    const QJsonObject restored = database.session(QStringLiteral("main"));
    EXPECT_EQ(restored.value(QStringLiteral("project")).toString(),
              QStringLiteral("/tmp/project"));
}

TEST_F(DatabaseTest, AnalysisCacheInvalidatesOnSizeOrMtime)
{
    const QString path = QStringLiteral("/tmp/package.pkg");
    database.cacheAnalysis(path, 4096, 1000, QStringLiteral("PS4 PKG"),
                           QByteArray("{\"kind\":5}"));
    ASSERT_EQ(database.analysisCacheCount(), 1);

    auto hit = database.cachedAnalysis(path, 4096, 1000);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->format, QStringLiteral("PS4 PKG"));
    EXPECT_EQ(hit->payload, QByteArray("{\"kind\":5}"));

    EXPECT_FALSE(database.cachedAnalysis(path, 4097, 1000).has_value());
    EXPECT_FALSE(database.cachedAnalysis(path, 4096, 999).has_value());

    database.cacheAnalysis(path, 4096, 1000, QStringLiteral("PE"), QByteArray("{}"));
    EXPECT_EQ(database.analysisCacheCount(), 1);

    database.clearAnalysisCache();
    EXPECT_EQ(database.analysisCacheCount(), 0);
}

TEST_F(DatabaseTest, SettingsSingletonClampsValues)
{
    Settings::instance().attach(&database);
    AppSettings values = Settings::instance().values();
    EXPECT_EQ(values.fontSize, 10);
    EXPECT_EQ(values.themeId, QStringLiteral("pkgstudio.voidblack"));

    values.fontSize = 99;
    values.hexBytesPerRow = 4096;
    values.workerThreads = -5;
    values.recentLimit = 1000;
    Settings::instance().setValues(values);

    const AppSettings stored = Settings::instance().values();
    EXPECT_EQ(stored.fontSize, 24);
    EXPECT_EQ(stored.hexBytesPerRow, 64);
    EXPECT_EQ(stored.workerThreads, 0);
    EXPECT_EQ(stored.recentLimit, 100);

    Settings::instance().setShortcut(QStringLiteral("file.openFile"),
                                     QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    EXPECT_EQ(Settings::instance()
                  .shortcut(QStringLiteral("file.openFile"), QKeySequence())
                  .toString(QKeySequence::PortableText),
              QStringLiteral("Ctrl+Shift+O"));
    EXPECT_TRUE(Settings::instance().hasShortcutOverride(QStringLiteral("file.openFile")));
    Settings::instance().resetShortcuts();
    EXPECT_FALSE(Settings::instance().hasShortcutOverride(QStringLiteral("file.openFile")));
}

TEST(AtomicFileTest, WritesAtomicallyAndBacksUp)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("out.txt"));
    ASSERT_TRUE(atomic::writeFile(path, QByteArray("first")));
    EXPECT_EQ(atomic::readFile(path, 1024).value(), QByteArray("first"));

    ASSERT_TRUE(atomic::writeFile(path, QByteArray("second")));
    EXPECT_EQ(atomic::readFile(path, 1024).value(), QByteArray("second"));

    const QString backup = dir.filePath(QStringLiteral("backup/out.txt.bak"));
    const auto copied = atomic::backupFile(path, backup);
    ASSERT_TRUE(copied);
    EXPECT_EQ(copied.value(), 6U);
    EXPECT_EQ(atomic::readFile(backup, 1024).value(), QByteArray("second"));

    EXPECT_FALSE(atomic::readFile(path, 2));
    EXPECT_FALSE(atomic::readFile(dir.filePath(QStringLiteral("missing")), 10));
}

TEST(LoggerTest, FiltersLevelsAndRedactsPaths)
{
    QTemporaryDir dir;
    Logger& logger = Logger::instance();
    ASSERT_TRUE(logger.initialize(dir.path(), LogLevel::Info, 100));
    logger.clear();

    logger.append(LogLevel::Debug, QStringLiteral("test"), QStringLiteral("debug line"));
    logger.append(LogLevel::Info, QStringLiteral("test"), QStringLiteral("info line"));
    logger.append(LogLevel::Error, QStringLiteral("test"), QStringLiteral("error line"));

    const QList<LogRecord> records = logger.snapshot();
    ASSERT_EQ(records.size(), 2);
    EXPECT_EQ(records.at(0).message, QStringLiteral("info line"));
    EXPECT_EQ(records.at(1).level, LogLevel::Error);

    const QString redacted =
        Logger::redact(QStringLiteral("cannot open /home/user/secret/package.pkg here"));
    EXPECT_FALSE(redacted.contains(QStringLiteral("/home/user")));
    EXPECT_TRUE(redacted.contains(QStringLiteral("<path>")));

    EXPECT_EQ(Logger::levelName(LogLevel::Warning), QStringLiteral("WARN"));
    EXPECT_EQ(Logger::levelFromName(QStringLiteral("error")), LogLevel::Error);
}

TEST(ThemeTest, BuiltinThemesAreComplete)
{
    const QList<Theme> themes = Theme::builtins();
    ASSERT_GE(themes.size(), 4);
    QStringList ids;
    for (const Theme& theme : themes) {
        QString reason;
        EXPECT_TRUE(theme.isValid(&reason)) << reason.toStdString();
        for (const QString& role : Theme::requiredRoles())
            EXPECT_TRUE(theme.colors.contains(role)) << role.toStdString();
        ids << theme.id;
    }
    EXPECT_TRUE(ids.contains(QStringLiteral("pkgstudio.dark")));
    EXPECT_TRUE(ids.contains(QStringLiteral("pkgstudio.cyberpunk")));
    EXPECT_TRUE(ids.contains(QStringLiteral("pkgstudio.voidblack")));
    EXPECT_TRUE(ids.contains(QStringLiteral("pkgstudio.neonhex")));
}

TEST(ThemeTest, VoidBlackIsTheDefaultPalette)
{
    const QList<Theme> themes = Theme::builtins();
    const auto it = std::find_if(themes.begin(), themes.end(), [](const Theme& theme) {
        return theme.id == QLatin1String("pkgstudio.voidblack");
    });
    ASSERT_NE(it, themes.end());

    EXPECT_EQ(it->color(QStringLiteral("window")), QColor(QStringLiteral("#0a0a0a")));
    EXPECT_EQ(it->color(QStringLiteral("panel")), QColor(QStringLiteral("#101010")));
    EXPECT_EQ(it->color(QStringLiteral("accent")), QColor(QStringLiteral("#00ff41")));

    EXPECT_GE(contrastRatio(it->color(QStringLiteral("windowText")),
                            it->color(QStringLiteral("window"))),
              7.0);
    EXPECT_GE(contrastRatio(it->color(QStringLiteral("panelText")),
                            it->color(QStringLiteral("panel"))),
              4.5);
    EXPECT_GE(contrastRatio(it->color(QStringLiteral("hexBytes")),
                            it->color(QStringLiteral("base"))),
              7.0);

    EXPECT_EQ(ThemeManager::instance().fallback().id, QStringLiteral("pkgstudio.voidblack"));
}

TEST(ThemeTest, ApexThemeCollectionIsComplete)
{
    const QList<Theme> themes = Theme::builtins();
    const QStringList expected = {
        QStringLiteral("pkgstudio.cyber.blue"),
        QStringLiteral("pkgstudio.blood.circuit"),
        QStringLiteral("pkgstudio.terminal.classic"),
        QStringLiteral("pkgstudio.arctic.dev"),
    };
    for (const QString& id : expected) {
        const auto it = std::find_if(themes.begin(), themes.end(), [&id](const Theme& theme) {
            return theme.id == id;
        });
        ASSERT_NE(it, themes.end()) << id.toStdString();
        EXPECT_FALSE(it->description.trimmed().isEmpty()) << id.toStdString();
        EXPECT_GE(it->contrast(QStringLiteral("baseText"), QStringLiteral("base")), 7.0)
            << id.toStdString();
        EXPECT_GE(it->contrast(QStringLiteral("panelText"), QStringLiteral("panel")), 4.5)
            << id.toStdString();
        EXPECT_GE(it->contrast(QStringLiteral("windowText"), QStringLiteral("window")), 7.0)
            << id.toStdString();
    }

    // Every built-in theme must be describable and stay readable.
    for (const Theme& theme : themes) {
        EXPECT_FALSE(theme.description.trimmed().isEmpty()) << theme.id.toStdString();
        EXPECT_GE(theme.contrast(QStringLiteral("baseText"), QStringLiteral("base")), 4.5)
            << theme.id.toStdString();
        EXPECT_GE(theme.contrast(QStringLiteral("hexBytes"), QStringLiteral("base")), 4.5)
            << theme.id.toStdString();
    }

    EXPECT_FALSE(themes.first().colors.isEmpty());
}

TEST(ThemeTest, ThemeDescriptionSurvivesJsonRoundTrip)
{
    Theme theme = Theme::builtins().first();
    theme.description = QStringLiteral("Round trip description");
    const auto restored = Theme::fromJson(theme.toJson());
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored.value().description, theme.description);
}

TEST(ThemeTest, JsonRoundTripAndValidation)
{
    Theme dark = Theme::builtins().first();
    const auto restored = Theme::fromJson(dark.toJson());
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored.value().id, dark.id);
    EXPECT_EQ(restored.value().color(QStringLiteral("window")),
              dark.color(QStringLiteral("window")));

    QJsonObject broken = dark.toJson();
    QJsonObject colors = broken.value(QStringLiteral("colors")).toObject();
    colors.remove(QStringLiteral("hexOffset"));
    broken.insert(QStringLiteral("colors"), colors);
    EXPECT_FALSE(Theme::fromJson(broken));

    QJsonObject invalidColor = dark.toJson();
    colors = invalidColor.value(QStringLiteral("colors")).toObject();
    colors.insert(QStringLiteral("window"), QStringLiteral("not-a-colour"));
    invalidColor.insert(QStringLiteral("colors"), colors);
    EXPECT_FALSE(Theme::fromJson(invalidColor));
}

TEST(ThemeTest, StyleSheetSubstitutionIsComplete)
{
    const Theme dark = Theme::builtins().first();
    const QString sheet = ThemeManager::styleSheet(dark);
    EXPECT_FALSE(sheet.contains(QLatin1Char('@')));
    EXPECT_TRUE(sheet.contains(QStringLiteral("background-color: #1e1e1e")));
    // A naive prefix replacement previously produced values like "#1e1e1eText".
    EXPECT_FALSE(sheet.contains(QStringLiteral("1e1e1eText")));
}

TEST(ThemeTest, UserThemeSaveExportImport)
{
    QTemporaryDir dir;
    ThemeManager& manager = ThemeManager::instance();
    manager.initialize(dir.path());

    Theme custom = Theme::builtins().first();
    custom.id = QStringLiteral("test.custom");
    custom.name = QStringLiteral("Test Custom");
    ASSERT_TRUE(manager.saveUserTheme(custom));
    EXPECT_TRUE(manager.contains(QStringLiteral("test.custom")));

    const QString exported = dir.filePath(QStringLiteral("exported.json"));
    ASSERT_TRUE(manager.exportThemeFile(QStringLiteral("test.custom"), exported));
    EXPECT_TRUE(QFile::exists(exported));

    const QString imported = dir.filePath(QStringLiteral("imported.json"));
    QFile file(imported);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("{\"id\":\"test.imported\",\"name\":\"Imported\",\"colors\":{"
               "\"window\":\"#101010\"}}");
    file.close();
    EXPECT_FALSE(manager.importThemeFile(imported));  // incomplete theme rejected
}
