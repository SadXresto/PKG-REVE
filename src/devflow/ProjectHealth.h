// DevFlow project health: evidence-based checks, never a fabricated score.
#pragma once

#include "core/CancelToken.h"
#include "core/gameforge/BuildConfig.h"
#include "core/gameforge/DependencyGraph.h"
#include "core/gameforge/GameProject.h"

#include <QString>
#include <QVector>

namespace pkg::devflow {

enum class HealthSeverity { Info = 0, Warning = 1, Critical = 2 };

[[nodiscard]] QString healthSeverityName(HealthSeverity severity);

struct HealthFinding {
    QString id;
    QString title;
    QString detail;
    /// File, line or metric that supports the finding. Never empty.
    QString evidence;
    /// "confirmed" (observed directly) or "possible" (worth reviewing).
    QString confidence;
    HealthSeverity severity = HealthSeverity::Warning;
};

struct HealthReport {
    QVector<HealthFinding> findings;
    int checkedFiles = 0;
    int checkedTargets = 0;
    bool scanTruncated = false;
    bool cancelled = false;
    [[nodiscard]] int criticalCount() const;
    [[nodiscard]] int warningCount() const;
    /// True when every check that ran completed without findings. This is not
    /// a claim that the project is healthy.
    [[nodiscard]] bool noFindings() const { return findings.isEmpty(); }
};

/// Checks the structure, build configuration, dependency graph and file sizes.
/// Missing source files and unresolved dependencies carry the evidence line
/// that mentioned them.
[[nodiscard]] HealthReport checkProjectHealth(const QString& root,
                                              const gameforge::ProjectScan& scan,
                                              const gameforge::BuildConfigReport& config,
                                              const gameforge::DependencyGraph& graph,
                                              const gameforge::PortingReport& porting = {},
                                              const CancelToken& token = {});

}  // namespace pkg::devflow
