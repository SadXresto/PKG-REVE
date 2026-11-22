#include "devflow/DevFlowModels.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>
#include <QUuid>

namespace pkg::devflow {
namespace {

QString jsonString(const QJsonObject& object, const char* key)
{
    return object.value(QLatin1String(key)).toString();
}

QStringList jsonStrings(const QJsonObject& object, const char* key)
{
    QStringList values;
    for (const QJsonValue& value : object.value(QLatin1String(key)).toArray()) {
        if (value.isString())
            values.append(value.toString());
    }
    return values;
}

}  // namespace

QString buildSystemName(BuildSystem system)
{
    switch (system) {
    case BuildSystem::CMake:
        return QStringLiteral("CMake");
    case BuildSystem::Meson:
        return QStringLiteral("Meson");
    case BuildSystem::Makefile:
        return QStringLiteral("Makefile");
    case BuildSystem::Unknown:
        break;
    }
    return QStringLiteral("Unknown");
}

BuildSystem buildSystemFromName(const QString& name)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QLatin1String("cmake"))
        return BuildSystem::CMake;
    if (normalized == QLatin1String("meson"))
        return BuildSystem::Meson;
    if (normalized == QLatin1String("makefile") || normalized == QLatin1String("make"))
        return BuildSystem::Makefile;
    return BuildSystem::Unknown;
}

QVector<BuildSystem> supportedBuildSystems()
{
    return {BuildSystem::CMake, BuildSystem::Meson, BuildSystem::Makefile};
}

Status BuildProfile::validate() const
{
    if (name.trimmed().isEmpty())
        return makeError(QStringLiteral("Profile name is required"));
    if (projectPath.trimmed().isEmpty())
        return makeError(QStringLiteral("Project folder is required"));
    if (!QFileInfo(projectPath).isDir())
        return makeError(QStringLiteral("Project folder does not exist"), projectPath);
    if (buildSystem == BuildSystem::Unknown)
        return makeError(QStringLiteral("Choose a supported build system"));
    for (auto it = environment.constBegin(); it != environment.constEnd(); ++it) {
        if (it.key().trimmed().isEmpty())
            return makeError(QStringLiteral("Environment entries need a name"));
    }
    return okStatus();
}

QJsonObject BuildProfile::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("projectPath"), projectPath);
    object.insert(QStringLiteral("buildSystem"), buildSystemName(buildSystem));
    object.insert(QStringLiteral("buildDirectory"), buildDirectory);
    object.insert(QStringLiteral("configuration"), configuration);
    object.insert(QStringLiteral("target"), target);
    object.insert(QStringLiteral("compiler"), compiler);
    object.insert(QStringLiteral("arguments"), QJsonArray::fromStringList(arguments));
    QJsonObject env;
    for (auto it = environment.constBegin(); it != environment.constEnd(); ++it)
        env.insert(it.key(), it.value());
    object.insert(QStringLiteral("environment"), env);
    object.insert(QStringLiteral("createdAt"), createdAt.toString(Qt::ISODate));
    return object;
}

Result<BuildProfile> BuildProfile::fromJson(const QJsonObject& object)
{
    BuildProfile profile;
    profile.id = jsonString(object, "id");
    profile.name = jsonString(object, "name").trimmed();
    profile.projectPath = jsonString(object, "projectPath");
    profile.buildSystem = buildSystemFromName(jsonString(object, "buildSystem"));
    profile.buildDirectory = jsonString(object, "buildDirectory");
    profile.configuration = jsonString(object, "configuration");
    profile.target = jsonString(object, "target");
    profile.compiler = jsonString(object, "compiler");
    profile.arguments = jsonStrings(object, "arguments");
    const QJsonObject env = object.value(QStringLiteral("environment")).toObject();
    for (auto it = env.begin(); it != env.end(); ++it) {
        if (it.value().isString())
            profile.environment.insert(it.key(), it.value().toString());
    }
    profile.createdAt = QDateTime::fromString(jsonString(object, "createdAt"), Qt::ISODate);
    if (profile.id.isEmpty() || profile.name.isEmpty())
        return makeError(QStringLiteral("Profile JSON is missing its id or name"));
    if (profile.buildSystem == BuildSystem::Unknown)
        return makeError(QStringLiteral("Profile JSON has an unsupported build system"));
    return profile;
}

QString taskPriorityName(TaskPriority priority)
{
    switch (priority) {
    case TaskPriority::Low:
        return QStringLiteral("Low");
    case TaskPriority::Normal:
        return QStringLiteral("Normal");
    case TaskPriority::High:
        return QStringLiteral("High");
    case TaskPriority::Urgent:
        return QStringLiteral("Urgent");
    }
    return QStringLiteral("Normal");
}

QString taskStatusName(TaskStatus status)
{
    switch (status) {
    case TaskStatus::ToDo:
        return QStringLiteral("To do");
    case TaskStatus::InProgress:
        return QStringLiteral("In progress");
    case TaskStatus::Done:
        return QStringLiteral("Done");
    }
    return QStringLiteral("To do");
}

TaskPriority taskPriorityFromName(const QString& name, TaskPriority fallback)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QLatin1String("low"))
        return TaskPriority::Low;
    if (normalized == QLatin1String("normal"))
        return TaskPriority::Normal;
    if (normalized == QLatin1String("high"))
        return TaskPriority::High;
    if (normalized == QLatin1String("urgent"))
        return TaskPriority::Urgent;
    return fallback;
}

TaskStatus taskStatusFromName(const QString& name, TaskStatus fallback)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QLatin1String("to do") || normalized == QLatin1String("todo"))
        return TaskStatus::ToDo;
    if (normalized == QLatin1String("in progress") || normalized == QLatin1String("progress"))
        return TaskStatus::InProgress;
    if (normalized == QLatin1String("done"))
        return TaskStatus::Done;
    return fallback;
}

Status ProjectTask::validate() const
{
    if (title.trimmed().isEmpty())
        return makeError(QStringLiteral("Task title is required"));
    if (title.size() > 200)
        return makeError(QStringLiteral("Task title is limited to 200 characters"));
    if (description.size() > 4000)
        return makeError(QStringLiteral("Task description is limited to 4000 characters"));
    return okStatus();
}

QJsonObject ProjectTask::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("projectPath"), projectPath);
    object.insert(QStringLiteral("title"), title);
    object.insert(QStringLiteral("description"), description);
    object.insert(QStringLiteral("priority"), taskPriorityName(priority));
    object.insert(QStringLiteral("status"), taskStatusName(status));
    object.insert(QStringLiteral("linkedFile"), linkedFile);
    object.insert(QStringLiteral("linkedError"), linkedError);
    object.insert(QStringLiteral("createdAt"), createdAt.toString(Qt::ISODate));
    object.insert(QStringLiteral("updatedAt"), updatedAt.toString(Qt::ISODate));
    return object;
}

Result<ProjectTask> ProjectTask::fromJson(const QJsonObject& object)
{
    ProjectTask task;
    task.id = jsonString(object, "id");
    task.projectPath = jsonString(object, "projectPath");
    task.title = jsonString(object, "title");
    task.description = jsonString(object, "description");
    task.priority = taskPriorityFromName(jsonString(object, "priority"), TaskPriority::Normal);
    task.status = taskStatusFromName(jsonString(object, "status"), TaskStatus::ToDo);
    task.linkedFile = jsonString(object, "linkedFile");
    task.linkedError = jsonString(object, "linkedError");
    task.createdAt = QDateTime::fromString(jsonString(object, "createdAt"), Qt::ISODate);
    task.updatedAt = QDateTime::fromString(jsonString(object, "updatedAt"), Qt::ISODate);
    if (task.id.isEmpty() || task.title.trimmed().isEmpty())
        return makeError(QStringLiteral("Task JSON is missing its id or title"));
    return task;
}

Status WorkspaceSnapshot::validate() const
{
    if (name.trimmed().isEmpty())
        return makeError(QStringLiteral("Snapshot name is required"));
    if (openFiles.size() > 200)
        return makeError(QStringLiteral("A snapshot can hold at most 200 files"));
    return okStatus();
}

QJsonObject WorkspaceSnapshot::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("projectPath"), projectPath);
    object.insert(QStringLiteral("openFiles"), QJsonArray::fromStringList(openFiles));
    object.insert(QStringLiteral("activeIndex"), activeIndex);
    object.insert(QStringLiteral("sidebarId"), sidebarId);
    QJsonArray sizes;
    for (const int size : splitterSizes)
        sizes.append(size);
    object.insert(QStringLiteral("splitterSizes"), sizes);
    QJsonObject cursors;
    for (auto it = cursorOffsets.constBegin(); it != cursorOffsets.constEnd(); ++it)
        cursors.insert(it.key(), static_cast<double>(it.value()));
    object.insert(QStringLiteral("cursorOffsets"), cursors);
    object.insert(QStringLiteral("createdAt"), createdAt.toString(Qt::ISODate));
    return object;
}

Result<WorkspaceSnapshot> WorkspaceSnapshot::fromJson(const QJsonObject& object)
{
    WorkspaceSnapshot snapshot;
    snapshot.id = jsonString(object, "id");
    snapshot.name = jsonString(object, "name");
    snapshot.projectPath = jsonString(object, "projectPath");
    snapshot.openFiles = jsonStrings(object, "openFiles");
    snapshot.activeIndex = object.value(QStringLiteral("activeIndex")).toInt();
    snapshot.sidebarId = jsonString(object, "sidebarId");
    for (const QJsonValue& size : object.value(QStringLiteral("splitterSizes")).toArray()) {
        if (size.isDouble())
            snapshot.splitterSizes.append(size.toInt());
    }
    const QJsonObject cursors = object.value(QStringLiteral("cursorOffsets")).toObject();
    for (auto it = cursors.begin(); it != cursors.end(); ++it) {
        if (it.value().isDouble())
            snapshot.cursorOffsets.insert(it.key(),
                                          static_cast<quint64>(it.value().toDouble()));
    }
    snapshot.createdAt = QDateTime::fromString(jsonString(object, "createdAt"), Qt::ISODate);
    if (snapshot.id.isEmpty())
        return makeError(QStringLiteral("Snapshot JSON is missing its id"));
    return snapshot;
}

QString WorkspaceSnapshot::summary() const
{
    QString result = QStringLiteral("%1 tab(s)").arg(openFiles.size());
    if (!projectPath.isEmpty())
        result += QStringLiteral(" \\u00B7 %1").arg(QFileInfo(projectPath).fileName());
    return result;
}

QStringList WorkflowDefinition::supportedSteps()
{
    return {QStringLiteral("scan"), QStringLiteral("detect"),
            QStringLiteral("dependencies"), QStringLiteral("portability"),
            QStringLiteral("report")};
}

QString WorkflowDefinition::stepDescription(const QString& step)
{
    if (step == QLatin1String("scan"))
        return QStringLiteral("Scan the project folder for supported files");
    if (step == QLatin1String("detect"))
        return QStringLiteral("Detect the project type and build files");
    if (step == QLatin1String("dependencies"))
        return QStringLiteral("Parse dependencies and build configuration");
    if (step == QLatin1String("portability"))
        return QStringLiteral("Analyse portability signals and platform requirements");
    if (step == QLatin1String("report"))
        return QStringLiteral("Generate a Markdown report next to the project");
    return QStringLiteral("Unknown step");
}

Status WorkflowDefinition::validate() const
{
    if (name.trimmed().isEmpty())
        return makeError(QStringLiteral("Workflow name is required"));
    if (steps.isEmpty())
        return makeError(QStringLiteral("Select at least one step"));
    const QStringList supported = supportedSteps();
    for (const QString& step : steps) {
        if (!supported.contains(step))
            return makeError(QStringLiteral("Unsupported workflow step"), step);
    }
    if (steps.contains(QStringLiteral("report")) &&
        !steps.contains(QStringLiteral("scan"))) {
        return makeError(QStringLiteral("A report needs the scan step before it"));
    }
    return okStatus();
}

QJsonObject WorkflowDefinition::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("projectPath"), projectPath);
    object.insert(QStringLiteral("steps"), QJsonArray::fromStringList(steps));
    object.insert(QStringLiteral("createdAt"), createdAt.toString(Qt::ISODate));
    return object;
}

Result<WorkflowDefinition> WorkflowDefinition::fromJson(const QJsonObject& object)
{
    WorkflowDefinition workflow;
    workflow.id = jsonString(object, "id");
    workflow.name = jsonString(object, "name");
    workflow.projectPath = jsonString(object, "projectPath");
    workflow.steps = jsonStrings(object, "steps");
    workflow.createdAt = QDateTime::fromString(jsonString(object, "createdAt"), Qt::ISODate);
    if (workflow.id.isEmpty())
        return makeError(QStringLiteral("Workflow JSON is missing its id"));
    const Status valid = workflow.validate();
    if (!valid)
        return valid.error();
    return workflow;
}

QVector<QuickAction> quickActionsForFile(const QString& path, bool openInHexEditor)
{
    QVector<QuickAction> actions;
    const QFileInfo info(path);
    if (!info.isFile() && !info.isDir())
        return actions;

    if (info.isFile()) {
        actions.append({QStringLiteral("hex.open"), QStringLiteral("Open in Hex Editor"),
                        QStringLiteral("Inspect and edit bytes in memory")});
        actions.append({QStringLiteral("metadata"), QStringLiteral("Inspect metadata"),
                        QStringLiteral("Run the matching file parser")});
        actions.append({QStringLiteral("hash"), QStringLiteral("Calculate SHA-256"),
                        QStringLiteral("Hash the file on a background worker")});
        actions.append({QStringLiteral("compare"), QStringLiteral("Compare with another file…"),
                        QStringLiteral("Bounded binary comparison")});
        if (openInHexEditor) {
            actions.append({QStringLiteral("bookmark"), QStringLiteral("Add bookmark"),
                            QStringLiteral("Bookmark the current offset")});
        }
    }
    actions.append({QStringLiteral("folder"), QStringLiteral("Open containing folder"),
                    QStringLiteral("Show the file in the system file manager")});
    actions.append({QStringLiteral("copyPath"), QStringLiteral("Copy file path"),
                    QStringLiteral("Copy the absolute path to the clipboard")});
    actions.append({QStringLiteral("properties"), QStringLiteral("View file properties"),
                    QStringLiteral("Size, timestamps, permissions and path")});
    return actions;
}

QString makeId(const QString& prefix)
{
    static quint64 counter = 0;
    ++counter;
    return QStringLiteral("%1-%2-%3")
        .arg(prefix, QString::number(QDateTime::currentMSecsSinceEpoch()),
             QString::number(counter));
}

}  // namespace pkg::devflow
