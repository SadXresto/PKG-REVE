#include "core/ProjectDiff.h"

#include "core/BinaryCompare.h"
#include "core/gameforge/BuildConfig.h"
#include "core/gameforge/GameProject.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace pkg::projectdiff {
namespace {

const QStringList& skippedDirectoryNames()
{
    static const QStringList names = {
        QStringLiteral(".git"),  QStringLiteral(".svn"),   QStringLiteral(".hg"),
        QStringLiteral(".vs"),   QStringLiteral(".idea"),  QStringLiteral("build"),
        QStringLiteral("out"),   QStringLiteral("dist"),   QStringLiteral("target"),
        QStringLiteral("node_modules"), QStringLiteral(".cache"),
    };
    return names;
}

bool isConfigFileName(const QString& name)
{
    const QString lowered = name.toLower();
    return lowered == QLatin1String("cmakelists.txt") || lowered.endsWith(QLatin1String(".cmake"))
        || lowered == QLatin1String("meson.build") || lowered == QLatin1String("meson_options.txt")
        || lowered == QLatin1String("makefile") || lowered == QLatin1String("gnumakefile")
        || lowered == QLatin1String("cmakepresets.json") || lowered == QLatin1String("project.godot")
        || lowered.endsWith(QLatin1String(".uproject"));
}

bool looksBinary(const QString& path)
{
    static const QStringList suffixes = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("tga"),
        QStringLiteral("dds"), QStringLiteral("ktx"), QStringLiteral("wav"),
        QStringLiteral("mp3"), QStringLiteral("ogg"), QStringLiteral("flac"),
        QStringLiteral("zip"), QStringLiteral("7z"),  QStringLiteral("gz"),
        QStringLiteral("exe"), QStringLiteral("dll"), QStringLiteral("so"),
        QStringLiteral("dylib"), QStringLiteral("a"), QStringLiteral("lib"),
        QStringLiteral("o"),   QStringLiteral("obj"), QStringLiteral("pdb"),
        QStringLiteral("pak"), QStringLiteral("bin"), QStringLiteral("dat"),
        QStringLiteral("ttf"), QStringLiteral("otf"), QStringLiteral("fbx"),
        QStringLiteral("glb"), QStringLiteral("blend"),
    };
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffixes.contains(suffix))
        return true;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = file.read(512);
    return head.contains('\0');
}

QByteArray sampleHash(const QString& path, quint64 limit, bool* complete, qint64 size)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (complete != nullptr)
            *complete = false;
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const quint64 readLimit = std::min<quint64>(limit, static_cast<quint64>(size));
    quint64 remaining = readLimit;
    while (remaining > 0) {
        const QByteArray chunk = file.read(static_cast<qint64>(std::min<quint64>(
            remaining, 256ULL * 1024ULL)));
        if (chunk.isEmpty())
            break;
        hash.addData(chunk);
        remaining -= static_cast<quint64>(chunk.size());
    }
    if (complete != nullptr)
        *complete = static_cast<qint64>(readLimit) >= size;
    return hash.result();
}

QStringList collectTree(const QString& root, QStringList* notes, bool* truncated)
{
    QStringList files;
    if (root.isEmpty() || !QFileInfo(root).isDir()) {
        if (notes != nullptr)
            notes->append(QStringLiteral("Not a readable folder: %1").arg(root));
        return files;
    }
    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        const QString relative = QDir(root).relativeFilePath(path);
        const QStringList parts = relative.split(QLatin1Char('/'));
        bool skip = false;
        for (const QString& part : parts) {
            if (skippedDirectoryNames().contains(part, Qt::CaseInsensitive)) {
                skip = true;
                break;
            }
        }
        if (skip)
            continue;
        files.append(relative);
    }
    std::sort(files.begin(), files.end());
    if (notes != nullptr && files.size() > 0)
        notes->append(QStringLiteral("%1: %2 file(s) scanned.").arg(root).arg(files.size()));
    Q_UNUSED(truncated)
    return files;
}

QStringList lineDifference(const QString& leftPath, const QString& rightPath, int maxLines)
{
    auto readLines = [maxLines](const QString& path) {
        QStringList lines;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return lines;
        QByteArray data = file.read(512 * 1024);
        const QString text = QString::fromUtf8(data);
        const QStringList all = text.split(QLatin1Char('\n'));
        for (const QString& line : all) {
            if (lines.size() >= maxLines)
                break;
            lines.append(line.trimmed());
        }
        return lines;
    };
    const QStringList left = readLines(leftPath);
    const QStringList right = readLines(rightPath);
    QHash<QString, int> balance;
    for (const QString& line : left) {
        if (!line.isEmpty())
            ++balance[line];
    }
    for (const QString& line : right) {
        if (!line.isEmpty())
            --balance[line];
    }
    QStringList result;
    for (auto it = balance.constBegin(); it != balance.constEnd() && result.size() < 6; ++it) {
        if (it.value() > 0)
            result.append(QStringLiteral("\u2212 %1").arg(it.key()));
        else if (it.value() < 0)
            result.append(QStringLiteral("+ %1").arg(it.key()));
    }
    if (result.isEmpty())
        result.append(QStringLiteral("Ordering or whitespace changed; no added or removed "
                                     "line was isolated."));
    return result;
}

struct SideData {
    gameforge::ProjectScan scan;
    gameforge::BuildConfigReport config;
    gameforge::PortingReport porting;
};

SideData analyzeSide(const QString& root, const CancelToken& token, const DiffLimits& limits)
{
    SideData data;
    data.scan = gameforge::scanProject(root, token);
    if (data.scan.cancelled)
        return data;
    gameforge::ConfigLimits configLimits;
    configLimits.maxFiles = limits.maxDependencyFiles;
    data.config = gameforge::analyzeBuildConfig(data.scan, token, configLimits);
    gameforge::PortingLimits portingLimits;
    data.porting = gameforge::analyzePortability(data.scan, token, portingLimits);
    return data;
}

QString dependencyKey(const gameforge::DeclaredDependency& dependency)
{
    return dependency.name.toLower();
}

}  // namespace

QString changeKindName(ChangeKind kind)
{
    switch (kind) {
    case ChangeKind::Added:
        return QStringLiteral("added");
    case ChangeKind::Removed:
        return QStringLiteral("removed");
    case ChangeKind::Modified:
        return QStringLiteral("modified");
    case ChangeKind::Unchanged:
        return QStringLiteral("unchanged");
    }
    return QStringLiteral("unknown");
}

DiffReport compareProjects(const QString& leftRoot, const QString& rightRoot,
                           const CancelToken& token, const DiffLimits& limits)
{
    DiffReport report;
    report.leftRoot = leftRoot;
    report.rightRoot = rightRoot;

    if (leftRoot.trimmed().isEmpty() || rightRoot.trimmed().isEmpty()) {
        report.notes.append(QStringLiteral("Choose two project folders to compare."));
        return report;
    }
    if (QFileInfo(leftRoot).absoluteFilePath() == QFileInfo(rightRoot).absoluteFilePath()) {
        report.notes.append(QStringLiteral("The two folders are the same; nothing to compare."));
        return report;
    }

    QStringList leftFiles = collectTree(leftRoot, &report.notes, &report.truncated);
    QStringList rightFiles = collectTree(rightRoot, &report.notes, &report.truncated);
    if (leftFiles.size() + rightFiles.size() > limits.maxFiles) {
        report.truncated = true;
        report.notes.append(QStringLiteral("Only the first %1 files were compared.")
                                .arg(limits.maxFiles));
        leftFiles = leftFiles.mid(0, limits.maxFiles / 2);
        rightFiles = rightFiles.mid(0, limits.maxFiles / 2);
    }

    QSet<QString> allPaths;
    for (const QString& file : leftFiles)
        allPaths.insert(file);
    for (const QString& file : rightFiles)
        allPaths.insert(file);
    QStringList paths(allPaths.begin(), allPaths.end());
    std::sort(paths.begin(), paths.end());

    const QSet<QString> leftSet(leftFiles.begin(), leftFiles.end());
    const QSet<QString> rightSet(rightFiles.begin(), rightFiles.end());

    int binaryComparisons = 0;
    int processed = 0;
    for (const QString& relative : paths) {
        if ((processed % 64) == 0 && token.isCancelled()) {
            report.cancelled = true;
            report.notes.append(QStringLiteral("Comparison cancelled; the report covers the "
                                               "files read so far."));
            break;
        }
        ++processed;
        const QString leftPath = QDir(leftRoot).filePath(relative);
        const QString rightPath = QDir(rightRoot).filePath(relative);
        const bool inLeft = leftSet.contains(relative);
        const bool inRight = rightSet.contains(relative);

        FileChange change;
        change.path = relative;
        if (inLeft)
            change.leftSize = QFileInfo(leftPath).size();
        if (inRight)
            change.rightSize = QFileInfo(rightPath).size();

        if (inLeft && !inRight) {
            change.kind = ChangeKind::Removed;
            ++report.removed;
        } else if (!inLeft && inRight) {
            change.kind = ChangeKind::Added;
            ++report.added;
        } else {
            bool leftComplete = false;
            bool rightComplete = false;
            const QByteArray leftHash = sampleHash(leftPath, limits.contentHashBytes,
                                                   &leftComplete, change.leftSize);
            const QByteArray rightHash = sampleHash(rightPath, limits.contentHashBytes,
                                                    &rightComplete, change.rightSize);
            report.comparedBytes += static_cast<qint64>(std::min<quint64>(
                limits.contentHashBytes,
                static_cast<quint64>(std::max<qint64>(0, change.leftSize))));
            change.contentSampled = !(leftComplete && rightComplete);
            if (change.leftSize != change.rightSize || leftHash != rightHash) {
                change.kind = ChangeKind::Modified;
                ++report.modified;
            } else {
                change.kind = ChangeKind::Unchanged;
                ++report.unchanged;
                if (change.contentSampled) {
                    change.uncertain = true;
                    change.note = QStringLiteral("Sampled content matched; the full file was not "
                                                 "hashed.");
                    ++report.uncertain;
                }
            }
            if (change.kind == ChangeKind::Modified) {
                change.binary = looksBinary(inLeft ? leftPath : rightPath);
                if (change.binary && binaryComparisons < limits.maxBinaryComparisons) {
                    ++binaryComparisons;
                    const auto compared = compareFiles(leftPath, rightPath,
                                                       limits.maxBinaryCompareBytes, token);
                    if (compared) {
                        change.firstDifference = compared.value().firstDifference;
                        if (compared.value().truncated)
                            change.note = QStringLiteral("First difference is within the "
                                                         "compared prefix; the comparison was "
                                                         "bounded.");
                    } else {
                        change.note = compared.error().detail.isEmpty()
                            ? compared.error().message
                            : compared.error().detail;
                    }
                } else if (change.binary) {
                    change.note = QStringLiteral("Binary difference not compared: the binary "
                                                 "comparison limit was reached.");
                }
            }
        }
        report.files.append(change);
    }

    // Configuration files -------------------------------------------------
    for (const FileChange& change : report.files) {
        if (change.kind == ChangeKind::Unchanged)
            continue;
        const QString name = QFileInfo(change.path).fileName();
        if (!isConfigFileName(name))
            continue;
        if (report.configChanges.size() >= limits.maxConfigFiles) {
            report.truncated = true;
            break;
        }
        ConfigChange config;
        config.file = change.path;
        config.kind = change.kind;
        if (change.kind == ChangeKind::Modified) {
            config.sampleLines = lineDifference(QDir(leftRoot).filePath(change.path),
                                                QDir(rightRoot).filePath(change.path), 4000);
            config.summary = QStringLiteral("Configuration edited (%1 added/removed line(s) "
                                            "isolated)")
                                 .arg(config.sampleLines.size());
        } else {
            config.summary = change.kind == ChangeKind::Added
                ? QStringLiteral("Configuration file added")
                : QStringLiteral("Configuration file removed");
        }
        report.configChanges.append(config);
    }

    // Dependencies --------------------------------------------------------
    const SideData left = analyzeSide(leftRoot, token, limits);
    const SideData right = analyzeSide(rightRoot, token, limits);
    if (token.isCancelled() || left.scan.cancelled || right.scan.cancelled) {
        report.cancelled = true;
        report.notes.append(QStringLiteral("Analysis of the two projects was cancelled."));
    } else {
        QHash<QString, gameforge::DeclaredDependency> leftDeps;
        for (const auto& dependency : left.config.dependencies)
            leftDeps.insert(dependencyKey(dependency), dependency);
        QHash<QString, gameforge::DeclaredDependency> rightDeps;
        for (const auto& dependency : right.config.dependencies)
            rightDeps.insert(dependencyKey(dependency), dependency);

        QSet<QString> keys(leftDeps.keyBegin(), leftDeps.keyEnd());
        for (auto it = rightDeps.keyBegin(); it != rightDeps.keyEnd(); ++it)
            keys.insert(*it);
        QStringList sorted(keys.begin(), keys.end());
        std::sort(sorted.begin(), sorted.end());
        for (const QString& key : sorted) {
            const auto leftIt = leftDeps.constFind(key);
            const auto rightIt = rightDeps.constFind(key);
            DependencyChange change;
            if (leftIt == leftDeps.constEnd() && rightIt == rightDeps.constEnd())
                continue;
            if (leftIt == leftDeps.constEnd()) {
                change.name = rightIt.value().name;
                change.rightVersion = rightIt.value().version;
                change.change = QStringLiteral("added");
                change.rightEvidence = QStringLiteral("%1:%2")
                                           .arg(rightIt.value().file)
                                           .arg(rightIt.value().line);
            } else if (rightIt == rightDeps.constEnd()) {
                change.name = leftIt.value().name;
                change.leftVersion = leftIt.value().version;
                change.change = QStringLiteral("removed");
                change.leftEvidence = QStringLiteral("%1:%2")
                                          .arg(leftIt.value().file)
                                          .arg(leftIt.value().line);
            } else {
                if (leftIt.value().version == rightIt.value().version)
                    continue;
                change.name = leftIt.value().name;
                change.leftVersion = leftIt.value().version;
                change.rightVersion = rightIt.value().version;
                change.change = QStringLiteral("changed");
                change.leftEvidence = QStringLiteral("%1:%2")
                                          .arg(leftIt.value().file)
                                          .arg(leftIt.value().line);
                change.rightEvidence = QStringLiteral("%1:%2")
                                           .arg(rightIt.value().file)
                                           .arg(rightIt.value().line);
            }
            report.dependencyChanges.append(change);
        }

        QHash<QString, int> leftPorting;
        QHash<QString, QString> areas;
        for (const auto& finding : left.porting.findings) {
            leftPorting.insert(finding.label, finding.occurrences);
            areas.insert(finding.label, gameforge::signalAreaName(finding.area));
        }
        QHash<QString, int> rightPorting;
        for (const auto& finding : right.porting.findings) {
            rightPorting.insert(finding.label, finding.occurrences);
            areas.insert(finding.label, gameforge::signalAreaName(finding.area));
        }
        QSet<QString> labels(leftPorting.keyBegin(), leftPorting.keyEnd());
        for (auto it = rightPorting.keyBegin(); it != rightPorting.keyEnd(); ++it)
            labels.insert(*it);
        QStringList sortedLabels(labels.begin(), labels.end());
        std::sort(sortedLabels.begin(), sortedLabels.end());
        for (const QString& label : sortedLabels) {
            const int leftCount = leftPorting.value(label, 0);
            const int rightCount = rightPorting.value(label, 0);
            if (leftCount == rightCount)
                continue;
            PortabilityDelta delta;
            delta.label = label;
            delta.area = areas.value(label);
            delta.leftOccurrences = leftCount;
            delta.rightOccurrences = rightCount;
            if (leftCount == 0)
                delta.change = QStringLiteral("added");
            else if (rightCount == 0)
                delta.change = QStringLiteral("removed");
            else
                delta.change = QStringLiteral("changed");
            report.portabilityChanges.append(delta);
        }
        if (left.config.truncated || right.config.truncated || left.porting.truncated
            || right.porting.truncated) {
            report.truncated = true;
            report.notes.append(QStringLiteral("A build or portability scan was truncated; "
                                               "section results cover the files read."));
        }
    }

    if (report.notes.isEmpty())
        report.notes.append(QStringLiteral("Both trees were read completely."));
    return report;
}

QString toMarkdown(const DiffReport& report)
{
    QString text;
    text += QStringLiteral("# PKG Studio project diff\n\n");
    text += QStringLiteral("- Left:  `%1`\n").arg(report.leftRoot);
    text += QStringLiteral("- Right: `%1`\n").arg(report.rightRoot);
    text += QStringLiteral("- Files: %1 added, %2 removed, %3 modified, %4 unchanged")
                .arg(report.added)
                .arg(report.removed)
                .arg(report.modified)
                .arg(report.unchanged);
    if (report.uncertain > 0)
        text += QStringLiteral(", %1 size-sampled match(es)").arg(report.uncertain);
    text += QLatin1Char('\n');
    text += QStringLiteral("- Content compared: %1 bytes hashed (bounded)\n\n")
                .arg(report.comparedBytes);

    text += QStringLiteral("## Files\n\n");
    if (report.files.isEmpty()) {
        text += QStringLiteral("No files were listed.\n\n");
    } else {
        text += QStringLiteral("| Change | Path | Left size | Right size | Note |\n");
        text += QStringLiteral("| --- | --- | ---: | ---: | --- |\n");
        for (const FileChange& change : report.files) {
            if (change.kind == ChangeKind::Unchanged)
                continue;
            QString note = change.note;
            if (change.kind == ChangeKind::Modified && change.binary)
                note = QStringLiteral("binary, first difference at 0x%1")
                           .arg(change.firstDifference, 0, 16);
            text += QStringLiteral("| %1 | `%2` | %3 | %4 | %5 |\n")
                        .arg(changeKindName(change.kind), change.path)
                        .arg(change.leftSize)
                        .arg(change.rightSize)
                        .arg(note);
        }
        text += QLatin1Char('\n');
    }

    text += QStringLiteral("## Configuration changes\n\n");
    if (report.configChanges.isEmpty()) {
        text += QStringLiteral("No build or engine configuration file changed.\n\n");
    } else {
        for (const ConfigChange& change : report.configChanges) {
            text += QStringLiteral("- **%1** (`%2`): %3\n")
                        .arg(changeKindName(change.kind), change.file, change.summary);
            for (const QString& line : change.sampleLines)
                text += QStringLiteral("  - `%1`\n").arg(line);
        }
        text += QLatin1Char('\n');
    }

    text += QStringLiteral("## Dependency changes\n\n");
    if (report.dependencyChanges.isEmpty()) {
        text += QStringLiteral("Declared dependencies are unchanged.\n\n");
    } else {
        for (const DependencyChange& change : report.dependencyChanges) {
            text += QStringLiteral("- **%1**: %2 `%3` \u2192 `%4` (%5 \u2192 %6)\n")
                        .arg(change.name, change.change, change.leftVersion,
                             change.rightVersion, change.leftEvidence, change.rightEvidence);
        }
        text += QLatin1Char('\n');
    }

    text += QStringLiteral("## Portability differences\n\n");
    if (report.portabilityChanges.isEmpty()) {
        text += QStringLiteral("No portability finding changed its occurrence count.\n\n");
    } else {
        for (const PortabilityDelta& delta : report.portabilityChanges) {
            text += QStringLiteral("- **%1** (%2): %3 \u2192 %4 occurrence(s), %5\n")
                        .arg(delta.label, delta.area)
                        .arg(delta.leftOccurrences)
                        .arg(delta.rightOccurrences)
                        .arg(delta.change);
        }
        text += QLatin1Char('\n');
    }

    text += QStringLiteral("## Notes\n\n");
    for (const QString& note : report.notes)
        text += QStringLiteral("- %1\n").arg(note);
    if (report.truncated)
        text += QStringLiteral("- The comparison reached a limit; this report is incomplete.\n");
    if (report.cancelled)
        text += QStringLiteral("- The comparison was cancelled; this report is incomplete.\n");
    return text;
}

}  // namespace pkg::projectdiff
