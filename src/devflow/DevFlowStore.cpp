#include "devflow/DevFlowStore.h"

#include "data/Database.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace pkg::devflow {
namespace {

constexpr auto kProfiles = "profile";
constexpr auto kTasks = "task";
constexpr auto kSnapshots = "snapshot";
constexpr auto kWorkflows = "workflow";
constexpr auto kErrorState = "error-state";

QString encode(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

}  // namespace

DevFlowStore::DevFlowStore(Database& database) : m_database(database) {}

QList<BuildProfile> DevFlowStore::profiles() const
{
    QList<BuildProfile> result;
    for (const auto& item : m_database.devflowItems(QString::fromLatin1(kProfiles))) {
        const QJsonDocument document = QJsonDocument::fromJson(item.second.toUtf8());
        if (!document.isObject())
            continue;
        auto profile = BuildProfile::fromJson(document.object());
        if (profile)
            result.append(profile.value());
    }
    return result;
}

bool DevFlowStore::saveProfile(const BuildProfile& profile, QString* error)
{
    const Status valid = profile.validate();
    if (!valid) {
        if (error != nullptr)
            *error = valid.error().detail.isEmpty() ? valid.error().message
                                                    : valid.error().detail;
        return false;
    }
    m_database.saveDevflowItem(QString::fromLatin1(kProfiles), profile.id, encode(profile.toJson()));
    return true;
}

void DevFlowStore::removeProfile(const QString& id)
{
    m_database.removeDevflowItem(QString::fromLatin1(kProfiles), id);
}

QList<ProjectTask> DevFlowStore::tasks(const QString& projectPath) const
{
    QList<ProjectTask> result;
    for (const auto& item : m_database.devflowItems(QString::fromLatin1(kTasks))) {
        const QJsonDocument document = QJsonDocument::fromJson(item.second.toUtf8());
        if (!document.isObject())
            continue;
        auto task = ProjectTask::fromJson(document.object());
        if (!task)
            continue;
        if (!projectPath.isEmpty() && task.value().projectPath != projectPath)
            continue;
        result.append(task.value());
    }
    return result;
}

bool DevFlowStore::saveTask(const ProjectTask& task, QString* error)
{
    const Status valid = task.validate();
    if (!valid) {
        if (error != nullptr)
            *error = valid.error().detail.isEmpty() ? valid.error().message
                                                    : valid.error().detail;
        return false;
    }
    m_database.saveDevflowItem(QString::fromLatin1(kTasks), task.id, encode(task.toJson()));
    return true;
}

void DevFlowStore::removeTask(const QString& id)
{
    m_database.removeDevflowItem(QString::fromLatin1(kTasks), id);
}

QList<WorkspaceSnapshot> DevFlowStore::snapshots() const
{
    QList<WorkspaceSnapshot> result;
    for (const auto& item : m_database.devflowItems(QString::fromLatin1(kSnapshots))) {
        const QJsonDocument document = QJsonDocument::fromJson(item.second.toUtf8());
        if (!document.isObject())
            continue;
        auto snapshot = WorkspaceSnapshot::fromJson(document.object());
        if (snapshot)
            result.append(snapshot.value());
    }
    return result;
}

bool DevFlowStore::saveSnapshot(const WorkspaceSnapshot& snapshot, QString* error)
{
    const Status valid = snapshot.validate();
    if (!valid) {
        if (error != nullptr)
            *error = valid.error().detail.isEmpty() ? valid.error().message
                                                    : valid.error().detail;
        return false;
    }
    m_database.saveDevflowItem(QString::fromLatin1(kSnapshots), snapshot.id,
                               encode(snapshot.toJson()));
    return true;
}

void DevFlowStore::removeSnapshot(const QString& id)
{
    m_database.removeDevflowItem(QString::fromLatin1(kSnapshots), id);
}

QList<WorkflowDefinition> DevFlowStore::workflows() const
{
    QList<WorkflowDefinition> result;
    for (const auto& item : m_database.devflowItems(QString::fromLatin1(kWorkflows))) {
        const QJsonDocument document = QJsonDocument::fromJson(item.second.toUtf8());
        if (!document.isObject())
            continue;
        auto workflow = WorkflowDefinition::fromJson(document.object());
        if (workflow)
            result.append(workflow.value());
    }
    return result;
}

bool DevFlowStore::saveWorkflow(const WorkflowDefinition& workflow, QString* error)
{
    const Status valid = workflow.validate();
    if (!valid) {
        if (error != nullptr)
            *error = valid.error().detail.isEmpty() ? valid.error().message
                                                    : valid.error().detail;
        return false;
    }
    m_database.saveDevflowItem(QString::fromLatin1(kWorkflows), workflow.id,
                               encode(workflow.toJson()));
    return true;
}

void DevFlowStore::removeWorkflow(const QString& id)
{
    m_database.removeDevflowItem(QString::fromLatin1(kWorkflows), id);
}

void DevFlowStore::setErrorResolved(const QString& projectPath, const QString& fingerprint,
                                    bool resolved)
{
    const QString id = projectPath + QLatin1Char('|') + fingerprint;
    if (resolved) {
        QJsonObject object;
        object.insert(QStringLiteral("resolved"), true);
        m_database.saveDevflowItem(QString::fromLatin1(kErrorState), id, encode(object));
    } else {
        m_database.removeDevflowItem(QString::fromLatin1(kErrorState), id);
    }
}

bool DevFlowStore::isErrorResolved(const QString& projectPath, const QString& fingerprint) const
{
    const QString id = projectPath + QLatin1Char('|') + fingerprint;
    for (const auto& item : m_database.devflowItems(QString::fromLatin1(kErrorState))) {
        if (item.first != id)
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(item.second.toUtf8());
        return document.object().value(QStringLiteral("resolved")).toBool();
    }
    return false;
}

int DevFlowStore::count(const QString& kind) const
{
    return static_cast<int>(m_database.devflowItems(kind).size());
}

}  // namespace pkg::devflow
