// DevFlow workflow execution.
//
// Only the five supported, read-only steps can run. No shell command, script
// or build is executed by a workflow; the report is the only output written.
#pragma once

#include "core/CancelToken.h"
#include "devflow/DevFlowModels.h"

#include <QFutureWatcher>
#include <QObject>

#include <memory>

namespace pkg::devflow {

struct WorkflowResult {
    bool ok = false;
    QString error;
    QString reportPath;
    QStringList logs;
    int scannedFiles = 0;
    int dependencies = 0;
    int portabilityFindings = 0;
    QString detectedKind;
};

class WorkflowRunner : public QObject {
    Q_OBJECT

public:
    explicit WorkflowRunner(QObject* parent = nullptr);
    ~WorkflowRunner() override;

    /// Runs `workflow` against `projectPath`. A Markdown report, when the
    /// workflow includes the report step, is written into `reportDirectory`.
    void start(const WorkflowDefinition& workflow, const QString& projectPath,
               const QString& reportDirectory);
    void cancel();
    [[nodiscard]] bool isRunning() const { return m_running; }

signals:
    void stepStarted(const QString& step);
    void stepFinished(const QString& step, bool ok, const QString& detail);
    void logLine(const QString& line);
    void finished(bool ok, const QString& error, const QString& reportPath);

private:
    void onFinished();
    WorkflowResult run(const WorkflowDefinition& workflow, const QString& projectPath,
                       const QString& reportDirectory);

    CancelToken m_token;
    bool m_running = false;
    std::shared_ptr<QFutureWatcher<WorkflowResult>> m_watcher;
};

}  // namespace pkg::devflow
