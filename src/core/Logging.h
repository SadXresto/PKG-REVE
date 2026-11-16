// Structured logging: rotating log file, bounded in-memory ring buffer used by
// the Output panel, and redacted crash reports.
#pragma once

#include <QDateTime>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>

namespace pkg {

enum class LogLevel { Trace = 0, Debug, Info, Warning, Error };

struct LogRecord {
    QDateTime timestamp;
    LogLevel level = LogLevel::Info;
    QString category;
    QString message;
    QString threadName;
};

class Logger : public QObject {
    Q_OBJECT

public:
    static Logger& instance();
    ~Logger() override;

    /// Opens (and rotates) the log file. Safe to call once at startup.
    bool initialize(const QString& logDirectory,
                    LogLevel minLevel,
                    qsizetype maxRecords = 4000,
                    qint64 maxFileBytes = 8 * 1024 * 1024);

    void setMinLevel(LogLevel level);
    [[nodiscard]] LogLevel minLevel() const;
    void setFileLoggingEnabled(bool enabled);
    [[nodiscard]] bool fileLoggingEnabled() const;
    [[nodiscard]] QString logFilePath() const;
    [[nodiscard]] QString logDirectory() const;

    /// Thread safe. Records below the minimum level are dropped.
    void append(LogLevel level, const QString& category, const QString& message);

    [[nodiscard]] QList<LogRecord> snapshot() const;
    [[nodiscard]] qsizetype recordCount() const;
    void clear();

    static QString levelName(LogLevel level);
    static LogLevel levelFromName(const QString& name, LogLevel fallback = LogLevel::Info);

    /// Removes absolute file paths and user names from diagnostic text.
    static QString redact(const QString& text);

    /// Installs handlers that write a redacted crash report then re-raise.
    static void installCrashHandler(const QString& logDirectory,
                                    const QString& applicationVersion);

signals:
    void recordAdded(const pkg::LogRecord& record);
    void logFileProblem(const QString& message);

private:
    Logger();
    void rotateIfNeededLocked();
    void writeFileLocked(const LogRecord& record);

    mutable QMutex m_mutex;
    QList<LogRecord> m_records;
    qsizetype m_maxRecords = 4000;
    LogLevel m_minLevel = LogLevel::Info;
    bool m_fileLogging = true;
    QString m_logDirectory;
    QString m_logFilePath;
    qint64 m_maxFileBytes = 8 * 1024 * 1024;
    qint64 m_currentFileBytes = 0;
    int m_fileHandle = -1;
};

/// Convenience helpers; thread safe.
void logTrace(const QString& category, const QString& message);
void logDebug(const QString& category, const QString& message);
void logInfo(const QString& category, const QString& message);
void logWarning(const QString& category, const QString& message);
void logError(const QString& category, const QString& message);

}  // namespace pkg

Q_DECLARE_METATYPE(pkg::LogRecord)
