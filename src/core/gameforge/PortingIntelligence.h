// Porting intelligence: evidence-backed Windows/Linux/architecture/API findings
// assembled from the parsed build files, the dependency graph and the existing
// portability analyzer. Every insight keeps its source file and line; nothing
// is reported without a match behind it.
#pragma once

#include "core/gameforge/BuildConfig.h"
#include "core/gameforge/DependencyGraph.h"
#include "core/gameforge/GameProject.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace pkg::gameforge {

struct IntelInsight {
    QString title;
    QString detail;
    QString file;
    int line = 0;
    QString evidence;
};

struct PortingIntelligence {
    QVector<IntelInsight> windows;
    QVector<IntelInsight> linuxPlatform;
    QVector<IntelInsight> architecture;
    QVector<IntelInsight> graphicsInput;
    QVector<IntelInsight> externalDependencies;
    QVector<IntelInsight> recommendations;
    QStringList notes;

    [[nodiscard]] bool empty() const;
    [[nodiscard]] int total() const;
};

[[nodiscard]] PortingIntelligence analyzePortingIntelligence(const BuildConfigReport& config,
                                                             const DependencyGraph& graph,
                                                             const PortingReport& portability);

} // namespace pkg::gameforge
