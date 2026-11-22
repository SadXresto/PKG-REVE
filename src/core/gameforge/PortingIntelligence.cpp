#include "core/gameforge/PortingIntelligence.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace pkg::gameforge {
namespace {

constexpr int kMaxPerSection = 40;
constexpr int kMaxRecommendations = 12;

void splitExample(const QString& example, QString* file, int* line)
{
    const int colon = static_cast<int>(example.lastIndexOf(QLatin1Char(':')));
    if (colon <= 0) {
        *file = example;
        *line = 0;
        return;
    }
    bool ok = false;
    const int parsed = example.mid(colon + 1).toInt(&ok);
    *file = example.left(colon);
    *line = ok ? parsed : 0;
}

bool isWindowsApi(const QString& id)
{
    static const QSet<QString> ids = {
        QStringLiteral("win32"),    QStringLiteral("windows.loader"),
        QStringLiteral("directx"),  QStringLiteral("xinput"),
        QStringLiteral("rawinput"), QStringLiteral("xaudio"),
    };
    return ids.contains(id);
}

bool isPosixApi(const QString& id)
{
    static const QSet<QString> ids = {QStringLiteral("posix"), QStringLiteral("pthread")};
    return ids.contains(id);
}

void addInsight(QVector<IntelInsight>& list, const QString& title, const QString& detail,
                const QString& file, int line, const QString& evidence)
{
    if (list.size() >= kMaxPerSection)
        return;
    list.append({title, detail, file, line, evidence});
}

} // namespace

bool PortingIntelligence::empty() const
{
    return windows.isEmpty() && linuxPlatform.isEmpty() && architecture.isEmpty()
        && graphicsInput.isEmpty() && externalDependencies.isEmpty()
        && recommendations.isEmpty();
}

int PortingIntelligence::total() const
{
    return static_cast<int>(windows.size() + linuxPlatform.size() + architecture.size()
                            + graphicsInput.size() + externalDependencies.size()
                            + recommendations.size());
}

PortingIntelligence analyzePortingIntelligence(const BuildConfigReport& config,
                                               const DependencyGraph& graph,
                                               const PortingReport& portability)
{
    PortingIntelligence intel;

    for (const ConfigFinding& finding : config.findings) {
        if (finding.kind != ConfigKind::PlatformCondition)
            continue;
        const QString upper = finding.name.toUpper();
        if (upper.contains(QLatin1String("WIN32")) || upper.contains(QLatin1String("MSVC"))
            || upper.contains(QLatin1String("MINGW"))) {
            addInsight(intel.windows,
                       QStringLiteral("Windows-only build branch (%1)").arg(finding.name),
                       finding.detail, finding.file, finding.line, finding.evidence);
        } else if (upper.contains(QLatin1String("UNIX"))
                   || upper.contains(QLatin1String("LINUX"))
                   || upper.contains(QLatin1String("ANDROID"))
                   || upper.contains(QLatin1String("APPLE"))
                   || upper.contains(QLatin1String("IOS"))
                   || upper.contains(QLatin1String("EMSCRIPTEN"))) {
            addInsight(intel.linuxPlatform,
                       QStringLiteral("POSIX/Apple build branch (%1)").arg(finding.name),
                       finding.detail, finding.file, finding.line, finding.evidence);
        }
    }

    for (const ConfigFinding& finding : config.findings) {
        if (finding.kind == ConfigKind::Architecture) {
            addInsight(intel.architecture, finding.name, finding.detail, finding.file,
                       finding.line, finding.evidence);
        }
    }

    for (const PortingFinding& finding : portability.findings) {
        QString file;
        int line = 0;
        splitExample(finding.example, &file, &line);
        const QString detail =
            QStringLiteral("%1 matches in %2 file(s). %3")
                .arg(finding.occurrences)
                .arg(finding.fileCount)
                .arg(finding.note);
        if (isWindowsApi(finding.id)) {
            addInsight(intel.windows, finding.label, detail, file, line, finding.example);
        } else if (isPosixApi(finding.id)) {
            addInsight(intel.linuxPlatform, finding.label, detail, file, line, finding.example);
        }
        if (finding.area == SignalArea::Graphics || finding.area == SignalArea::Input) {
            addInsight(intel.graphicsInput, finding.label, detail, file, line, finding.example);
        }
    }

    // External dependency availability: declared packages and undeclared links.
    QSet<QString> seenNames;
    for (const DeclaredDependency& dependency : config.dependencies) {
        const QString key = dependency.name.toLower();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        if (dependency.explicitDeclaration) {
            addInsight(intel.externalDependencies, dependency.name,
                       QStringLiteral("Declared with %1%2 in %3:%4; the build machine must "
                                      "provide it on every target platform.")
                           .arg(dependency.declaredBy,
                                dependency.version.isEmpty()
                                    ? QString()
                                    : QStringLiteral(" (version %1)").arg(dependency.version),
                                dependency.file)
                           .arg(dependency.line),
                       dependency.file, dependency.line,
                       QStringLiteral("%1(%2)").arg(dependency.declaredBy, dependency.name));
        } else {
            addInsight(intel.externalDependencies, dependency.name,
                       QStringLiteral("Linked as '%1' in %2:%3 without a find_package/"
                                      "dependency() declaration, so where it comes from is not "
                                      "recorded.")
                           .arg(dependency.name, dependency.file)
                           .arg(dependency.line),
                       dependency.file, dependency.line,
                       QStringLiteral("target_link_libraries entry"));
        }
    }

    // Recommendations are derived only from the findings above.
    auto recommend = [&intel](const QString& title, const QString& detail, const QString& file,
                              int line) {
        if (intel.recommendations.size() >= kMaxRecommendations)
            return;
        intel.recommendations.append({title, detail, file, line, {}});
    };

    if (!intel.windows.isEmpty() && intel.linuxPlatform.isEmpty()) {
        const IntelInsight& first = intel.windows.first();
        recommend(
            QStringLiteral("Add a non-Windows code path"),
            QStringLiteral("Windows-only configuration was found and no UNIX/LINUX branch was "
                           "found in the inspected build files. A Linux build needs a platform "
                           "layer or an additional conditional for the code below."),
            first.file, first.line);
    }
    if (!intel.linuxPlatform.isEmpty() && intel.windows.isEmpty()) {
        const IntelInsight& first = intel.linuxPlatform.first();
        recommend(
            QStringLiteral("Add a Windows code path"),
            QStringLiteral("POSIX/Apple configuration was found and no WIN32/MSVC branch was "
                           "found in the inspected build files."),
            first.file, first.line);
    }

    bool posixThreads = false;
    QString threadFile;
    int threadLine = 0;
    for (const ConfigFinding& finding : config.findings) {
        if (finding.detail.contains(QLatin1String("POSIX thread"))
            || finding.evidence.contains(QLatin1String("-pthread"))) {
            posixThreads = true;
            threadFile = finding.file;
            threadLine = finding.line;
            break;
        }
    }
    if (posixThreads) {
        recommend(QStringLiteral("Replace POSIX thread flags for MSVC"),
                  QStringLiteral("-pthread is a GCC/Clang flag that MSVC does not accept; "
                                 "std::thread or SDL_Thread compile on both targets."),
                  threadFile, threadLine);
    }

    if (!graph.unresolved.isEmpty()) {
        QStringList names;
        for (const QString& entry : graph.unresolved) {
            if (names.size() >= 5)
                break;
            const int dash = static_cast<int>(entry.indexOf(QStringLiteral(" \u2014 ")));
            names.append(dash > 0 ? entry.left(dash) : entry);
        }
        if (intel.externalDependencies.isEmpty()) {
            recommend(QStringLiteral("Declare link-only dependencies"),
                      QStringLiteral("These names are linked but never declared: %1. A "
                                     "find_package or pkg-config declaration makes them "
                                     "resolvable on both platforms.")
                          .arg(names.join(QStringLiteral(", "))),
                      QString(), 0);
        } else {
            const IntelInsight& first = intel.externalDependencies.first();
            recommend(QStringLiteral("Declare link-only dependencies"),
                      QStringLiteral("These names are linked but never declared: %1.")
                          .arg(names.join(QStringLiteral(", "))),
                      first.file, first.line);
        }
    }

    const bool hasStandard = std::any_of(
        config.findings.begin(), config.findings.end(), [](const ConfigFinding& finding) {
            return finding.kind == ConfigKind::CompilerSetting
                && finding.name.contains(QLatin1String("STANDARD"));
        });
    if (!config.analyzedFiles.isEmpty() && !hasStandard) {
        recommend(QStringLiteral("Pin the C++ standard"),
                  QStringLiteral("No CMAKE_CXX_STANDARD or equivalent was found in %1 "
                                 "inspected build file(s); pinning it keeps both toolchains "
                                 "building the same language level.")
                      .arg(config.analyzedFiles.size()),
                  config.analyzedFiles.first(), 1);
    }

    intel.notes.append(
        QStringLiteral("Assembled from %1 inspected build file(s) and %2 source file(s) read by "
                       "the API scan. A configuration that is not mentioned may still be "
                       "incompatible.")
            .arg(config.analyzedFiles.size())
            .arg(portability.scannedFiles));
    if (intel.architecture.isEmpty()) {
        intel.notes.append(QStringLiteral("No architecture requirement (32/64-bit) was "
                                          "declared in the inspected build files."));
    }
    intel.notes.append(QStringLiteral("No build was run and no project code was executed; "
                                      "these findings are static evidence only."));
    return intel;
}

} // namespace pkg::gameforge
