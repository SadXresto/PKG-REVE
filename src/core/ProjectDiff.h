// Smart project diff: compare two folder states without touching either.
//
// Files are compared by size and a bounded SHA-256 sample; a file whose content
// was too large to hash completely is marked `contentSampled` rather than being
// reported as identical. Build configuration, declared dependencies and
// portability findings are compared separately and keep their evidence.
#pragma once

#include "core/CancelToken.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace pkg::projectdiff {

enum class ChangeKind { Added, Removed, Modified, Unchanged };

[[nodiscard]] QString changeKindName(ChangeKind kind);

struct FileChange {
    QString path; // relative, '/'-separated
    ChangeKind kind = ChangeKind::Unchanged;
    qint64 leftSize = 0;
    qint64 rightSize = 0;
    quint64 firstDifference = 0;
    bool binary = false;
    /// True when only a sample of the content could be hashed.
    bool contentSampled = false;
    /// True when the two sides hash equally but content was only sampled.
    bool uncertain = false;
    QString note;
};

struct ConfigChange {
    QString file;
    QString summary;
    QStringList sampleLines;
    ChangeKind kind = ChangeKind::Modified;
};

struct DependencyChange {
    QString name;
    QString leftVersion;
    QString rightVersion;
    /// "added", "removed" or "changed".
    QString change;
    QString leftEvidence;
    QString rightEvidence;
};

struct PortabilityDelta {
    QString label;
    QString area;
    int leftOccurrences = 0;
    int rightOccurrences = 0;
    QString change; // "added", "removed" or "changed"
};

struct DiffReport {
    QString leftRoot;
    QString rightRoot;
    QVector<FileChange> files;
    QVector<ConfigChange> configChanges;
    QVector<DependencyChange> dependencyChanges;
    QVector<PortabilityDelta> portabilityChanges;
    QStringList notes;
    int added = 0;
    int removed = 0;
    int modified = 0;
    int unchanged = 0;
    int uncertain = 0;
    qint64 comparedBytes = 0;
    bool truncated = false;
    bool cancelled = false;

    [[nodiscard]] int changedCount() const { return added + removed + modified; }
    [[nodiscard]] bool isEmpty() const { return files.isEmpty(); }
};

struct DiffLimits {
    int maxFiles = 20000;
    int maxConfigFiles = 80;
    int maxDependencyFiles = 60;
    int maxBinaryComparisons = 40;
    quint64 contentHashBytes = 4ULL * 1024ULL * 1024ULL;
    quint64 maxBinaryCompareBytes = 16ULL * 1024ULL * 1024ULL;
};

/// Compares the trees under `leftRoot` and `rightRoot`. Neither tree is
/// modified. Unreadable folders produce an empty report with a note.
[[nodiscard]] DiffReport compareProjects(const QString& leftRoot, const QString& rightRoot,
                                         const CancelToken& token = {},
                                         const DiffLimits& limits = {});

/// Markdown report for export, including the evidence behind every section.
[[nodiscard]] QString toMarkdown(const DiffReport& report);

}  // namespace pkg::projectdiff
