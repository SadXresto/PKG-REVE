// Typed access to DevFlow's SQLite-backed documents.
#pragma once

#include "devflow/DevFlowModels.h"

#include <QList>
#include <QString>

namespace pkg {
class Database;
}

namespace pkg::devflow {

class DevFlowStore {
public:
    explicit DevFlowStore(Database& database);

    // Build profiles -------------------------------------------------------
    [[nodiscard]] QList<BuildProfile> profiles() const;
    [[nodiscard]] bool saveProfile(const BuildProfile& profile, QString* error = nullptr);
    void removeProfile(const QString& id);

    // Tasks ----------------------------------------------------------------
    [[nodiscard]] QList<ProjectTask> tasks(const QString& projectPath = {}) const;
    [[nodiscard]] bool saveTask(const ProjectTask& task, QString* error = nullptr);
    void removeTask(const QString& id);

    // Workspace snapshots --------------------------------------------------
    [[nodiscard]] QList<WorkspaceSnapshot> snapshots() const;
    [[nodiscard]] bool saveSnapshot(const WorkspaceSnapshot& snapshot, QString* error = nullptr);
    void removeSnapshot(const QString& id);

    // Workflows ------------------------------------------------------------
    [[nodiscard]] QList<WorkflowDefinition> workflows() const;
    [[nodiscard]] bool saveWorkflow(const WorkflowDefinition& workflow, QString* error = nullptr);
    void removeWorkflow(const QString& id);

    // Error resolution state ----------------------------------------------
    void setErrorResolved(const QString& projectPath, const QString& fingerprint, bool resolved);
    [[nodiscard]] bool isErrorResolved(const QString& projectPath,
                                       const QString& fingerprint) const;

    /// Number of stored documents of one kind (dashboard counts).
    [[nodiscard]] int count(const QString& kind) const;

private:
    Database& m_database;
};

}  // namespace pkg::devflow
