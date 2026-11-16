#include "data/Settings.h"

#include "data/Database.h"

#include <QDir>
#include <QStandardPaths>
#include <algorithm>

namespace pkg {
namespace {

QString boolText(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

bool parseBool(const QString& text, bool fallback)
{
    if (text.isEmpty())
        return fallback;
    return text == QLatin1String("true") || text == QLatin1String("1");
}

int parseClamped(const QString& text, int fallback, int minimum, int maximum)
{
    if (text.isEmpty())
        return fallback;
    bool ok = false;
    const int value = text.toInt(&ok);
    if (!ok)
        return fallback;
    return std::clamp(value, minimum, maximum);
}

}  // namespace

QString Settings::dataDirectory()
{
    // PKGSTUDIO_DATA_DIR keeps automated runs and portable installs away from
    // the user's real settings and database.
    const QString override = qEnvironmentVariable("PKGSTUDIO_DATA_DIR");
    if (!override.isEmpty())
        return override;

    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.pkgstudio");
    return base;
}

QString Settings::logsDirectory()
{
    return QDir(dataDirectory()).filePath(QStringLiteral("logs"));
}

QString Settings::themesDirectory()
{
    return QDir(dataDirectory()).filePath(QStringLiteral("themes"));
}

QString Settings::databasePath()
{
    return QDir(dataDirectory()).filePath(QStringLiteral("pkgstudio.db"));
}

Settings& Settings::instance()
{
    static Settings settings;
    return settings;
}

void Settings::attach(Database* database)
{
    m_database = database;
    load();
}

void Settings::load()
{
    if (m_database == nullptr || !m_database->isOpen())
        return;

    AppSettings loaded;
    loaded.themeId = m_database->setting(QStringLiteral("ui.theme"), loaded.themeId);
    loaded.fontFamily = m_database->setting(QStringLiteral("editor.fontFamily"), QString());
    loaded.fontSize = parseClamped(m_database->setting(QStringLiteral("editor.fontSize")),
                                   loaded.fontSize, 7, 24);
    loaded.lightweight = parseBool(m_database->setting(QStringLiteral("ui.lightweight")),
                                   loaded.lightweight);
    loaded.hexBytesPerRow = parseClamped(m_database->setting(QStringLiteral("hex.bytesPerRow")),
                                         loaded.hexBytesPerRow, 8, 64);
    loaded.hexGroupSize = parseClamped(m_database->setting(QStringLiteral("hex.groupSize")),
                                       loaded.hexGroupSize, 1, 16);
    loaded.logLevel = Logger::levelFromName(
        m_database->setting(QStringLiteral("log.level")), loaded.logLevel);
    loaded.fileLogging = parseBool(m_database->setting(QStringLiteral("log.file")),
                                   loaded.fileLogging);
    loaded.workerThreads = parseClamped(m_database->setting(QStringLiteral("perf.workerThreads")),
                                       loaded.workerThreads, 0, 64);
    loaded.cacheLimitMB = parseClamped(m_database->setting(QStringLiteral("perf.cacheLimitMB")),
                                       loaded.cacheLimitMB, 8, 4096);
    loaded.confirmWrites = parseBool(m_database->setting(QStringLiteral("edit.confirmWrites")),
                                     loaded.confirmWrites);
    loaded.restoreSession = parseBool(m_database->setting(QStringLiteral("ui.restoreSession")),
                                      loaded.restoreSession);
    loaded.recentLimit = parseClamped(m_database->setting(QStringLiteral("ui.recentLimit")),
                                      loaded.recentLimit, 1, 100);
    loaded.lastProject = m_database->setting(QStringLiteral("project.lastPath"), QString());
    m_values = loaded;
}

void Settings::save()
{
    if (m_database == nullptr || !m_database->isOpen())
        return;

    const AppSettings& v = m_values;
    m_database->setSetting(QStringLiteral("ui.theme"), v.themeId);
    m_database->setSetting(QStringLiteral("editor.fontFamily"), v.fontFamily);
    m_database->setSetting(QStringLiteral("editor.fontSize"), QString::number(v.fontSize));
    m_database->setSetting(QStringLiteral("ui.lightweight"), boolText(v.lightweight));
    m_database->setSetting(QStringLiteral("hex.bytesPerRow"), QString::number(v.hexBytesPerRow));
    m_database->setSetting(QStringLiteral("hex.groupSize"), QString::number(v.hexGroupSize));
    m_database->setSetting(QStringLiteral("log.level"), Logger::levelName(v.logLevel));
    m_database->setSetting(QStringLiteral("log.file"), boolText(v.fileLogging));
    m_database->setSetting(QStringLiteral("perf.workerThreads"), QString::number(v.workerThreads));
    m_database->setSetting(QStringLiteral("perf.cacheLimitMB"), QString::number(v.cacheLimitMB));
    m_database->setSetting(QStringLiteral("edit.confirmWrites"), boolText(v.confirmWrites));
    m_database->setSetting(QStringLiteral("ui.restoreSession"), boolText(v.restoreSession));
    m_database->setSetting(QStringLiteral("ui.recentLimit"), QString::number(v.recentLimit));
    m_database->setSetting(QStringLiteral("project.lastPath"), v.lastProject);
}

void Settings::setValues(const AppSettings& values)
{
    AppSettings normalized = values;
    normalized.fontSize = std::clamp(values.fontSize, 7, 24);
    normalized.hexBytesPerRow = std::clamp(values.hexBytesPerRow, 8, 64);
    normalized.hexGroupSize = std::clamp(values.hexGroupSize, 1, 16);
    normalized.workerThreads = std::clamp(values.workerThreads, 0, 64);
    normalized.cacheLimitMB = std::clamp(values.cacheLimitMB, 8, 4096);
    normalized.recentLimit = std::clamp(values.recentLimit, 1, 100);
    m_values = normalized;
    save();
    emit changed();
}

QKeySequence Settings::shortcut(const QString& actionId, const QKeySequence& fallback) const
{
    if (m_database == nullptr || !m_database->isOpen())
        return fallback;
    const QString text =
        m_database->setting(QStringLiteral("shortcut.%1").arg(actionId), QString());
    if (text.isEmpty())
        return fallback;
    const QKeySequence sequence(text);
    return sequence.isEmpty() ? fallback : sequence;
}

void Settings::setShortcut(const QString& actionId, const QKeySequence& sequence)
{
    if (m_database == nullptr || !m_database->isOpen() || actionId.isEmpty())
        return;
    if (sequence.isEmpty())
        m_database->removeSetting(QStringLiteral("shortcut.%1").arg(actionId));
    else
        m_database->setSetting(QStringLiteral("shortcut.%1").arg(actionId),
                               sequence.toString(QKeySequence::PortableText));
}

void Settings::resetShortcuts()
{
    if (m_database == nullptr || !m_database->isOpen())
        return;
    m_database->removeSettingsWithPrefix(QStringLiteral("shortcut."));
}

bool Settings::hasShortcutOverride(const QString& actionId) const
{
    if (m_database == nullptr || !m_database->isOpen())
        return false;
    return !m_database->setting(QStringLiteral("shortcut.%1").arg(actionId)).isEmpty();
}

}  // namespace pkg
