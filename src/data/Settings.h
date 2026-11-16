// Persisted application preferences (SQLite backed) and shortcut overrides.
#pragma once

#include "core/Logging.h"

#include <QKeySequence>
#include <QObject>
#include <QString>

namespace pkg {

class Database;

struct AppSettings {
    QString themeId = QStringLiteral("pkgstudio.dark");
    QString fontFamily;
    int fontSize = 10;
    bool lightweight = false;

    int hexBytesPerRow = 16;
    int hexGroupSize = 1;

    LogLevel logLevel = LogLevel::Info;
    bool fileLogging = true;

    int workerThreads = 0;  // 0 = automatic (hardware threads, capped)
    int cacheLimitMB = 64;

    bool confirmWrites = true;
    bool restoreSession = true;
    int recentLimit = 15;

    QString lastProject;
};

class Settings : public QObject {
    Q_OBJECT

public:
    static Settings& instance();

    /// Binds the settings store. Values are loaded immediately.
    void attach(Database* database);

    [[nodiscard]] AppSettings values() const { return m_values; }
    void setValues(const AppSettings& values);

    [[nodiscard]] QKeySequence shortcut(const QString& actionId,
                                        const QKeySequence& fallback) const;
    void setShortcut(const QString& actionId, const QKeySequence& sequence);
    void resetShortcuts();
    [[nodiscard]] bool hasShortcutOverride(const QString& actionId) const;

    [[nodiscard]] static QString dataDirectory();
    [[nodiscard]] static QString logsDirectory();
    [[nodiscard]] static QString themesDirectory();
    [[nodiscard]] static QString databasePath();

signals:
    void changed();

private:
    Settings() = default;
    void load();
    void save();

    Database* m_database = nullptr;
    AppSettings m_values;
};

}  // namespace pkg
