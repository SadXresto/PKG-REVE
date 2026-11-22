// GameForge: read-only inspection of a local game project folder.
//
// The scanner never writes, never executes anything from the project and never
// follows symbolic links. All limits are explicit so a malformed or enormous
// folder cannot stall the caller; the UI runs it on a worker thread.
#pragma once

#include "core/CancelToken.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace pkg::gameforge {

/// Build system / engine identified from marker files. `Unknown` means the
/// folder could not be read at all; `Generic` means it was read but no
/// supported build system was detected.
enum class ProjectKind {
    Unknown,
    Generic,
    CMake,
    Meson,
    Makefile,
    Godot,
    Unity,
    Unreal,
};

[[nodiscard]] QString projectKindName(ProjectKind kind);

enum class FileCategory {
    Source,
    Header,
    Shader,
    Texture,
    Model,
    Audio,
    Asset,
    Config,
    Build,
    Binary,
    Script,
    Document,
    Other,
};

[[nodiscard]] QString categoryName(FileCategory category);
/// Suffix is matched case-insensitively; pass it with or without the dot.
[[nodiscard]] FileCategory categoryForSuffix(const QString& suffix);

struct ProjectFile {
    QString relativePath;
    FileCategory category = FileCategory::Other;
    qint64 size = 0;
};

struct ScanLimits {
    int maxFiles = 40000;
    int maxDepth = 32;
    qint64 maxBytes = 32LL * 1024 * 1024 * 1024;
};

struct ProjectScan {
    QString rootPath;
    QString displayName;
    ProjectKind kind = ProjectKind::Unknown;
    /// The marker file that identified `kind`, relative to the root.
    QString kindEvidence;
    QVector<ProjectFile> files;
    /// Recognised build descriptors and engine project files, relative paths.
    QStringList buildFiles;
    QMap<int, int> filesByCategory;    // FileCategory -> number of files
    QMap<int, qint64> bytesByCategory; // FileCategory -> total bytes
    int directoryCount = 0;
    int skippedEntries = 0;
    qint64 totalBytes = 0;
    bool truncated = false;
    bool cancelled = false;
    QStringList warnings;

    [[nodiscard]] int fileCount() const { return static_cast<int>(files.size()); }
    [[nodiscard]] bool readable() const { return !rootPath.isEmpty(); }
};

/// Walks `rootPath`. Returns an invalid scan (empty rootPath, one warning) when
/// the path is not a readable directory.
[[nodiscard]] ProjectScan scanProject(const QString& rootPath, const CancelToken& token = {},
                                      const ScanLimits& limits = {});

enum class SignalArea { Platform, Graphics, Audio, Input, Threading, Build };

[[nodiscard]] QString signalAreaName(SignalArea area);

/// One detected platform/engine API family. Occurrences are substring matches
/// in text-like project files; they are evidence to review, not a verdict.
struct PortingFinding {
    QString id;
    QString label;
    SignalArea area = SignalArea::Platform;
    int occurrences = 0;
    int fileCount = 0;
    QStringList files; // relative paths, capped
    QString example;   // "src/main.cpp:42"
    QString note;
};

struct PortingLimits {
    int maxFiles = 2000;
    qint64 maxBytesPerFile = 512 * 1024;
    qint64 maxBytesTotal = 24LL * 1024 * 1024;
};

struct PortingReport {
    int scannedFiles = 0;
    int skippedFiles = 0;
    qint64 scannedBytes = 0;
    bool truncated = false;
    bool cancelled = false;
    QVector<PortingFinding> findings;
    QStringList notes;

    [[nodiscard]] bool hasFindings() const { return !findings.isEmpty(); }
    /// Findings that usually block a straight Windows/Linux port.
    [[nodiscard]] int blockingCount() const;
};

/// Reads text-like files from a scan and looks for known API families.
/// Binary files, files without text suffixes and files above `maxBytesPerFile`
/// are skipped and counted.
[[nodiscard]] PortingReport analyzePortability(const ProjectScan& scan,
                                               const CancelToken& token = {},
                                               const PortingLimits& limits = {});

} // namespace pkg::gameforge
