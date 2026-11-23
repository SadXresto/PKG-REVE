#include "core/projectnav/ProjectNavigator.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace pkg::projectnav {
namespace {

QString normalizedFile(QString file)
{
    file.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (file.startsWith(QStringLiteral("./")))
        file.remove(0, 2);
    return file;
}

QString fileKey(const QString& file)
{
    return QStringLiteral("file:") + normalizedFile(file);
}

int scoreEntity(const Entity& entity, const QStringList& terms)
{
    const QString label = entity.label.toLower();
    const QString detail = entity.detail.toLower();
    const QString file = entity.file.toLower();
    const QString kind = entityKindName(entity.kind).toLower();
    const QString haystack = label + QLatin1Char('\n') + detail + QLatin1Char('\n') + file
        + QLatin1Char('\n') + kind;
    int score = 0;
    for (const QString& term : terms) {
        if (term.isEmpty())
            continue;
        if (!haystack.contains(term))
            return -1;
        if (label.startsWith(term))
            score += 6;
        else if (label.contains(term))
            score += 3;
        else if (file.contains(term))
            score += 2;
        else
            score += 1;
    }
    return score;
}

struct Builder {
    Index index;
    const Limits limits;
    QSet<QString> entityIds;
    QSet<QString> relationKeys;
    int skippedEntities = 0;
    int skippedRelations = 0;

    explicit Builder(Limits value) : limits(value) {}

    void note(const QString& text) { index.notes.append(text); }

    bool addEntity(const Entity& entity)
    {
        if (entity.id.isEmpty() || entityIds.contains(entity.id))
            return false;
        if (index.entities.size() >= limits.maxEntities) {
            ++skippedEntities;
            index.truncated = true;
            return false;
        }
        entityIds.insert(entity.id);
        index.entities.append(entity);
        return true;
    }

    void addRelation(const Relation& relation)
    {
        if (relation.from.isEmpty() || relation.to.isEmpty() || relation.from == relation.to)
            return;
        const QString key = relation.from + QLatin1Char('\x1f') + relation.to + QLatin1Char('\x1f')
            + relation.label;
        if (relationKeys.contains(key))
            return;
        if (index.relations.size() >= limits.maxRelations) {
            ++skippedRelations;
            index.truncated = true;
            return;
        }
        relationKeys.insert(key);
        index.relations.append(relation);
    }
};

QString severityRank(const QString& severity)
{
    if (severity.compare(QStringLiteral("error"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("error");
    if (severity.compare(QStringLiteral("warning"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("warning");
    return QStringLiteral("note");
}

}  // namespace

QString entityKindName(EntityKind kind)
{
    switch (kind) {
    case EntityKind::File:
        return QStringLiteral("File");
    case EntityKind::Target:
        return QStringLiteral("Build target");
    case EntityKind::Dependency:
        return QStringLiteral("Dependency");
    case EntityKind::BuildFile:
        return QStringLiteral("Configuration file");
    case EntityKind::SourceReference:
        return QStringLiteral("Source reference");
    case EntityKind::Portability:
        return QStringLiteral("Portability finding");
    case EntityKind::BuildError:
        return QStringLiteral("Build error");
    }
    return QStringLiteral("Unknown");
}

QString entityKindId(EntityKind kind)
{
    switch (kind) {
    case EntityKind::File:
        return QStringLiteral("file");
    case EntityKind::Target:
        return QStringLiteral("target");
    case EntityKind::Dependency:
        return QStringLiteral("dependency");
    case EntityKind::BuildFile:
        return QStringLiteral("buildfile");
    case EntityKind::SourceReference:
        return QStringLiteral("source");
    case EntityKind::Portability:
        return QStringLiteral("portability");
    case EntityKind::BuildError:
        return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

QString confidenceName(Confidence confidence)
{
    switch (confidence) {
    case Confidence::Confirmed:
        return QStringLiteral("confirmed");
    case Confidence::Inferred:
        return QStringLiteral("inferred");
    case Confidence::Weak:
        return QStringLiteral("weak");
    }
    return QStringLiteral("unknown");
}

const Entity* Index::find(const QString& id) const
{
    for (const Entity& entity : entities) {
        if (entity.id == id)
            return &entity;
    }
    return nullptr;
}

int Index::indexOf(const QString& id) const
{
    for (int i = 0; i < entities.size(); ++i) {
        if (entities.at(i).id == id)
            return i;
    }
    return -1;
}

QVector<int> Index::search(const QString& query, int limit) const
{
    QVector<int> result;
    const QString trimmed = query.trimmed().toLower();
    if (trimmed.isEmpty())
        return result;
    const QStringList terms = trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QVector<QPair<int, int>> scored;  // score, index
    for (int i = 0; i < entities.size(); ++i) {
        const int score = scoreEntity(entities.at(i), terms);
        if (score >= 0)
            scored.append({score, i});
    }
    std::sort(scored.begin(), scored.end(), [this](const QPair<int, int>& a,
                                                   const QPair<int, int>& b) {
        if (a.first != b.first)
            return a.first > b.first;
        return entities.at(a.second).label < entities.at(b.second).label;
    });
    const int count = std::min(limit, static_cast<int>(scored.size()));
    result.reserve(count);
    for (int i = 0; i < count; ++i)
        result.append(scored.at(i).second);
    return result;
}

QVector<Relation> Index::relationsOf(const QString& id) const
{
    QVector<Relation> result;
    for (const Relation& relation : relations) {
        if (relation.from == id || relation.to == id)
            result.append(relation);
    }
    return result;
}

int Index::count(EntityKind kind) const
{
    int total = 0;
    for (const Entity& entity : entities) {
        if (entity.kind == kind)
            ++total;
    }
    return total;
}

Index buildIndex(const QString& root, const gameforge::ProjectScan& scan,
                 const gameforge::BuildConfigReport& config,
                 const gameforge::DependencyGraph& graph,
                 const gameforge::PortingReport& porting,
                 const QVector<ErrorInput>& errors, const CancelToken& token,
                 const Limits& limits)
{
    Builder builder(limits);
    builder.index.root = root;

    if (root.trimmed().isEmpty()) {
        builder.note(QStringLiteral("No project folder is open."));
        return builder.index;
    }
    if (scan.cancelled || config.cancelled || porting.cancelled) {
        builder.index.cancelled = true;
        builder.note(QStringLiteral("The scan was cancelled; the index is incomplete."));
        return builder.index;
    }
    if (!scan.readable()) {
        builder.note(QStringLiteral("The project folder could not be read."));
        return builder.index;
    }

    // 1. Files ------------------------------------------------------------
    int fileEntities = 0;
    for (const gameforge::ProjectFile& file : scan.files) {
        if ((fileEntities % 64) == 0 && token.isCancelled()) {
            builder.index.cancelled = true;
            builder.note(QStringLiteral("Indexing was cancelled."));
            return builder.index;
        }
        if (fileEntities >= limits.maxFiles) {
            builder.index.truncated = true;
            builder.note(QStringLiteral("Only the first %1 files were indexed.")
                             .arg(limits.maxFiles));
            break;
        }
        Entity entity;
        entity.id = fileKey(file.relativePath);
        entity.kind = EntityKind::File;
        entity.label = QFileInfo(file.relativePath).fileName();
        entity.detail = QStringLiteral("%1 \u00B7 %2 bytes \u00B7 %3")
                            .arg(gameforge::categoryName(file.category))
                            .arg(file.size)
                            .arg(file.relativePath);
        entity.file = normalizedFile(file.relativePath);
        entity.confidence = Confidence::Confirmed;
        entity.evidence = QStringLiteral("Project scan");
        if (builder.addEntity(entity))
            ++fileEntities;
    }
    builder.note(QStringLiteral("%1 file(s) indexed.").arg(fileEntities));

    // 2. Configuration files ---------------------------------------------
    QStringList configFiles = scan.buildFiles;
    for (const QString& file : config.analyzedFiles) {
        if (!configFiles.contains(file))
            configFiles.append(file);
    }
    for (const QString& file : configFiles) {
        Entity entity;
        entity.id = QStringLiteral("buildfile:") + normalizedFile(file);
        entity.kind = EntityKind::BuildFile;
        entity.label = QFileInfo(file).fileName();
        entity.detail = QStringLiteral("Build or engine configuration: %1").arg(file);
        entity.file = normalizedFile(file);
        entity.confidence = Confidence::Confirmed;
        entity.evidence = QStringLiteral("Detected build descriptor");
        builder.addEntity(entity);
    }

    // 3. Targets and their configuration --------------------------------
    QHash<QString, QString> targetIds;
    for (const gameforge::BuildTarget& target : config.targets) {
        if (target.name.trimmed().isEmpty())
            continue;
        Entity entity;
        entity.id = QStringLiteral("target:") + target.name;
        if (targetIds.contains(entity.id))
            continue;
        targetIds.insert(entity.id, target.file);
        entity.kind = EntityKind::Target;
        entity.label = target.name;
        entity.detail = QStringLiteral("%1 \u00B7 %2 source(s) \u00B7 %3 link(s)")
                            .arg(target.type)
                            .arg(target.sources.size())
                            .arg(target.links.size());
        entity.file = normalizedFile(target.file);
        entity.line = target.line;
        entity.confidence = Confidence::Confirmed;
        entity.evidence = QStringLiteral("%1:%2").arg(target.file).arg(target.line);
        builder.addEntity(entity);

        const QString buildFileId = QStringLiteral("buildfile:") + normalizedFile(target.file);
        if (builder.entityIds.contains(buildFileId)) {
            builder.addRelation(Relation{entity.id, buildFileId, QStringLiteral("declared in"),
                                         Confidence::Confirmed, entity.evidence, entity.file,
                                         entity.line});
        }
        for (const QString& source : target.sources) {
            const QString normalized = normalizedFile(source);
            const QString fileId = fileKey(normalized);
            if (builder.entityIds.contains(fileId)) {
                builder.addRelation(Relation{entity.id, fileId, QStringLiteral("builds"),
                                             Confidence::Confirmed,
                                             QStringLiteral("%1:%2 lists the source")
                                                 .arg(target.file)
                                                 .arg(target.line),
                                             target.file, target.line});
            } else {
                // The parser saw the name but the scanner did not index it
                // (generated path, variable, filtered suffix).
                builder.addRelation(Relation{entity.id, buildFileId,
                                             QStringLiteral("references source %1").arg(source),
                                             Confidence::Inferred,
                                             QStringLiteral("%1:%2 lists the source")
                                                 .arg(target.file)
                                                 .arg(target.line),
                                             target.file, target.line});
            }
        }
    }

    // 4. Dependencies ----------------------------------------------------
    QHash<QString, QString> dependencyLabels;
    for (const gameforge::DeclaredDependency& dependency : config.dependencies) {
        if (dependency.name.trimmed().isEmpty())
            continue;
        const QString id = QStringLiteral("dependency:") + dependency.name.toLower();
        dependencyLabels.insert(id, dependency.name);
        if (!builder.entityIds.contains(id)) {
            Entity entity;
            entity.id = id;
            entity.kind = EntityKind::Dependency;
            entity.label = dependency.name;
            entity.detail = dependency.version.isEmpty()
                ? QStringLiteral("Declared by %1").arg(dependency.declaredBy)
                : QStringLiteral("Version %1 \u00B7 declared by %2")
                      .arg(dependency.version, dependency.declaredBy);
            entity.file = normalizedFile(dependency.file);
            entity.line = dependency.line;
            entity.confidence = dependency.explicitDeclaration ? Confidence::Confirmed
                                                               : Confidence::Inferred;
            entity.evidence = QStringLiteral("%1:%2").arg(dependency.file).arg(dependency.line);
            builder.addEntity(entity);
        }
        const QString buildFileId = QStringLiteral("buildfile:") + normalizedFile(dependency.file);
        if (builder.entityIds.contains(buildFileId)) {
            builder.addRelation(Relation{id, buildFileId, QStringLiteral("declared in"),
                                         dependency.explicitDeclaration ? Confidence::Confirmed
                                                                        : Confidence::Inferred,
                                         QStringLiteral("%1:%2").arg(dependency.file)
                                             .arg(dependency.line),
                                         dependency.file, dependency.line});
        }
        // A target that links the library is related to the dependency.
        for (const gameforge::BuildTarget& target : config.targets) {
            if (!target.links.contains(dependency.name))
                continue;
            const QString targetId = QStringLiteral("target:") + target.name;
            if (!builder.entityIds.contains(targetId))
                continue;
            builder.addRelation(Relation{targetId, id, QStringLiteral("links"),
                                         Confidence::Confirmed,
                                         QStringLiteral("%1:%2 links %3")
                                             .arg(target.file)
                                             .arg(target.line)
                                             .arg(dependency.name),
                                         target.file, target.line});
        }
    }

    // 5. Dependency graph edges (confirm or add relationships) ----------
    for (const gameforge::GraphEdge& edge : graph.edges) {
        const QString fromId = builder.entityIds.contains(QStringLiteral("target:") + edge.from)
            ? QStringLiteral("target:") + edge.from
            : QStringLiteral("dependency:") + edge.from.toLower();
        const QString toId = builder.entityIds.contains(QStringLiteral("dependency:") + edge.to.toLower())
            ? QStringLiteral("dependency:") + edge.to.toLower()
            : QStringLiteral("target:") + edge.to;
        if (!builder.entityIds.contains(fromId) || !builder.entityIds.contains(toId))
            continue;
        builder.addRelation(Relation{fromId, toId,
                                     QStringLiteral("depends on"),
                                     edge.confirmed ? Confidence::Confirmed : Confidence::Inferred,
                                     edge.evidence.isEmpty()
                                         ? QStringLiteral("Dependency graph edge")
                                         : edge.evidence,
                                     edge.file, edge.line});
    }
    for (const QString& cycle : graph.cycles)
        builder.note(QStringLiteral("Target cycle: %1").arg(cycle));
    for (const QString& unresolved : graph.unresolved)
        builder.note(QStringLiteral("Unresolved link: %1").arg(unresolved));

    // 6. Portability findings and their source references ---------------
    for (const gameforge::PortingFinding& finding : porting.findings) {
        const QString portabilityId = QStringLiteral("portability:") + finding.id;
        Entity entity;
        entity.id = portabilityId;
        entity.kind = EntityKind::Portability;
        entity.label = finding.label;
        entity.detail = QStringLiteral("%1 area \u00B7 %2 occurrence(s) in %3 file(s)")
                            .arg(gameforge::signalAreaName(finding.area))
                            .arg(finding.occurrences)
                            .arg(finding.fileCount);
        entity.confidence = Confidence::Confirmed;
        entity.evidence = finding.example.isEmpty()
            ? QStringLiteral("Portability scan")
            : QStringLiteral("Example: %1").arg(finding.example);
        builder.addEntity(entity);

        if (!finding.example.isEmpty()) {
            const int separator = static_cast<int>(
                finding.example.lastIndexOf(QLatin1Char(':')));
            const QString exampleFile = separator > 0 ? finding.example.left(separator)
                                                      : finding.example;
            const int line = separator > 0
                ? finding.example.mid(separator + 1).toInt()
                : 0;
            const QString referenceId = QStringLiteral("source:") + finding.id;
            Entity reference;
            reference.id = referenceId;
            reference.kind = EntityKind::SourceReference;
            reference.label = exampleFile;
            reference.detail = QStringLiteral("First %1 reference, line %2")
                                   .arg(finding.label)
                                   .arg(line);
            reference.file = normalizedFile(exampleFile);
            reference.line = line;
            reference.confidence = Confidence::Confirmed;
            reference.evidence = QStringLiteral("%1:%2").arg(exampleFile).arg(line);
            builder.addEntity(reference);
            builder.addRelation(Relation{portabilityId, referenceId,
                                         QStringLiteral("example reference"),
                                         Confidence::Confirmed, reference.evidence,
                                         exampleFile, line});
            const QString fileId = fileKey(exampleFile);
            if (builder.entityIds.contains(fileId)) {
                builder.addRelation(Relation{referenceId, fileId, QStringLiteral("in file"),
                                             Confidence::Confirmed, reference.evidence,
                                             exampleFile, line});
            }
        }
    }

    // 7. Build errors ---------------------------------------------------
    QHash<QString, QString> errorIds;
    int errorCount = 0;
    for (const ErrorInput& error : errors) {
        if (errorCount >= limits.maxErrors) {
            builder.index.truncated = true;
            builder.note(QStringLiteral("Only the first %1 build errors were indexed.")
                             .arg(limits.maxErrors));
            break;
        }
        const QString fingerprint = error.fingerprint.isEmpty()
            ? QStringLiteral("%1|%2|%3").arg(error.severity, error.file, error.message)
            : error.fingerprint;
        const QString id = QStringLiteral("error:") + fingerprint;
        if (errorIds.contains(id)) {
            ++errorCount;
            continue;
        }
        errorIds.insert(id, error.message);
        Entity entity;
        entity.id = id;
        entity.kind = EntityKind::BuildError;
        entity.label = error.message.isEmpty() ? QStringLiteral("Build diagnostic")
                                               : error.message;
        entity.detail = QStringLiteral("%1 \u00B7 %2")
                            .arg(severityRank(error.severity),
                                 error.file.isEmpty() ? QStringLiteral("no file")
                                                      : error.file);
        entity.file = normalizedFile(error.file);
        entity.line = error.line;
        entity.confidence = Confidence::Confirmed;
        entity.evidence = error.file.isEmpty()
            ? QStringLiteral("Build output")
            : QStringLiteral("%1:%2").arg(error.file).arg(error.line);
        if (builder.addEntity(entity)) {
            ++errorCount;
            const QString fileId = fileKey(error.file);
            if (builder.entityIds.contains(fileId)) {
                builder.addRelation(Relation{id, fileId, QStringLiteral("reported in"),
                                             Confidence::Confirmed, entity.evidence,
                                             error.file, error.line});
            }
        }
    }

    // 8. Explicitly weak relationships: a portability finding whose label
    // mentions a declared dependency name is worth reviewing, but nothing in
    // the build files states that link.
    for (const gameforge::PortingFinding& finding : porting.findings) {
        const QString portabilityId = QStringLiteral("portability:") + finding.id;
        if (!builder.entityIds.contains(portabilityId))
            continue;
        const QString label = finding.label.toLower();
        for (auto it = dependencyLabels.constBegin(); it != dependencyLabels.constEnd(); ++it) {
            if (!builder.entityIds.contains(it.key()))
                continue;
            const QString dependency = it.value().toLower();
            if (dependency.size() < 3 || !label.contains(dependency))
                continue;
            builder.addRelation(Relation{portabilityId, it.key(),
                                         QStringLiteral("may involve"),
                                         Confidence::Weak,
                                         QStringLiteral("Name similarity only; not stated in a "
                                                        "build file"),
                                         {}, 0});
        }
    }

    if (config.truncated || porting.truncated || scan.truncated) {
        builder.index.truncated = true;
        builder.note(QStringLiteral("At least one source scan was truncated; the index reflects "
                                    "the files that were read."));
    }
    if (builder.skippedEntities > 0 || builder.skippedRelations > 0) {
        builder.note(QStringLiteral("Index limits reached: %1 entities and %2 relationships "
                                    "were omitted.")
                         .arg(builder.skippedEntities)
                         .arg(builder.skippedRelations));
    }
    builder.note(QStringLiteral("%1 entities, %2 relationships (%3 confirmed).")
                     .arg(builder.index.entities.size())
                     .arg(builder.index.relations.size())
                     .arg([&builder]() {
                         int confirmed = 0;
                         for (const Relation& relation : builder.index.relations) {
                             if (relation.confidence == Confidence::Confirmed)
                                 ++confirmed;
                         }
                         return confirmed;
                     }()));
    return builder.index;
}

}  // namespace pkg::projectnav
