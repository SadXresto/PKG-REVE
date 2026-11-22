#include "devflow/BuildRunner.h"

#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>

namespace pkg::devflow {

BuildRunner::BuildRunner(QObject* parent) : QObject(parent) {}

BuildRunner::~BuildRunner()
{
    if (m_process != nullptr) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
}

bool BuildRunner::toolAvailable(BuildSystem system)
{
    return !toolPath(system).isEmpty();
}

QString BuildRunner::toolPath(BuildSystem system)
{
    switch (system) {
    case BuildSystem::CMake:
        return QStandardPaths::findExecutable(QStringLiteral("cmake"));
    case BuildSystem::Meson:
        return QStandardPaths::findExecutable(QStringLiteral("meson"));
    case BuildSystem::Makefile: {
        const QString make = QStandardPaths::findExecutable(QStringLiteral("make"));
        return !make.isEmpty() ? make
                               : QStandardPaths::findExecutable(QStringLiteral("mingw32-make"));
    }
    case BuildSystem::Unknown:
        break;
    }
    return {};
}

BuildCommand BuildRunner::commandFor(const BuildProfile& profile, QString* error)
{
    BuildCommand command;
    if (profile.validate()) {
        // validate() returns a Status; an invalid profile cannot be run.
    } else {
        if (error != nullptr) {
            *error = profile.validate().error().detail.isEmpty()
                ? profile.validate().error().message
                : profile.validate().error().detail;
        }
        return command;
    }

    const QString tool = toolPath(profile.buildSystem);
    if (tool.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("%1 is not installed or not on PATH")
                         .arg(buildSystemName(profile.buildSystem));
        }
        return command;
    }

    command.program = tool;
    command.workingDirectory = profile.projectPath;
    command.environment = profile.environment;

    const QString directory = profile.buildDirectory.trimmed().isEmpty()
        ? QStringLiteral("build")
        : profile.buildDirectory.trimmed();

    switch (profile.buildSystem) {
    case BuildSystem::CMake:
        command.arguments << QStringLiteral("--build") << directory;
        if (!profile.configuration.trimmed().isEmpty())
            command.arguments << QStringLiteral("--config") << profile.configuration;
        if (!profile.target.trimmed().isEmpty())
            command.arguments << QStringLiteral("--target") << profile.target;
        command.arguments << profile.arguments;
        break;
    case BuildSystem::Meson:
        command.arguments << QStringLiteral("compile") << QStringLiteral("-C") << directory;
        if (!profile.target.trimmed().isEmpty())
            command.arguments << profile.target;
        command.arguments << profile.arguments;
        break;
    case BuildSystem::Makefile: {
        command.arguments << QStringLiteral("-C") << directory;
        if (!profile.configuration.trimmed().isEmpty())
            command.arguments << QStringLiteral("BUILD=") + profile.configuration;
        if (!profile.target.trimmed().isEmpty())
            command.arguments << profile.target;
        command.arguments << profile.arguments;
        break;
    }
    case BuildSystem::Unknown:
        break;
    }

    if (!profile.compiler.trimmed().isEmpty() && profile.buildSystem != BuildSystem::CMake)
        command.environment.insert(QStringLiteral("CXX"), profile.compiler.trimmed());

    QStringList preview{command.program};
    for (const QString& argument : command.arguments)
        preview << (argument.contains(QLatin1Char(' '))
                        ? QStringLiteral("\"%1\"").arg(argument)
                        : argument);
    command.display = preview.join(QLatin1Char(' '));
    return command;
}

bool BuildRunner::isRunning() const
{
    return m_process != nullptr && m_process->state() != QProcess::NotRunning;
}

void BuildRunner::start(const BuildProfile& profile)
{
    if (isRunning()) {
        emit failed(QStringLiteral("A build is already running"));
        return;
    }
    QString error;
    const BuildCommand command = commandFor(profile, &error);
    if (!command.isValid()) {
        emit failed(error.isEmpty() ? QStringLiteral("Cannot build this profile") : error);
        return;
    }

    delete m_process;
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(command.workingDirectory);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (auto it = command.environment.constBegin(); it != command.environment.constEnd(); ++it)
        environment.insert(it.key(), it.value());
    m_process->setProcessEnvironment(environment);
    m_display = command.display;

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        const QByteArray data = m_process->readAllStandardOutput();
        if (!data.isEmpty())
            emit outputReceived(QString::fromLocal8Bit(data));
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &BuildRunner::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, &BuildRunner::onError);

    emit started(command.display);
    m_process->start(command.program, command.arguments);
}

void BuildRunner::cancel()
{
    if (!isRunning())
        return;
    emit outputReceived(QStringLiteral("\n[devflow] Cancelling the build\u2026\n"));
    m_process->terminate();
    if (m_killTimer == nullptr) {
        m_killTimer = new QTimer(this);
        m_killTimer->setSingleShot(true);
        connect(m_killTimer, &QTimer::timeout, this, [this]() {
            if (isRunning())
                m_process->kill();
        });
    }
    m_killTimer->start(2500);
}

void BuildRunner::onFinished(int exitCode, QProcess::ExitStatus status)
{
    const QByteArray remaining = m_process->readAllStandardOutput();
    if (!remaining.isEmpty())
        emit outputReceived(QString::fromLocal8Bit(remaining));
    emit finished(exitCode, status, m_display);
}

void BuildRunner::onError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        emit failed(QStringLiteral("The build tool could not be started: %1")
                        .arg(m_process->errorString()));
        return;
    }
    emit outputReceived(QStringLiteral("[devflow] process error: %1\n")
                            .arg(m_process->errorString()));
}

}  // namespace pkg::devflow
