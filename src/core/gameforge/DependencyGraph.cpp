#include "core/gameforge/DependencyGraph.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <functional>

namespace pkg::gameforge {
namespace {

QString dependencyKey(const QString& name)
{
    return name.trimmed().toLower();
}

bool looksLikeLinkArtifact(const QString& token)
{
    return token.startsWith(QLatin1Char('-')) || token.startsWith(QLatin1String("$<"))
        || token.startsWith(QLatin1String("${")) || token.contains(QLatin1Char('/'))
        || token.contains(QLatin1Char('\\'))
        || token.endsWith(QLatin1String(".lib"), Qt::CaseInsensitive)
        || token.endsWith(QLatin1String(".a"), Qt::CaseInsensitive)
        || token.endsWith(QLatin1String(".so"), Qt::CaseInsensitive)
        || token.endsWith(QLatin1String(".dll"), Qt::CaseInsensitive)
        || token.endsWith(QLatin1String(".framework"), Qt::CaseInsensitive);
}

struct LinkRef {
    QString target;
    QString file;
    int line = 0;
    QString evidence;
};

} // namespace

QString nodeKindName(NodeKind kind)
{
    switch (kind) {
    case NodeKind::Project:
        return QStringLiteral("Project");
    case NodeKind::Target:
        return QStringLiteral("Target");
    case NodeKind::Dependency:
        break;
    }
    return QStringLiteral("Dependency");
}

const GraphNode* DependencyGraph::find(const QString& id) const
{
    for (const GraphNode& node : nodes) {
        if (node.id == id)
            return &node;
    }
    return nullptr;
}

int DependencyGraph::indexOf(const QString& id) const
{
    for (int i = 0; i < nodes.size(); ++i) {
        if (nodes.at(i).id == id)
            return i;
    }
    return -1;
}

int DependencyGraph::inferredEdgeCount() const
{
    int count = 0;
    for (const GraphEdge& edge : edges) {
        if (!edge.confirmed)
            ++count;
    }
    return count;
}

DependencyGraph buildDependencyGraph(const BuildConfigReport& config,
                                     const ProjectScan& scan, const GraphLimits& limits)
{
    DependencyGraph graph;
    const QString projectLabel =
        !config.projectName.isEmpty() ? config.projectName : scan.displayName;

    GraphNode project;
    project.id = QStringLiteral("project");
    project.label = projectLabel.isEmpty() ? QStringLiteral("Project") : projectLabel;
    project.kind = NodeKind::Project;
    project.confirmed = true;
    project.detail = config.engineName.isEmpty()
        ? QStringLiteral("Root of the parsed build files.")
        : QStringLiteral("%1 %2 project root.")
              .arg(config.engineName, config.engineVersion);
    graph.nodes.append(project);

    // Explicit package declarations, keyed by normalised name.
    QHash<QString, DeclaredDependency> declared;
    for (const DeclaredDependency& dependency : config.dependencies) {
        if (!dependency.explicitDeclaration)
            continue;
        const QString key = dependencyKey(dependency.name);
        if (!declared.contains(key))
            declared.insert(key, dependency);
    }

    // Targets first so link entries can be resolved against them.
    QHash<QString, QString> targetIds;
    QSet<QString> seenTargets;
    for (const BuildTarget& target : config.targets) {
        const QString key = dependencyKey(target.name);
        if (key.isEmpty() || seenTargets.contains(key))
            continue;
        if (graph.nodes.size() >= limits.maxNodes) {
            graph.truncated = true;
            break;
        }
        seenTargets.insert(key);
        GraphNode node;
        node.id = QStringLiteral("target:") + key;
        node.label = target.name;
        node.kind = NodeKind::Target;
        node.file = target.file;
        node.line = target.line;
        node.confirmed = true;
        node.detail = QStringLiteral("%1 '%2' from %3:%4")
                          .arg(target.type, target.name, target.file)
                          .arg(target.line);
        if (target.sources.size() > 0) {
            node.detail += QStringLiteral(" \u00b7 %1 source entr%2")
                               .arg(target.sources.size())
                               .arg(target.sources.size() == 1 ? QStringLiteral("y")
                                                               : QStringLiteral("ies"));
        }
        if (!target.sources.isEmpty())
            node.detail += QStringLiteral(" \u00b7 first: %1").arg(target.sources.first());
        graph.nodes.append(node);
        targetIds.insert(key, node.id);

        GraphEdge edge;
        edge.from = project.id;
        edge.to = node.id;
        edge.confirmed = false; // the project root is a view, not a declared parent
        edge.file = target.file;
        edge.line = target.line;
        edge.evidence = QStringLiteral("Target declared at %1:%2; parent relation is a view "
                                       "root, not a build-file statement.")
                            .arg(target.file)
                            .arg(target.line);
        if (graph.edges.size() < limits.maxEdges)
            graph.edges.append(edge);
    }

    // Link entries, grouped per dependency so each pair appears once.
    QHash<QString, QString> dependencyLabels;
    QHash<QString, QVector<LinkRef>> links;
    int artifactCount = 0;
    for (const BuildTarget& target : config.targets) {
        for (const QString& link : target.links) {
            if (looksLikeLinkArtifact(link)) {
                ++artifactCount;
                continue;
            }
            const QString key = dependencyKey(link);
            if (key.isEmpty())
                continue;
            dependencyLabels.insert(key, link);
            links[key].append({target.name, target.file, target.line, link});
        }
    }
    // Dependencies declared without a link (find_package, plugins) also exist.
    for (auto it = declared.constBegin(); it != declared.constEnd(); ++it) {
        if (!dependencyLabels.contains(it.key()))
            dependencyLabels.insert(it.key(), it->name);
    }

    QStringList unresolvedNames;
    QSet<QString> seenDependencies;
    QVector<QString> orderedKeys;
    for (const BuildTarget& target : config.targets) {
        for (const QString& link : target.links) {
            const QString key = dependencyKey(link);
            if (key.isEmpty() || looksLikeLinkArtifact(link) || seenDependencies.contains(key))
                continue;
            seenDependencies.insert(key);
            orderedKeys.append(key);
        }
    }
    for (auto it = declared.constBegin(); it != declared.constEnd(); ++it) {
        if (seenDependencies.contains(it.key()))
            continue;
        seenDependencies.insert(it.key());
        orderedKeys.append(it.key());
    }

    // `SDL2::SDL2` names the package `SDL2`; resolve a namespaced link entry to
    // its package declaration so the edge is not reported as unresolved.
    const auto declaredFor = [&declared](const QString& key) {
        const auto exact = declared.constFind(key);
        if (exact != declared.constEnd())
            return exact;
        const int separator = static_cast<int>(key.indexOf(QLatin1String("::")));
        if (separator > 0)
            return declared.constFind(key.left(separator));
        return declared.constEnd();
    };
    const auto isProjectTarget = [&targetIds](const QString& key) {
        if (targetIds.contains(key))
            return true;
        const int separator = static_cast<int>(key.indexOf(QLatin1String("::")));
        return separator > 0 && targetIds.contains(key.left(separator));
    };

    for (const QString& key : orderedKeys) {
        const QString label = dependencyLabels.value(key);
        const bool isTarget = isProjectTarget(key);
        const auto declaration = declaredFor(key);
        const bool explicitlyDeclared = declaration != declared.constEnd();
        const QVector<LinkRef> references = links.value(key);

        if (graph.nodes.size() >= limits.maxNodes) {
            graph.truncated = true;
            break;
        }

        QString nodeId;
        if (isTarget) {
            nodeId = targetIds.value(key);
        } else {
            nodeId = QStringLiteral("dep:") + key;
            GraphNode node;
            node.id = nodeId;
            node.label = label;
            node.kind = NodeKind::Dependency;
            node.confirmed = explicitlyDeclared;
            if (explicitlyDeclared) {
                node.file = declaration->file;
                node.line = declaration->line;
                node.detail = QStringLiteral("Declared with %1%2 at %3:%4.")
                                  .arg(declaration->declaredBy,
                                       declaration->version.isEmpty()
                                           ? QString()
                                           : QStringLiteral(" (version %1)")
                                                 .arg(declaration->version),
                                       declaration->file)
                                  .arg(declaration->line);
            } else {
                node.detail =
                    QStringLiteral("Referenced only as a link name; no find_package/dependency() "
                                   "declaration was found in the inspected files.");
                if (!references.isEmpty()) {
                    node.file = references.first().file;
                    node.line = references.first().line;
                }
            }
            graph.nodes.append(node);
        }

        if (!isTarget && !explicitlyDeclared) {
            const LinkRef first = references.isEmpty() ? LinkRef{} : references.first();
            unresolvedNames.append(
                references.isEmpty()
                    ? label
                    : QStringLiteral("%1 \u2014 linked by '%2' at %3:%4, never declared.")
                          .arg(label, first.target, first.file)
                          .arg(first.line));
        }

        if (isTarget && !references.isEmpty() && references.first().target == label) {
            // A target that "links" itself would be a one-node cycle; skip it.
            continue;
        }

        if (references.isEmpty()) {
            if (explicitlyDeclared && graph.edges.size() < limits.maxEdges) {
                GraphEdge edge;
                edge.from = project.id;
                edge.to = nodeId;
                edge.confirmed = true;
                edge.file = declaration->file;
                edge.line = declaration->line;
                edge.evidence = QStringLiteral("Declared at %1:%2 (%3).")
                                    .arg(declaration->file)
                                    .arg(declaration->line)
                                    .arg(declaration->declaredBy);
                graph.edges.append(edge);
            }
            continue;
        }

        for (const LinkRef& reference : references) {
            if (graph.edges.size() >= limits.maxEdges) {
                graph.truncated = true;
                break;
            }
            const QString fromId = targetIds.value(dependencyKey(reference.target));
            if (fromId.isEmpty())
                continue;
            GraphEdge edge;
            edge.from = fromId;
            edge.to = nodeId;
            edge.confirmed = isTarget || explicitlyDeclared;
            edge.file = reference.file;
            edge.line = reference.line;
            edge.evidence = QStringLiteral("%1 -> %2 at %3:%4")
                                .arg(reference.target, label, reference.file)
                                .arg(reference.line);
            graph.edges.append(edge);
        }
    }

    graph.unresolved = unresolvedNames;
    std::sort(graph.unresolved.begin(), graph.unresolved.end(),
              [](const QString& left, const QString& right) {
                  return QString::compare(left, right, Qt::CaseInsensitive) < 0;
              });

    if (artifactCount > 0) {
        graph.warnings.append(
            QStringLiteral("%1 link entries were file paths or compiler flags and are not shown "
                           "as graph nodes.")
                .arg(artifactCount));
    }
    if (graph.truncated) {
        graph.warnings.append(
            QStringLiteral("The graph was limited to %1 nodes / %2 edges.")
                .arg(limits.maxNodes)
                .arg(limits.maxEdges));
    }

    // Cycle detection over target-to-target edges only; project edges are views.
    QHash<QString, QVector<QString>> adjacency;
    for (const GraphEdge& edge : graph.edges) {
        const GraphNode* from = graph.find(edge.from);
        const GraphNode* to = graph.find(edge.to);
        if (from == nullptr || to == nullptr)
            continue;
        if (from->kind != NodeKind::Target || to->kind != NodeKind::Target)
            continue;
        adjacency[from->id].append(to->id);
    }
    QHash<QString, int> color; // 0 unvisited, 1 in stack, 2 done
    QSet<QString> reported;
    QVector<QString> stack;
    std::function<void(const QString&)> visit = [&](const QString& id) {
        color.insert(id, 1);
        stack.append(id);
        const QVector<QString> next = adjacency.value(id);
        for (const QString& child : next) {
            if (graph.cycles.size() >= limits.maxCycles)
                break;
            if (color.value(child) == 0) {
                visit(child);
            } else if (color.value(child) == 1) {
                const int start = static_cast<int>(stack.indexOf(child));
                if (start < 0)
                    continue;
                QStringList path;
                for (int i = start; i < stack.size(); ++i)
                    path.append(graph.find(stack.at(i))->label);
                path.append(graph.find(child)->label);
                const QString full = path.join(QStringLiteral(" -> "));
                QStringList sorted = path.mid(0, path.size() - 1);
                std::sort(sorted.begin(), sorted.end());
                const QString key = sorted.join(QLatin1Char('|'));
                if (!reported.contains(key)) {
                    reported.insert(key);
                    graph.cycles.append(full);
                }
            }
        }
        stack.removeLast();
        color.insert(id, 2);
    };
    for (const GraphNode& node : graph.nodes) {
        if (node.kind == NodeKind::Target && color.value(node.id) == 0)
            visit(node.id);
    }

    if (graph.cycles.size() >= limits.maxCycles)
        graph.warnings.append(
            QStringLiteral("Cycle reporting stopped at %1 cycles.").arg(limits.maxCycles));

    return graph;
}

} // namespace pkg::gameforge
