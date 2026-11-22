#include "data/Database.h"

#include "core/Logging.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace pkg {
namespace {
constexpr auto kLogCategory = "database";
constexpr int kSchemaVersion = 2;
}  // namespace

Database::~Database()
{
    close();
}

Status Database::open(const QString& path)
{
    if (m_open)
        close();

    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory))
        return makeError(QStringLiteral("Cannot create database directory"), directory);

    m_connectionName = QStringLiteral("pkgstudio-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    if (!db.isValid())
        return makeError(QStringLiteral("The Qt SQLite driver is not available"));
    db.setDatabaseName(path);
    if (!db.open()) {
        m_lastError = db.lastError().text();
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
        logError(QString::fromLatin1(kLogCategory),
                 QStringLiteral("Cannot open database: %1").arg(m_lastError));
        return makeError(QStringLiteral("Cannot open database"), m_lastError);
    }

    m_path = path;
    m_open = true;

    auto busy = exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
    auto wal = exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    auto sync = exec(QStringLiteral("PRAGMA synchronous = NORMAL"));
    Q_UNUSED(busy)
    Q_UNUSED(wal)
    Q_UNUSED(sync)

    const Status migration = migrate();
    if (!migration) {
        close();
        return migration;
    }
    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("Database ready at %1 (schema v%2)").arg(path).arg(schemaVersion()));
    return okStatus();
}

void Database::close()
{
    if (!m_open && m_connectionName.isEmpty())
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isOpen())
            db.close();
    }
    if (!m_connectionName.isEmpty())
        QSqlDatabase::removeDatabase(m_connectionName);
    m_connectionName.clear();
    m_open = false;
}

bool Database::isOpen() const
{
    if (!m_open)
        return false;
    const QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    return db.isOpen();
}

Status Database::exec(const QString& sql) const
{
    if (m_connectionName.isEmpty())
        return makeError(QStringLiteral("Database is not open"));
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    if (!query.exec(sql)) {
        m_lastError = query.lastError().text();
        logError(QString::fromLatin1(kLogCategory),
                 QStringLiteral("SQL failed: %1").arg(m_lastError));
        return makeError(QStringLiteral("SQL statement failed"), m_lastError);
    }
    return okStatus();
}

Status Database::migrate()
{
    QSqlQuery versionQuery(QSqlDatabase::database(m_connectionName, false));
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next())
        return makeError(QStringLiteral("Cannot read database schema version"));

    const int version = versionQuery.value(0).toInt();
    if (version >= kSchemaVersion)
        return okStatus();

    const Status began = exec(QStringLiteral("BEGIN IMMEDIATE"));
    if (!began)
        return began;

    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS settings ("
                       "key TEXT PRIMARY KEY, value TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS recent ("
                       "path TEXT PRIMARY KEY, kind TEXT NOT NULL, opened_at INTEGER NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS recent_opened_idx ON recent(opened_at DESC)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS session ("
                       "key TEXT PRIMARY KEY, value TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS analysis_cache ("
                       "path TEXT PRIMARY KEY, size INTEGER NOT NULL, mtime INTEGER NOT NULL, "
                       "format TEXT NOT NULL, payload BLOB NOT NULL, created_at INTEGER NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS analysis_created_idx "
                       "ON analysis_cache(created_at DESC)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS devflow ("
                       "kind TEXT NOT NULL, id TEXT NOT NULL, payload TEXT NOT NULL, "
                       "updated_at INTEGER NOT NULL, PRIMARY KEY(kind, id))"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS devflow_kind_idx ON devflow(kind, updated_at DESC)"),
    };

    for (const QString& statement : statements) {
        const Status status = exec(statement);
        if (!status) {
            const Status rolledBack = exec(QStringLiteral("ROLLBACK"));
            Q_UNUSED(rolledBack)
            return status;
        }
    }

    if (!exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion))) {
        const Status rolledBack = exec(QStringLiteral("ROLLBACK"));
        Q_UNUSED(rolledBack)
        return makeError(QStringLiteral("Cannot record schema version"));
    }
    const Status committed = exec(QStringLiteral("COMMIT"));
    if (!committed)
        return committed;
    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("Migrated database to schema v%1").arg(kSchemaVersion));
    return okStatus();
}

int Database::schemaVersion() const
{
    if (!isOpen())
        return 0;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next())
        return query.value(0).toInt();
    return 0;
}

QString Database::setting(const QString& key, const QString& fallback) const
{
    if (!isOpen())
        return fallback;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("SELECT value FROM settings WHERE key = ?"));
    query.addBindValue(key);
    if (query.exec() && query.next())
        return query.value(0).toString();
    return fallback;
}

void Database::setSetting(const QString& key, const QString& value)
{
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("INSERT INTO settings(key, value) VALUES(?, ?) "
                                 "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    query.addBindValue(key);
    // A null QString binds as SQL NULL, which violates the NOT NULL schema.
    query.addBindValue(value.isNull() ? QStringLiteral("") : value);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        logError(QString::fromLatin1(kLogCategory),
                 QStringLiteral("Cannot store setting %1: %2").arg(key, m_lastError));
    }
}

void Database::removeSetting(const QString& key)
{
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("DELETE FROM settings WHERE key = ?"));
    query.addBindValue(key);
    query.exec();
}

void Database::removeSettingsWithPrefix(const QString& prefix)
{
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("DELETE FROM settings WHERE key LIKE ?"));
    query.addBindValue(prefix + QStringLiteral("%"));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        logError(QString::fromLatin1(kLogCategory),
                 QStringLiteral("Cannot remove settings: %1").arg(m_lastError));
    }
}

void Database::addRecent(const QString& path, const QString& kind)
{
    if (!isOpen() || path.isEmpty())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral(
        "INSERT INTO recent(path, kind, opened_at) VALUES(?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET kind = excluded.kind, "
        "opened_at = excluded.opened_at"));
    query.addBindValue(path);
    query.addBindValue(kind);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        logWarning(QString::fromLatin1(kLogCategory),
                   QStringLiteral("Cannot record recent entry: %1").arg(m_lastError));
    }
}

QList<RecentEntry> Database::recent(int limit) const
{
    QList<RecentEntry> entries;
    if (!isOpen())
        return entries;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral(
        "SELECT path, kind, opened_at FROM recent ORDER BY opened_at DESC LIMIT ?"));
    query.addBindValue(qMax(1, limit));
    if (!query.exec())
        return entries;
    while (query.next()) {
        RecentEntry entry;
        entry.path = query.value(0).toString();
        entry.kind = query.value(1).toString();
        entry.openedAt = QDateTime::fromSecsSinceEpoch(query.value(2).toLongLong());
        entries.append(entry);
    }
    return entries;
}

void Database::removeRecent(const QString& path)
{
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("DELETE FROM recent WHERE path = ?"));
    query.addBindValue(path);
    query.exec();
}

void Database::clearRecent()
{
    const Status status = exec(QStringLiteral("DELETE FROM recent"));
    if (!status)
        logWarning(QString::fromLatin1(kLogCategory),
                   QStringLiteral("Cannot clear recent entries: %1").arg(status.error().detail));
}

void Database::saveSession(const QString& key, const QJsonObject& value)
{
    if (!isOpen())
        return;
    const QByteArray json = QJsonDocument(value).toJson(QJsonDocument::Compact);
    setSetting(QStringLiteral("session.%1").arg(key), QString::fromUtf8(json));
}

QJsonObject Database::session(const QString& key) const
{
    const QString json = setting(QStringLiteral("session.%1").arg(key));
    if (json.isEmpty())
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    return document.object();
}

void Database::saveDevflowItem(const QString& kind, const QString& id, const QString& payload)
{
    if (!isOpen() || kind.isEmpty() || id.isEmpty())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral(
        "INSERT INTO devflow(kind, id, payload, updated_at) VALUES(?, ?, ?, ?) "
        "ON CONFLICT(kind, id) DO UPDATE SET payload = excluded.payload, "
        "updated_at = excluded.updated_at"));
    query.addBindValue(kind);
    query.addBindValue(id);
    query.addBindValue(payload);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        logWarning(QString::fromLatin1(kLogCategory),
                   QStringLiteral("Cannot save DevFlow item: %1").arg(m_lastError));
    }
}

QList<QPair<QString, QString>> Database::devflowItems(const QString& kind) const
{
    QList<QPair<QString, QString>> items;
    if (!isOpen())
        return items;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral(
        "SELECT id, payload FROM devflow WHERE kind = ? ORDER BY updated_at DESC, id"));
    query.addBindValue(kind);
    if (!query.exec())
        return items;
    while (query.next())
        items.append({query.value(0).toString(), query.value(1).toString()});
    return items;
}

void Database::removeDevflowItem(const QString& kind, const QString& id)
{
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("DELETE FROM devflow WHERE kind = ? AND id = ?"));
    query.addBindValue(kind);
    query.addBindValue(id);
    query.exec();
}

void Database::clearDevflowItems(const QString& kind)
{
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("DELETE FROM devflow WHERE kind = ?"));
    query.addBindValue(kind);
    query.exec();
}

void Database::cacheAnalysis(const QString& path, quint64 size, qint64 modifiedMs,
                             const QString& format, const QByteArray& payload)
{
    if (!isOpen() || path.isEmpty())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral(
        "INSERT INTO analysis_cache(path, size, mtime, format, payload, created_at) "
        "VALUES(?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET size = excluded.size, mtime = excluded.mtime, "
        "format = excluded.format, payload = excluded.payload, "
        "created_at = excluded.created_at"));
    query.addBindValue(path);
    query.addBindValue(QVariant::fromValue(size));
    query.addBindValue(modifiedMs);
    query.addBindValue(format);
    query.addBindValue(payload);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        logWarning(QString::fromLatin1(kLogCategory),
                   QStringLiteral("Cannot cache analysis: %1").arg(m_lastError));
    }
}

std::optional<CachedAnalysis> Database::cachedAnalysis(const QString& path, quint64 size,
                                                       qint64 modifiedMs) const
{
    if (!isOpen())
        return std::nullopt;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral(
        "SELECT format, payload FROM analysis_cache WHERE path = ? AND size = ? AND mtime = ?"));
    query.addBindValue(path);
    query.addBindValue(QVariant::fromValue(size));
    query.addBindValue(modifiedMs);
    if (!query.exec() || !query.next())
        return std::nullopt;

    CachedAnalysis cached;
    cached.format = query.value(0).toString();
    cached.payload = query.value(1).toByteArray();
    return cached;
}

void Database::trimAnalysisCache(int keepEntries)
{
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral(
        "DELETE FROM analysis_cache WHERE path NOT IN ("
        "SELECT path FROM analysis_cache ORDER BY created_at DESC LIMIT ?)"));
    query.addBindValue(qMax(1, keepEntries));
    query.exec();
}

void Database::clearAnalysisCache()
{
    const Status status = exec(QStringLiteral("DELETE FROM analysis_cache"));
    if (!status)
        logWarning(QString::fromLatin1(kLogCategory),
                   QStringLiteral("Cannot clear analysis cache: %1")
                       .arg(status.error().detail));
}

int Database::analysisCacheCount() const
{
    if (!isOpen())
        return 0;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    if (query.exec(QStringLiteral("SELECT COUNT(*) FROM analysis_cache")) && query.next())
        return query.value(0).toInt();
    return 0;
}

}  // namespace pkg
