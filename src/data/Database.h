// SQLite storage for settings, recent projects, session state and the analysis
// cache. All methods must be called from the thread that created the instance.
#pragma once

#include "core/Result.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace pkg {

struct RecentEntry {
    QString path;
    QString kind;  // "file" or "project"
    QDateTime openedAt;
};

struct CachedAnalysis {
    QString format;
    QByteArray payload;
};

class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    [[nodiscard]] Status open(const QString& path);
    void close();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] int schemaVersion() const;

    // Settings -------------------------------------------------------------
    [[nodiscard]] QString setting(const QString& key, const QString& fallback = {}) const;
    void setSetting(const QString& key, const QString& value);
    void removeSetting(const QString& key);
    void removeSettingsWithPrefix(const QString& prefix);

    // Recent entries -------------------------------------------------------
    void addRecent(const QString& path, const QString& kind);
    [[nodiscard]] QList<RecentEntry> recent(int limit = 15) const;
    void removeRecent(const QString& path);
    void clearRecent();

    // Session --------------------------------------------------------------
    void saveSession(const QString& key, const QJsonObject& value);
    [[nodiscard]] QJsonObject session(const QString& key) const;

    // Analysis cache -------------------------------------------------------
    void cacheAnalysis(const QString& path, quint64 size, qint64 modifiedMs,
                       const QString& format, const QByteArray& payload);
    [[nodiscard]] std::optional<CachedAnalysis> cachedAnalysis(const QString& path,
                                                               quint64 size,
                                                               qint64 modifiedMs) const;
    void trimAnalysisCache(int keepEntries);
    void clearAnalysisCache();

    /// Number of rows in the analysis cache (diagnostics and tests).
    [[nodiscard]] int analysisCacheCount() const;

private:
    [[nodiscard]] Status migrate();
    [[nodiscard]] Status exec(const QString& sql) const;

    QString m_path;
    QString m_connectionName;
    mutable QString m_lastError;
    bool m_open = false;
};

}  // namespace pkg
