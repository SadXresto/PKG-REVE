// GameForge dependency graph built from parsed build files.
//
// Every node and edge remembers the file and line it came from. Edges that the
// build file states literally are `confirmed`; relationships that the scanner
// derives (the project root, link names without a package declaration) are
// `inferred` and drawn differently.
#pragma once

#include "core/gameforge/BuildConfig.h"
#include "core/gameforge/GameProject.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace pkg::gameforge {

enum class NodeKind { Project, Target, Dependency };

[[nodiscard]] QString nodeKindName(NodeKind kind);

struct GraphNode {
    QString id;
    QString label;
    NodeKind kind = NodeKind::Dependency;
    QString detail;
    QString file;
    int line = 0;
    bool confirmed = true;
};

struct GraphEdge {
    QString from;
    QString to;
    bool confirmed = true;
    QString evidence;
    QString file;
    int line = 0;
};

struct DependencyGraph {
    QVector<GraphNode> nodes;
    QVector<GraphEdge> edges;
    /// Dependencies referenced but never declared, as readable sentences.
    QStringList unresolved;
    /// "a -> b -> a" for each detected target cycle.
    QStringList cycles;
    QStringList warnings;
    bool truncated = false;

    [[nodiscard]] const GraphNode* find(const QString& id) const;
    [[nodiscard]] int indexOf(const QString& id) const;
    [[nodiscard]] int inferredEdgeCount() const;
};

struct GraphLimits {
    int maxNodes = 400;
    int maxEdges = 1200;
    int maxCycles = 20;
};

[[nodiscard]] DependencyGraph buildDependencyGraph(const BuildConfigReport& config,
                                                   const ProjectScan& scan,
                                                   const GraphLimits& limits = {});

} // namespace pkg::gameforge
