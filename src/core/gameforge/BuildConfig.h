// GameForge build-configuration inspection.
//
// Only formats whose syntax is actually parsed here are claimed: CMake
// (CMakeLists.txt), Meson (meson.build), Makefiles, Godot project.godot and
// Unreal *.uproject. Everything reported carries the file and line it came
// from; nothing is assumed when a construct is not understood.
#pragma once

#include "core/CancelToken.h"
#include "core/gameforge/GameProject.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace pkg::gameforge {

enum class ConfigKind {
    Target,
    Dependency,
    CompilerSetting,
    PlatformCondition,
    Architecture,
    BuildFile,
};

[[nodiscard]] QString configKindName(ConfigKind kind);

/// One parsed statement. `file` is relative to the project root and `line` is
/// 1-based; `evidence` is the trimmed source line, capped in length.
struct ConfigFinding {
    ConfigKind kind = ConfigKind::BuildFile;
    QString name;
    QString detail;
    QString file;
    int line = 0;
    QString evidence;
    /// False when the value is derived (for example a link flag interpreted as
    /// an architecture requirement) instead of written literally.
    bool literal = true;
};

struct BuildTarget {
    QString name;
    QString type; // "executable", "library", "tool", "custom target"
    QString file;
    int line = 0;
    QStringList sources;
    QStringList links;
};

struct DeclaredDependency {
    QString name;
    QString version;
    QString declaredBy; // find_package / dependency() / target_link_libraries / plugin
    QString file;
    int line = 0;
    /// True when a build file declares it explicitly (find_package, dependency(),
    /// a plugin entry, or a Makefile prerequisite).
    bool explicitDeclaration = true;
};

struct BuildConfigReport {
    QString projectName;
    QString projectVersion;
    QString engineName;
    QString engineVersion;
    QVector<ConfigFinding> findings;
    QVector<BuildTarget> targets;
    QVector<DeclaredDependency> dependencies;
    QStringList analyzedFiles;
    QStringList warnings;
    int linesRead = 0;
    bool truncated = false;
    bool cancelled = false;

    [[nodiscard]] bool empty() const { return analyzedFiles.isEmpty(); }
    [[nodiscard]] const BuildTarget* findTarget(const QString& name) const;
};

struct ConfigLimits {
    int maxFiles = 60;
    qint64 maxBytesPerFile = 512 * 1024;
    int maxFindings = 4000;
    int maxCommandsPerFile = 20000;
};

/// Parses the project's build files. Files are read only if they are small
/// enough; unreadable or unparsable files become warnings, never exceptions.
[[nodiscard]] BuildConfigReport analyzeBuildConfig(const ProjectScan& scan,
                                                   const CancelToken& token = {},
                                                   const ConfigLimits& limits = {});

} // namespace pkg::gameforge
