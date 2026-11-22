#include "devflow/ProjectHealth.h"

#include "core/FormatUtil.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

namespace pkg::devflow {
namespace {

constexpr qint64 kLargeFileBytes = 512LL * 1024 * 1024;

void addFinding(HealthReport& report, HealthFinding finding)
{
    if (finding.evidence.trimmed().isEmpty())
        finding.evidence = QStringLiteral("No evidence captured");
    report.findings.append(finding);
}

}  // namespace

QString healthSeverityName(HealthSeverity severity)
{
    switch (severity) {
    case HealthSeverity::Info:
        return QStringLiteral("Info");
    case HealthSeverity::Warning:
        return QStringLiteral("Warning");
    case HealthSeverity::Critical:
        return QStringLiteral("Critical");
    }
    return QStringLiteral("Warning");
}

int HealthReport::criticalCount() const
{
    int count = 0;
    for (const HealthFinding& finding : findings) {
        if (finding.severity == HealthSeverity::Critical)
            ++count;
    }
    return count;
}

int HealthReport::warningCount() const
{
    int count = 0;
    for (const HealthFinding& finding : findings) {
        if (finding.severity == HealthSeverity::Warning)
            ++count;
    }
    return count;
}

HealthReport checkProjectHealth(const QString& root, const gameforge::ProjectScan& scan,
                                const gameforge::BuildConfigReport& config,
                                const gameforge::DependencyGraph& graph,
                                const gameforge::PortingReport& porting,
                                const CancelToken& token)
{
    HealthReport report;
    report.checkedFiles = scan.fileCount();
    report.checkedTargets = static_cast<int>(config.targets.size());
    report.scanTruncated = scan.truncated;

    if (token.isCancelled()) {
        report.cancelled = true;
        return report;
    }

    if (!scan.readable()) {
        addFinding(report,
                   {QStringLiteral("project.unreadable"),
                    QStringLiteral("The project folder could not be read"),
                    QStringLiteral("No files were scanned, so no health checks could run."),
                    scan.rootPath.isEmpty() ? root : scan.rootPath, QStringLiteral("confirmed"),
                    HealthSeverity::Critical});
        return report;
    }

    if (scan.kind == gameforge::ProjectKind::Generic ||
        scan.kind == gameforge::ProjectKind::Unknown) {
        addFinding(report,
                   {QStringLiteral("project.no-build-system"),
                    QStringLiteral("No supported build system was detected"),
                    QStringLiteral("GameForge recognises CMake, Meson, Makefile, Godot and "
                                   "Unreal projects. This folder has none of their marker "
                                   "files, so build and portability checks are limited."),
                    QStringLiteral("Scanned %1 file(s) under %2")
                        .arg(scan.fileCount())
                        .arg(scan.rootPath),
                    QStringLiteral("confirmed"), HealthSeverity::Warning});
    }

    if (scan.truncated) {
        addFinding(report,
                   {QStringLiteral("project.scan-truncated"),
                    QStringLiteral("The scan limit was reached"),
                    QStringLiteral("The project is large enough that the scanner stopped at its "
                                   "file or byte limit. Results describe the scanned part only."),
                    QStringLiteral("%1 file(s), %2")
                        .arg(scan.fileCount())
                        .arg(fmt::humanBytes(scan.totalBytes)),
                    QStringLiteral("confirmed"), HealthSeverity::Warning});
    }

    if (config.empty() && scan.kind != gameforge::ProjectKind::Generic) {
        addFinding(report,
                   {QStringLiteral("build.no-config"),
                    QStringLiteral("No build configuration could be parsed"),
                    QStringLiteral("A build system was detected but none of its build files "
                                   "could be read within the parser limits."),
                    scan.kindEvidence.isEmpty() ? QStringLiteral("Marker: %1")
                                                      .arg(gameforge::projectKindName(scan.kind))
                                                : scan.kindEvidence,
                    QStringLiteral("confirmed"), HealthSeverity::Warning});
    }

    if (config.truncated) {
        addFinding(report,
                   {QStringLiteral("build.config-truncated"),
                    QStringLiteral("Build files exceeded the parser limits"),
                    QStringLiteral("Some settings or dependencies may be missing from this "
                                   "report because a parser limit was reached."),
                    QStringLiteral("%1 line(s) read").arg(config.linesRead),
                    QStringLiteral("confirmed"), HealthSeverity::Warning});
    }

    for (const QString& warning : config.warnings) {
        addFinding(report,
                   {QStringLiteral("build.warning"),
                    QStringLiteral("Build configuration warning"),
                    warning, QStringLiteral("Build config parser"), QStringLiteral("confirmed"),
                    HealthSeverity::Info});
    }

    // Missing referenced source files ------------------------------------
    int missing = 0;
    for (const gameforge::BuildTarget& target : config.targets) {
        for (const QString& source : target.sources) {
            if (source.contains(QLatin1Char('$')) || source.contains(QStringLiteral("${")))
                continue; // unresolved variable: cannot be checked honestly
            const QFileInfo info(QDir(root).filePath(source));
            if (info.exists())
                continue;
            ++missing;
            addFinding(report,
                       {QStringLiteral("build.missing-source"),
                        QStringLiteral("Source file referenced by target '%1' is missing")
                            .arg(target.name),
                        QStringLiteral("The build file lists \"%1\" but it does not exist in "
                                       "the project. It may be generated at build time.")
                            .arg(source),
                        QStringLiteral("%1:%2").arg(target.file).arg(target.line),
                        QStringLiteral("confirmed"), HealthSeverity::Warning});
        }
    }

    // Dependencies -------------------------------------------------------
    for (const QString& unresolved : graph.unresolved) {
        addFinding(report,
                   {QStringLiteral("graph.unresolved"),
                    QStringLiteral("Dependency is referenced but not declared"),
                    unresolved,
                    QStringLiteral("Dependency graph: %1").arg(unresolved),
                    QStringLiteral("confirmed"), HealthSeverity::Warning});
    }
    for (const QString& cycle : graph.cycles) {
        addFinding(report,
                   {QStringLiteral("graph.cycle"),
                    QStringLiteral("Circular target dependency"),
                    QStringLiteral("Targets depend on each other in a cycle: %1").arg(cycle),
                    QStringLiteral("Dependency graph cycle detection"),
                    QStringLiteral("confirmed"), HealthSeverity::Critical});
    }
    if (graph.truncated) {
        addFinding(report,
                   {QStringLiteral("graph.truncated"),
                    QStringLiteral("The dependency graph was truncated"),
                    QStringLiteral("Not every dependency edge is shown; the graph hit its "
                                   "node or edge limit."),
                    QStringLiteral("%1 node(s), %2 edge(s)")
                        .arg(graph.nodes.size())
                        .arg(graph.edges.size()),
                    QStringLiteral("confirmed"), HealthSeverity::Warning});
    }

    // Portability signals ------------------------------------------------
    int reportedSignals = 0;
    for (const gameforge::PortingFinding& finding : porting.findings) {
        if (reportedSignals >= 8)
            break;
        if (finding.occurrences == 0)
            continue;
        ++reportedSignals;
        addFinding(report,
                   {QStringLiteral("portability.") + finding.id,
                    QStringLiteral("Portability signal: %1").arg(finding.label),
                    QStringLiteral("%1 (%2 occurrence(s) in %3 file(s))")
                        .arg(finding.note.isEmpty() ? QStringLiteral("Detected API family")
                                                    : finding.note)
                        .arg(finding.occurrences)
                        .arg(finding.fileCount),
                    finding.example.isEmpty() ? finding.files.join(QStringLiteral(", "))
                                              : finding.example,
                    QStringLiteral("confirmed"), HealthSeverity::Info});
    }
    if (porting.truncated) {
        addFinding(report,
                   {QStringLiteral("portability.truncated"),
                    QStringLiteral("Portability analysis reached a limit"),
                    QStringLiteral("Not every source file was read, so some API signals may be "
                                   "missing from this report."),
                    QStringLiteral("%1 file(s) scanned").arg(porting.scannedFiles),
                    QStringLiteral("confirmed"), HealthSeverity::Warning});
    }

    // Large files --------------------------------------------------------
    QSet<QString> reportedLarge;
    for (const gameforge::ProjectFile& file : scan.files) {
        if (file.size < kLargeFileBytes || reportedLarge.contains(file.relativePath))
            continue;
        reportedLarge.insert(file.relativePath);
        addFinding(report,
                   {QStringLiteral("files.large"),
                    QStringLiteral("Large file in the project"),
                    QStringLiteral("\"%1\" is %2. Large assets increase scan, packaging and "
                                   "version-control cost.")
                        .arg(file.relativePath, fmt::humanBytes(static_cast<quint64>(file.size))),
                    QStringLiteral("%1 \\u00B7 %2 bytes")
                        .arg(file.relativePath)
                        .arg(file.size),
                    QStringLiteral("confirmed"), HealthSeverity::Info});
    }

    // Unsupported engine configuration -----------------------------------
    if (scan.kind == gameforge::ProjectKind::Godot ||
        scan.kind == gameforge::ProjectKind::Unity ||
        scan.kind == gameforge::ProjectKind::Unreal) {
        addFinding(report,
                   {QStringLiteral("build.engine"),
                    QStringLiteral("Engine project detected: %1")
                        .arg(gameforge::projectKindName(scan.kind)),
                    QStringLiteral("GameForge can inspect this project, but DevFlow build "
                                   "profiles only drive CMake, Meson and Makefile projects. "
                                   "Build through the engine's own tooling."),
                    scan.kindEvidence, QStringLiteral("confirmed"), HealthSeverity::Info});
    }

    if (missing == 0 && config.targets.isEmpty() && !config.empty()) {
        addFinding(report,
                   {QStringLiteral("build.no-targets"),
                    QStringLiteral("No build targets were parsed"),
                    QStringLiteral("The build files were read, but no target declaration was "
                                   "recognised, so build profiles cannot offer a target list."),
                    config.analyzedFiles.isEmpty()
                        ? QStringLiteral("No analyzed build file")
                        : config.analyzedFiles.first(),
                    QStringLiteral("confirmed"), HealthSeverity::Info});
    }

    return report;
}

}  // namespace pkg::devflow
