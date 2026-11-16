#include "core/Logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QThread>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>

#if defined(Q_OS_LINUX)
#  include <execinfo.h>
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace pkg {
namespace {

struct CrashContext {
    const char* logDirectory = nullptr;
    const char* version = nullptr;
};
CrashContext g_crashContext{};

void writeAllFd(int fd, const char* text)
{
    if (fd < 0 || text == nullptr)
        return;
    std::size_t remaining = std::strlen(text);
    const char* cursor = text;
    while (remaining > 0) {
        const ssize_t written = ::write(fd, cursor, remaining);
        if (written <= 0)
            break;
        cursor += written;
        remaining -= static_cast<std::size_t>(written);
    }
}

#if defined(Q_OS_LINUX)
void crashSignalHandler(int signalNumber)
{
    char body[8192];
    const std::time_t now = std::time(nullptr);
    std::tm tmValue{};
    ::gmtime_r(&now, &tmValue);

    int length = std::snprintf(body, sizeof(body),
                              "PKG Studio crash report\n"
                              "version: %s\n"
                              "signal: %d (%s)\n"
                              "utc: %04d-%02d-%02dT%02d:%02d:%02dZ\n"
                              "note: addresses only; no file content or paths are included\n"
                              "\nbacktrace:\n",
                              g_crashContext.version != nullptr ? g_crashContext.version : "unknown",
                              signalNumber, strsignal(signalNumber),
                              tmValue.tm_year + 1900, tmValue.tm_mon + 1, tmValue.tm_mday,
                              tmValue.tm_hour, tmValue.tm_min, tmValue.tm_sec);
    if (length < 0)
        length = 0;
    if (length > static_cast<int>(sizeof(body)))
        length = static_cast<int>(sizeof(body));

    void* frames[64];
    const int frameCount = ::backtrace(frames, 64);
    char* cursor = body + length;
    const std::size_t capacity = sizeof(body) - static_cast<std::size_t>(length);
    std::size_t used = 0;
    for (int i = 0; i < frameCount && used + 32 < capacity; ++i) {
        const int written = std::snprintf(cursor + used, capacity - used, "  #%02d %p\n", i,
                                          frames[i]);
        if (written <= 0)
            break;
        used += static_cast<std::size_t>(written);
    }

    if (g_crashContext.logDirectory != nullptr) {
        char path[2048];
        std::snprintf(path, sizeof(path), "%s/crash-%ld.log", g_crashContext.logDirectory,
                      static_cast<long>(now));
        const int fd = ::open(path, O_CREAT | O_WRONLY | O_APPEND, 0600);
        if (fd >= 0) {
            writeAllFd(fd, body);
            ::close(fd);
        }
    }

    ::signal(signalNumber, SIG_DFL);
    ::raise(signalNumber);
}

void terminateHandler()
{
    char message[256];
    std::snprintf(message, sizeof(message), "terminate called, aborting\n");
    if (g_crashContext.logDirectory != nullptr) {
        char path[2048];
        std::snprintf(path, sizeof(path), "%s/crash-terminate-%ld.log", g_crashContext.logDirectory,
                      static_cast<long>(std::time(nullptr)));
        const int fd = ::open(path, O_CREAT | O_WRONLY | O_APPEND, 0600);
        if (fd >= 0) {
            writeAllFd(fd, message);
            ::close(fd);
        }
    }
    std::abort();
}
#endif

}  // namespace

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::Logger() = default;

Logger::~Logger()
{
    QMutexLocker lock(&m_mutex);
    if (m_fileHandle >= 0) {
#if defined(Q_OS_LINUX)
        ::close(m_fileHandle);
#endif
        m_fileHandle = -1;
    }
}

bool Logger::initialize(const QString& logDirectory, LogLevel minLevel, qsizetype maxRecords,
                        qint64 maxFileBytes)
{
    QMutexLocker lock(&m_mutex);
    m_logDirectory = logDirectory;
    m_minLevel = minLevel;
    m_maxRecords = qMax<qsizetype>(1, maxRecords);
    m_maxFileBytes = qMax<qint64>(64 * 1024, maxFileBytes);

    if (!QDir().mkpath(logDirectory)) {
        m_fileLogging = false;
        emit logFileProblem(QStringLiteral("Cannot create log directory: %1").arg(logDirectory));
        return false;
    }

    m_logFilePath = QDir(logDirectory).filePath(QStringLiteral("pkgstudio.log"));
    rotateIfNeededLocked();

#if defined(Q_OS_LINUX)
    if (m_fileHandle >= 0) {
        ::close(m_fileHandle);
        m_fileHandle = -1;
    }
    m_fileHandle = ::open(QFile::encodeName(m_logFilePath).constData(),
                          O_CREAT | O_WRONLY | O_APPEND, 0600);
    if (m_fileHandle < 0) {
        m_fileLogging = false;
        emit logFileProblem(QStringLiteral("Cannot open log file: %1").arg(m_logFilePath));
        return false;
    }
    m_currentFileBytes = QFileInfo(m_logFilePath).size();
#endif
    return true;
}

void Logger::rotateIfNeededLocked()
{
    const QFileInfo info(m_logFilePath);
    if (!info.exists() || info.size() < m_maxFileBytes)
        return;
    const QString rotated = m_logFilePath + QStringLiteral(".1");
    QFile::remove(rotated);
    QFile::rename(m_logFilePath, rotated);
}

void Logger::setMinLevel(LogLevel level)
{
    QMutexLocker lock(&m_mutex);
    m_minLevel = level;
}

LogLevel Logger::minLevel() const
{
    QMutexLocker lock(&m_mutex);
    return m_minLevel;
}

void Logger::setFileLoggingEnabled(bool enabled)
{
    QMutexLocker lock(&m_mutex);
    m_fileLogging = enabled;
}

bool Logger::fileLoggingEnabled() const
{
    QMutexLocker lock(&m_mutex);
    return m_fileLogging;
}

QString Logger::logFilePath() const
{
    QMutexLocker lock(&m_mutex);
    return m_logFilePath;
}

QString Logger::logDirectory() const
{
    QMutexLocker lock(&m_mutex);
    return m_logDirectory;
}

void Logger::append(LogLevel level, const QString& category, const QString& message)
{
    if (message.isEmpty())
        return;

    LogRecord record;
    {
        QMutexLocker lock(&m_mutex);
        if (level < m_minLevel)
            return;
        record.timestamp = QDateTime::currentDateTime();
        record.level = level;
        record.category = category;
        record.message = message;
        record.threadName = QThread::currentThread()->objectName();
        if (record.threadName.isEmpty())
            record.threadName = QStringLiteral("main");

        m_records.append(record);
        while (m_records.size() > m_maxRecords)
            m_records.removeFirst();

        if (m_fileLogging)
            writeFileLocked(record);
    }
    emit recordAdded(record);
}

void Logger::writeFileLocked(const LogRecord& record)
{
#if defined(Q_OS_LINUX)
    if (m_fileHandle < 0)
        return;
    const QString line = QStringLiteral("%1 [%2] [%3] %4\n")
                             .arg(record.timestamp.toString(Qt::ISODateWithMs),
                                  levelName(record.level).leftJustified(5),
                                  record.category, record.message);
    const QByteArray utf8 = line.toUtf8();
    if (::write(m_fileHandle, utf8.constData(), static_cast<std::size_t>(utf8.size())) < 0)
        return;
    m_currentFileBytes += static_cast<qint64>(utf8.size());
#endif
}

QList<LogRecord> Logger::snapshot() const
{
    QMutexLocker lock(&m_mutex);
    return m_records;
}

qsizetype Logger::recordCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_records.size();
}

void Logger::clear()
{
    QMutexLocker lock(&m_mutex);
    m_records.clear();
}

QString Logger::levelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace:
        return QStringLiteral("TRACE");
    case LogLevel::Debug:
        return QStringLiteral("DEBUG");
    case LogLevel::Info:
        return QStringLiteral("INFO");
    case LogLevel::Warning:
        return QStringLiteral("WARN");
    case LogLevel::Error:
        return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

LogLevel Logger::levelFromName(const QString& name, LogLevel fallback)
{
    const QString upper = name.trimmed().toUpper();
    if (upper == QLatin1String("TRACE"))
        return LogLevel::Trace;
    if (upper == QLatin1String("DEBUG"))
        return LogLevel::Debug;
    if (upper == QLatin1String("INFO"))
        return LogLevel::Info;
    if (upper == QLatin1String("WARN") || upper == QLatin1String("WARNING"))
        return LogLevel::Warning;
    if (upper == QLatin1String("ERROR"))
        return LogLevel::Error;
    return fallback;
}

QString Logger::redact(const QString& text)
{
    QString out = text;
    static const QRegularExpression unixPath(QStringLiteral("(/[A-Za-z0-9._-]+){2,}/?"));
    static const QRegularExpression winPath(QStringLiteral("[A-Za-z]:\\\\[^\\s\"']+"));
    out.replace(winPath, QStringLiteral("<path>"));
    out.replace(unixPath, QStringLiteral("<path>"));
    return out;
}

void Logger::installCrashHandler(const QString& logDirectory, const QString& applicationVersion)
{
    static QByteArray directory;
    static QByteArray version;
    directory = QFile::encodeName(logDirectory);
    version = applicationVersion.toUtf8();

    g_crashContext.logDirectory = directory.constData();
    g_crashContext.version = version.constData();

#if defined(Q_OS_LINUX)
    std::signal(SIGSEGV, crashSignalHandler);
    std::signal(SIGABRT, crashSignalHandler);
    std::signal(SIGFPE, crashSignalHandler);
    std::signal(SIGILL, crashSignalHandler);
    std::signal(SIGBUS, crashSignalHandler);
    std::set_terminate(terminateHandler);
#endif
}

void logTrace(const QString& category, const QString& message)
{
    Logger::instance().append(LogLevel::Trace, category, message);
}
void logDebug(const QString& category, const QString& message)
{
    Logger::instance().append(LogLevel::Debug, category, message);
}
void logInfo(const QString& category, const QString& message)
{
    Logger::instance().append(LogLevel::Info, category, message);
}
void logWarning(const QString& category, const QString& message)
{
    Logger::instance().append(LogLevel::Warning, category, message);
}
void logError(const QString& category, const QString& message)
{
    Logger::instance().append(LogLevel::Error, category, message);
}

}  // namespace pkg
