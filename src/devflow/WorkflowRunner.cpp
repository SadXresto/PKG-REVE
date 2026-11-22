#include "devflow/WorkflowRunner.h"

#include "core/AtomicFile.h"
#include "core/FormatUtil.h"
#include "core/gameforge/BuildConfig.h"
#include "core/gameforge/DependencyGraph.h"
#include "core/gameforge/GameProject.h"
#include "core/gameforge/PortingIntelligence.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QtConcurrent>

namespace pkg::devflow {
namespace {

QString reportMarkdown(const QString& projectPath, const gameforge::ProjectScan& scan,
                       const gameforge::BuildConfigReport& config,
                       const gameforge::DependencyGraph& graph,
                       const gameforge::PortingIntelligence& intel,
                       const gameforge::PortingReport& porting)
{
    QString markdown;
    markdown += QStringLiteral("# DevFlow report \\u2014 %1\n\n")
                    .arg(QFileInfo(projectPath).fileName());
    markdown += QStringLiteral("- Generated: %1\n")
                    .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    markdown += QStringLiteral("- Project: `%1`\n").arg(projectPath);
    markdown += QStringLiteral("- Detected type: %1")
                    .arg(gameforge::projectKindName(scan.kind));
    if (!scan.kindEvidence.isEmpty())
        markdown += QStringLiteral(" (evidence: `%1`)").arg(scan.kindEvidence);
    markdown += QStringLiteral("\n- Files scanned: %1 (%2)\n\n")
                    .arg(scan.fileCount())
                    .arg(fmt::humanBytes(scan.totalBytes));

    markdown += QStringLiteral("## Build configuration\n\n");
    if (config.empty()) {
        markdown += QStringLiteral("No supported build files were parsed.\n\n");
    } else {
        markdown += QStringLiteral("Analyzed files: %1\n\n")
                        .arg(config.analyzedFiles.join(QStringLiteral(", ")));
        if (!config.targets.isEmpty()) {
            markdown += QStringLiteral("| Target | Type | File |\n|---|---|---|\n");
            for (const gameforge::BuildTarget& target : config.targets) {
                markdown += QStringLiteral("| %1 | %2 | %3:%4 |\n")
                                .arg(target.name, target.type, target.file)
                                .arg(target.line);
            }
            markdown += QLatin1Char('\n');
        }
        if (config.truncated)
            markdown += QStringLiteral("The parser reached a limit; results are partial.\n\n");
    }

    markdown += QStringLiteral("## Dependencies\n\n");
    markdown += QStringLiteral("%1 node(s), %2 edge(s), %3 inferred, %4 unresolved.\n\n")
                    .arg(graph.nodes.size())
                    .arg(graph.edges.size())
                    .arg(graph.inferredEdgeCount())
                    .arg(graph.unresolved.size());
    for (const QString& unresolved : graph.unresolved)
        markdown += QStringLiteral("- Unresolved: %1\n").arg(unresolved);
    for (const QString& cycle : graph.cycles)
        markdown += QStringLiteral("- Cycle: %1\n").arg(cycle);

    markdown += QStringLiteral("\n## Portability\n\n");
    markdown += QStringLiteral("%1 signal(s) from %2 scanned file(s); %3 skipped.\n\n")
                    .arg(porting.findings.size())
                    .arg(porting.scannedFiles)
                    .arg(porting.skippedFiles);
    const auto appendInsights = [&markdown](const QString& title,
                                            const QVector<gameforge::IntelInsight>& insights) {
        if (insights.isEmpty())
            return;
        markdown += QStringLiteral("### %1\n\n").arg(title);
        for (const gameforge::IntelInsight& insight : insights) {
            markdown += QStringLiteral("- **%1** \\u2014 %2 (`%3:%4`)\n")
                            .arg(insight.title, insight.detail, insight.file)
                            .arg(insight.line);
        }
        markdown += QLatin1Char('\n');
    };
    appendInsights(QStringLiteral("Windows"), intel.windows);
    appendInsights(QStringLiteral("Linux"), intel.linuxPlatform);
    appendInsights(QStringLiteral("Architecture"), intel.architecture);
    appendInsights(QStringLiteral("Graphics and input"), intel.graphicsInput);
    appendInsights(QStringLiteral("External dependencies"), intel.externalDependencies);
    appendInsights(QStringLiteral("Recommendations"), intel.recommendations);

    markdown += QStringLiteral("## Limits\n\n");
    markdown += QStringLiteral(
        "This report is generated from static inspection. It does not build the project, "
        "does not evaluate build variables or conditions, and makes no claim about runtime "
        "behaviour.\n");
    return markdown;
}

}  // namespace

WorkflowRunner::WorkflowRunner(QObject* parent) : QObject(parent) {}
WorkflowRunner::~WorkflowRunner() = default;

void WorkflowRunner::start(const WorkflowDefinition& workflow, const QString& projectPath,
                           const QString& reportDirectory)
{
    if (m_running) {
        emit finished(false, QStringLiteral("A workflow is already running"), {});
        return;
    }
    const Status valid = workflow.validate();
    if (!valid) {
        emit finished(false,
                      valid.error().detail.isEmpty() ? valid.error().message
                                                     : valid.error().detail,
                      {});
        return;
    }
    if (!QFileInfo(projectPath).isDir()) {
        emit finished(false, QStringLiteral("Project folder does not exist"), {});
        return;
    }

    m_running = true;
    m_token = CancelToken();
    if (!m_watcher)
        m_watcher = std::make_shared<QFutureWatcher<WorkflowResult>>();
    connect(m_watcher.get(), &QFutureWatcher<WorkflowResult>::finished, this,
            &WorkflowRunner::onFinished, Qt::UniqueConnection);

    for (const QString& step : workflow.steps)
        emit stepStarted(step);

    m_watcher->setFuture(QtConcurrent::run([this, workflow, projectPath, reportDirectory]() {
        return run(workflow, projectPath, reportDirectory);
    }));
}

void WorkflowRunner::cancel()
{
    m_token.cancel();
    emit logLine(QStringLiteral("Cancellation requested; finishing the current step."));
}

WorkflowResult WorkflowRunner::run(const WorkflowDefinition& workflow, const QString& projectPath,
                                   const QString& reportDirectory)
{
    WorkflowResult result;
    const auto has = [&workflow](const QString& step) { return workflow.steps.contains(step); };
    const auto cancelled = [this]() { return m_token.isCancelled(); };

    emit logLine(QStringLiteral("Scanning %1").arg(projectPath));
    gameforge::ProjectScan scan = gameforge::scanProject(projectPath, m_token);
    result.scannedFiles = scan.fileCount();
    result.detectedKind = gameforge::projectKindName(scan.kind);

    gameforge::BuildConfigReport config;
    gameforge::DependencyGraph graph;
    gameforge::PortingReport porting;
    gameforge::PortingIntelligence intel;

    if (cancelled()) {
        result.error = QStringLiteral("Cancelled after the scan step");
        return result;
    }
    emit stepFinished(QStringLiteral("scan"), scan.readable(),
                      QStringLiteral("%1 file(s), type %2")
                          .arg(scan.fileCount())
                          .arg(result.detectedKind));

    if (has(QStringLiteral("detect"))) {
        if (!scan.readable()) {
            result.error = QStringLiteral("The project folder could not be read");
            return result;
        }
        emit stepFinished(QStringLiteral("detect"), true,
                          QStringLiteral("%1 via %2")
                              .arg(gameforge::projectKindName(scan.kind),
                                   scan.kindEvidence.isEmpty() ? QStringLiteral("file scan")
                                                               : scan.kindEvidence));
    }

    const bool needsConfig = has(QStringLiteral("dependencies")) ||
                             has(QStringLiteral("portability")) || has(QStringLiteral("report"));
    if (needsConfig) {
        emit logLine(QStringLiteral("Parsing build configuration"));
        config = gameforge::analyzeBuildConfig(scan, m_token);
        graph = gameforge::buildDependencyGraph(config, scan);
        result.dependencies = static_cast<int>(graph.nodes.size());
        if (cancelled()) {
            result.error = QStringLiteral("Cancelled while parsing build files");
            return result;
        }
        if (has(QStringLiteral("dependencies"))) {
            emit stepFinished(QStringLiteral("dependencies"), true,
                              QStringLiteral("%1 node(s), %2 edge(s), %3 unresolved")
                                  .arg(graph.nodes.size())
                                  .arg(graph.edges.size())
                                  .arg(graph.unresolved.size()));
        }
    }

    if (has(QStringLiteral("portability"))) {
        emit logLine(QStringLiteral("Analyzing portability signals"));
        porting = gameforge::analyzePortability(scan, m_token);
        intel = gameforge::analyzePortingIntelligence(config, graph, porting);
        result.portabilityFindings = static_cast<int>(porting.findings.size());
        if (cancelled()) {
            result.error = QStringLiteral("Cancelled while analysing portability");
            return result;
        }
        emit stepFinished(QStringLiteral("portability"), true,
                          QStringLiteral("%1 signal(s), %2 insight(s)")
                              .arg(porting.findings.size())
                              .arg(intel.total()));
    }

    if (has(QStringLiteral("report"))) {
        if (reportDirectory.trimmed().isEmpty()) {
            result.error = QStringLiteral("No report folder was chosen");
            return result;
        }
        if (!QDir().mkpath(reportDirectory)) {
            result.error = QStringLiteral("Cannot create the report folder");
            return result;
        }
        const QString stamp =
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        const QString name = QStringLiteral("%1-devflow-%2.md")
                                 .arg(QFileInfo(projectPath).fileName(), stamp);
        const QString path = QDir(reportDirectory).filePath(name);
        const QString markdown =
            reportMarkdown(projectPath, scan, config, graph, intel, porting);
        const Status written = atomic::writeFile(path, markdown.toUtf8());
        if (!written) {
            result.error = written.error().detail.isEmpty() ? written.error().message
                                                            : written.error().detail;
            return result;
        }
        result.reportPath = path;
        emit stepFinished(QStringLiteral("report"), true,
                          QStringLiteral("Written to %1").arg(path));
    }

    result.ok = true;
    return result;
}

void WorkflowRunner::onFinished()
{
    m_running = false;
    const WorkflowResult result = m_watcher->result();
    for (const QString& line : result.logs)
        emit logLine(line);
    if (!result.error.isEmpty() && result.ok == false)
        emit logLine(QStringLiteral("Stopped: %1").arg(result.error));
    emit finished(result.ok, result.error, result.reportPath);
}

}  // namespace pkg::devflow
