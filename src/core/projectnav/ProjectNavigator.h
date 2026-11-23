// Project Navigator: one evidence-backed index over a project's files,
// build targets, dependencies, configuration files, source references,
// portability findings and build errors.
//
// Nothing in the index is invented: every entity keeps the file and line it
// came from, and every relationship states whether it was read literally from a
// build file (`Confirmed`), derived from a scan (`Inferred`) or is only a
// similarity worth reviewing (`Weak`). The index is read-only.
#pragma once

#include "core/CancelToken.h"
#include "core/gameforge/BuildConfig.h"
#include "core/gameforge/DependencyGraph.h"
#include "core/gameforge/GameProject.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace pkg::projectnav {

enum class EntityKind {
    File,
    Target,
    Dependency,
    BuildFile,
    SourceReference,
    Portability,
    BuildError,
};

[[nodiscard]] QString entityKindName(EntityKind kind);
[[nodiscard]] QString entityKindId(EntityKind kind);

enum class Confidence { Confirmed, Inferred, Weak };

[[nodiscard]] QString confidenceName(Confidence confidence);

/// One navigable thing. `file` is relative to the project root when it came
/// from the scanner, otherwise the path the build parser reported.
struct Entity {
    QString id;
    EntityKind kind = EntityKind::File;
    QString label;
    QString detail;
    QString file;
    int line = 0;
    Confidence confidence = Confidence::Confirmed;
    QString evidence;
};

struct Relation {
    QString from;
    QString to;
    QString label;
    Confidence confidence = Confidence::Confirmed;
    QString evidence;
    QString file;
    int line = 0;
};

/// A build diagnostic reduced to what the navigator needs, so `pkgstudio_core`
/// does not depend on the DevFlow layer.
struct ErrorInput {
    QString fingerprint;
    QString file;
    int line = 0;
    QString severity;
    QString message;
};

struct Limits {
    int maxFiles = 2000;
    int maxEntities = 4000;
    int maxRelations = 8000;
    int maxErrors = 400;
};

struct Index {
    QString root;
    QVector<Entity> entities;
    QVector<Relation> relations;
    QStringList notes;
    bool truncated = false;
    bool cancelled = false;

    [[nodiscard]] const Entity* find(const QString& id) const;
    [[nodiscard]] int indexOf(const QString& id) const;
    /// Indices of entities matching all whitespace-separated terms, best first.
    [[nodiscard]] QVector<int> search(const QString& query, int limit = 60) const;
    /// Relationships touching `id`, in stored order.
    [[nodiscard]] QVector<Relation> relationsOf(const QString& id) const;
    [[nodiscard]] int count(EntityKind kind) const;
    [[nodiscard]] bool isEmpty() const { return entities.isEmpty(); }
};

/// Builds the index. Files and source references are bounded; results carry a
/// note when a limit was reached.
[[nodiscard]] Index buildIndex(const QString& root,
                               const gameforge::ProjectScan& scan,
                               const gameforge::BuildConfigReport& config,
                               const gameforge::DependencyGraph& graph,
                               const gameforge::PortingReport& porting,
                               const QVector<ErrorInput>& errors,
                               const CancelToken& token = {},
                               const Limits& limits = {});

}  // namespace pkg::projectnav
