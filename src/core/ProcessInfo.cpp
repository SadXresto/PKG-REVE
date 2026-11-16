#include "core/ProcessInfo.h"

#include <QFile>
#include <QSysInfo>
#include <QThread>

#if defined(Q_OS_WIN)
#  include <windows.h>
#  include <psapi.h>
#elif defined(Q_OS_LINUX)
#  include <unistd.h>
#endif

namespace pkg::sysinfo {

Result<quint64> currentResidentBytes()
{
#if defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS counters{};
    if (::GetProcessMemoryInfo(::GetCurrentProcess(), &counters, sizeof(counters)) == 0)
        return makeError(QStringLiteral("GetProcessMemoryInfo failed"));
    return static_cast<quint64>(counters.WorkingSetSize);
#elif defined(Q_OS_LINUX)
    QFile status(QStringLiteral("/proc/self/statm"));
    if (!status.open(QIODevice::ReadOnly))
        return makeError(QStringLiteral("Cannot read /proc/self/statm"));
    const QByteArray content = status.readAll().trimmed();
    const QList<QByteArray> parts = content.split(' ');
    if (parts.size() < 2)
        return makeError(QStringLiteral("Unexpected /proc/self/statm format"));
    bool ok = false;
    const quint64 residentPages = parts.at(1).toULongLong(&ok);
    if (!ok)
        return makeError(QStringLiteral("Cannot parse resident pages"));
    const quint64 pageSize = static_cast<quint64>(::sysconf(_SC_PAGESIZE));
    return residentPages * pageSize;
#else
    return makeError(QStringLiteral("Resident memory is not implemented on this platform"));
#endif
}

QString operatingSystem()
{
    return QStringLiteral("%1 %2 (%3)")
        .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(),
             QSysInfo::kernelVersion());
}

QString cpuDescription()
{
#if defined(Q_OS_LINUX)
    QFile cpuInfo(QStringLiteral("/proc/cpuinfo"));
    if (cpuInfo.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = cpuInfo.readAll().split('\n');
        for (const QByteArray& line : lines) {
            if (line.startsWith("model name")) {
                const int index = line.indexOf(':');
                if (index >= 0)
                    return QString::fromUtf8(line.mid(index + 1).trimmed());
            }
        }
    }
#endif
    return QStringLiteral("Unknown CPU");
}

int logicalCpuCount()
{
    return QThread::idealThreadCount();
}

quint64 totalRamBytes()
{
#if defined(Q_OS_LINUX)
    QFile memInfo(QStringLiteral("/proc/meminfo"));
    if (memInfo.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = memInfo.readAll().split('\n');
        for (const QByteArray& line : lines) {
            if (line.startsWith("MemTotal:")) {
                const QList<QByteArray> parts = line.simplified().split(' ');
                if (parts.size() >= 2) {
                    bool ok = false;
                    const quint64 kib = parts.at(1).toULongLong(&ok);
                    if (ok)
                        return kib * 1024;
                }
            }
        }
    }
#endif
    return 0;
}

}  // namespace pkg::sysinfo
