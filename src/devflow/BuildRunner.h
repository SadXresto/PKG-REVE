// DevFlow build profiles and real process execution.
//
// Commands are built from the detected build system and the installed tools.
// Nothing is executed through a shell, nothing is inferred silently, and the
// caller must show the command for confirmation before start() is called.
#pragma once

#include "devflow/DevFlowModels.h"

#include <QObject>
#include <QProcess>

class QTimer;

namespace pkg::devflow {

struct BuildCommand {
    QString program;
    QStringList arguments;
    QString workingDirectory;
    QMap<QString, QString> environment;
    QString display;
    [[nodiscard]] bool isValid() const { return !program.isEmpty(); }
};

class BuildRunner : public QObject {
    Q_OBJECT

public:
    explicit BuildRunner(QObject* parent = nullptr);
    ~BuildRunner() override;

    /// True when the tool needed by this build system is on PATH.
    [[nodiscard]] static bool toolAvailable(BuildSystem system);
    [[nodiscard]] static QString toolPath(BuildSystem system);
    /// Builds the exact command for a profile. `error` explains why a command
    /// cannot be produced (unsupported system, missing tool, invalid profile).
    [[nodiscard]] static BuildCommand commandFor(const BuildProfile& profile, QString* error = nullptr);

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] QString currentCommand() const { return m_display; }

    void start(const BuildProfile& profile);
    void cancel();

signals:
    void started(const QString& display);
    void outputReceived(const QString& text);
    void finished(int exitCode, QProcess::ExitStatus status, const QString& command);
    void failed(const QString& message);

private:
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onError(QProcess::ProcessError error);

    QProcess* m_process = nullptr;
    QTimer* m_killTimer = nullptr;
    QString m_display;
};

}  // namespace pkg::devflow
