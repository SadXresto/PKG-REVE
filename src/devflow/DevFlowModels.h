// DevFlow: persistent data models for the development workflow command center.
//
// Everything is serialisable so it can live in the local SQLite database, and
// so profiles, tasks and workflows can be imported and exported safely.
#pragma once

#include "core/Result.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace pkg::devflow {

/// Build systems DevFlow can actually drive. `Unknown` is never offered as a
/// runnable profile.
enum class BuildSystem { Unknown, CMake, Meson, Makefile };

[[nodiscard]] QString buildSystemName(BuildSystem system);
[[nodiscard]] BuildSystem buildSystemFromName(const QString& name);
[[nodiscard]] QVector<BuildSystem> supportedBuildSystems();

struct BuildProfile {
    QString id;
    QString name;
    QString projectPath;
    BuildSystem buildSystem = BuildSystem::Unknown;
    QString buildDirectory;
    QString configuration;
    QString target;
    QString compiler;
    QStringList arguments;
    QMap<QString, QString> environment;
    QDateTime createdAt;

    /// Validates the fields needed to run the profile. Errors name the field.
    [[nodiscard]] Status validate() const;
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static Result<BuildProfile> fromJson(const QJsonObject& object);
};

enum class TaskPriority { Low = 0, Normal = 1, High = 2, Urgent = 3 };
enum class TaskStatus { ToDo = 0, InProgress = 1, Done = 2 };

[[nodiscard]] QString taskPriorityName(TaskPriority priority);
[[nodiscard]] QString taskStatusName(TaskStatus status);
[[nodiscard]] TaskPriority taskPriorityFromName(const QString& name, TaskPriority fallback);
[[nodiscard]] TaskStatus taskStatusFromName(const QString& name, TaskStatus fallback);

struct ProjectTask {
    QString id;
    QString projectPath;
    QString title;
    QString description;
    TaskPriority priority = TaskPriority::Normal;
    TaskStatus status = TaskStatus::ToDo;
    QString linkedFile;
    /// Error fingerprint from Error Intelligence, empty when unlinked.
    QString linkedError;
    QDateTime createdAt;
    QDateTime updatedAt;

    [[nodiscard]] Status validate() const;
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static Result<ProjectTask> fromJson(const QJsonObject& object);
};

struct WorkspaceSnapshot {
    QString id;
    QString name;
    QString projectPath;
    QStringList openFiles;
    int activeIndex = 0;
    QString sidebarId;
    QList<int> splitterSizes;
    QMap<QString, quint64> cursorOffsets;
    QDateTime createdAt;

    [[nodiscard]] Status validate() const;
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static Result<WorkspaceSnapshot> fromJson(const QJsonObject& object);
    /// A one-line summary for lists; never invents tab names.
    [[nodiscard]] QString summary() const;
};

struct WorkflowDefinition {
    QString id;
    QString name;
    QString projectPath;
    /// Step ids from supportedSteps(). Unknown ids are rejected.
    QStringList steps;
    QDateTime createdAt;

    [[nodiscard]] static QStringList supportedSteps();
    [[nodiscard]] static QString stepDescription(const QString& step);
    [[nodiscard]] Status validate() const;
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static Result<WorkflowDefinition> fromJson(const QJsonObject& object);
};

struct QuickAction {
    QString id;
    QString label;
    QString description;
};

/// Actions that are genuinely supported for this path. Binary-only actions are
/// only offered for regular files; bookmark actions only when the file is open
/// in a hex editor.
[[nodiscard]] QVector<QuickAction> quickActionsForFile(const QString& path, bool openInHexEditor);

/// RFC 6234 style identifier for a task or profile; time based with a counter so
/// two objects created in the same millisecond stay distinct.
[[nodiscard]] QString makeId(const QString& prefix);

}  // namespace pkg::devflow
