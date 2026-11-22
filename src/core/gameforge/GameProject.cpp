#include "core/gameforge/GameProject.h"

#include "core/Logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <utility>

namespace pkg::gameforge {
namespace {

constexpr auto kLogCategory = "gameforge";
constexpr qint64 kSniffBytes = 4096;
constexpr int kMaxExampleListed = 10;
constexpr int kMaxMatchesPerTokenPerFile = 2000;

QString normalizedSuffix(const QString& suffix)
{
    const QString trimmed = suffix.trimmed().toLower();
    if (trimmed.isEmpty())
        return {};
    return trimmed.startsWith(QLatin1Char('.')) ? trimmed : QLatin1Char('.') + trimmed;
}

/// Directory names that never contain project sources and can be very large
/// (engine caches, VCS metadata, build trees). They are skipped and counted.
bool isSkippedDirectory(const QString& name)
{
    static const QSet<QString> skipped = {
        QStringLiteral(".git"),        QStringLiteral(".svn"),
        QStringLiteral(".hg"),         QStringLiteral(".vs"),
        QStringLiteral(".idea"),       QStringLiteral(".vscode"),
        QStringLiteral("node_modules"), QStringLiteral("__pycache__"),
        QStringLiteral("build"),       QStringLiteral("build-windows"),
        QStringLiteral("dist"),        QStringLiteral("dist-win"),
        QStringLiteral("library"),     QStringLiteral("temp"),
        QStringLiteral("obj"),         QStringLiteral("intermediate"),
        QStringLiteral("saved"),       QStringLiteral("deriveddatacache"),
    };
    return skipped.contains(name.toLower());
}

bool containsMarker(const QString& directory, const QString& name)
{
    return QFileInfo::exists(QDir(directory).filePath(name));
}

struct Marker {
    ProjectKind kind;
    QString file;
    bool isDirectory;
};

ProjectKind detectKind(const QString& root, QString* evidence, QStringList* buildFiles,
                       QStringList* warnings)
{
    const QDir dir(root);
    QStringList uprojects =
        dir.entryList({QStringLiteral("*.uproject")}, QDir::Files | QDir::Readable);
    if (!uprojects.isEmpty()) {
        if (evidence != nullptr)
            *evidence = uprojects.first();
        if (buildFiles != nullptr)
            buildFiles->append(uprojects);
        return ProjectKind::Unreal;
    }
    if (containsMarker(root, QStringLiteral("project.godot"))) {
        if (evidence != nullptr)
            *evidence = QStringLiteral("project.godot");
        if (buildFiles != nullptr)
            buildFiles->append(QStringLiteral("project.godot"));
        return ProjectKind::Godot;
    }
    if (containsMarker(root, QStringLiteral("CMakeLists.txt"))) {
        if (evidence != nullptr)
            *evidence = QStringLiteral("CMakeLists.txt");
        if (buildFiles != nullptr)
            buildFiles->append(QStringLiteral("CMakeLists.txt"));
        return ProjectKind::CMake;
    }
    if (containsMarker(root, QStringLiteral("meson.build"))) {
        if (evidence != nullptr)
            *evidence = QStringLiteral("meson.build");
        if (buildFiles != nullptr)
            buildFiles->append(QStringLiteral("meson.build"));
        return ProjectKind::Meson;
    }
    for (const QString& name : {QStringLiteral("Makefile"), QStringLiteral("makefile"),
                                QStringLiteral("GNUmakefile")}) {
        if (containsMarker(root, name)) {
            if (evidence != nullptr)
                *evidence = name;
            if (buildFiles != nullptr)
                buildFiles->append(name);
            return ProjectKind::Makefile;
        }
    }
    // Unity keeps its project markers in two sibling folders.
    if (QFileInfo(QDir(root).filePath(QStringLiteral("Assets"))).isDir()
        && QFileInfo(QDir(root).filePath(QStringLiteral("ProjectSettings"))).isDir()) {
        if (evidence != nullptr)
            *evidence = QStringLiteral("Assets/ + ProjectSettings/");
        if (buildFiles != nullptr) {
            buildFiles->append(QStringLiteral("Assets/"));
            buildFiles->append(QStringLiteral("ProjectSettings/"));
        }
        return ProjectKind::Unity;
    }
    if (warnings != nullptr && !dir.exists())
        warnings->append(QStringLiteral("The project folder disappeared while scanning."));
    return ProjectKind::Generic;
}

/// Text-like categories only. Assets, models, audio and executables are never
/// read for API tokens.
bool isTextScanCandidate(FileCategory category, const QString& relativePath)
{
    switch (category) {
    case FileCategory::Source:
    case FileCategory::Header:
    case FileCategory::Shader:
    case FileCategory::Script:
    case FileCategory::Build:
        return true;
    case FileCategory::Config:
        return relativePath.endsWith(QLatin1String(".cmake"), Qt::CaseInsensitive)
            || relativePath.endsWith(QLatin1String(".txt"), Qt::CaseInsensitive);
    default:
        return false;
    }
}

struct TokenSpec {
    const char* id;
    const char* label;
    SignalArea area;
    std::initializer_list<const char*> tokens;
    const char* note;
};

const TokenSpec kSpecs[] = {
    {"win32", "Win32 windowing and OS API", SignalArea::Platform,
     {"windows.h", "WinMain", "CreateWindowEx", "HWND"},
     "Windows-only headers and windowing calls. A port needs a platform layer such as "
     "SDL2/GLFW or #ifdef branches for the other target."},
    {"windows.loader", "LoadLibrary / GetProcAddress", SignalArea::Platform,
     {"LoadLibrary", "GetProcAddress"},
     "Windows-only dynamic loading. POSIX needs dlopen/dlsym behind the same wrapper."},
    {"posix", "POSIX system API", SignalArea::Platform,
     {"<unistd.h>", "dlopen(", "<sys/mman.h>"},
     "Linux/Unix-only APIs. A Windows build needs equivalents or a portability shim."},
    {"directx", "Direct3D", SignalArea::Graphics,
     {"d3d11", "d3d12", "d3d9", "ID3D11Device", "D3D12CreateDevice"},
     "Direct3D is Windows-only: a Linux target needs another renderer or a translation layer."},
    {"metal", "Metal", SignalArea::Graphics, {"Metal/Metal.h", "MTLDevice"},
     "Apple-only renderer; it does not build on Windows or Linux."},
    {"opengl", "OpenGL", SignalArea::Graphics,
     {"glGenTextures", "glBindBuffer", "GL/gl.h", "OpenGL"},
     "Cross-platform, but the context/loader creation is per-OS."},
    {"vulkan", "Vulkan", SignalArea::Graphics,
     {"vulkan.h", "VkInstance", "vkCreateInstance"},
     "Cross-platform renderer; check the surface creation code for per-OS branches."},
    {"sdl", "SDL2 / SDL3", SignalArea::Platform,
     {"SDL_Init", "SDL_CreateWindow", "SDL.h"},
     "Cross-platform windowing, input and audio: usually the quickest base for a port."},
    {"glfw", "GLFW", SignalArea::Platform, {"glfwInit", "glfw3.h"},
     "Cross-platform windowing; portable as-is."},
    {"unity", "Unity engine API", SignalArea::Platform,
     {"UnityEngine", "MonoBehaviour"},
     "Unity code is ported by adding Windows/Linux build targets in the editor, not by "
     "rewriting these calls."},
    {"godot", "Godot project API", SignalArea::Platform,
     {"res://", "extends Node", "GDScript"},
     "Godot projects export to Windows and Linux from one project; review the export presets."},
    {"xinput", "XInput (Xbox controller)", SignalArea::Input,
     {"XInput", "xinput.h"},
     "Windows-only controller API. SDL's gamepad API would cover both targets."},
    {"rawinput", "Raw Input", SignalArea::Input,
     {"RegisterRawInputDevices", "RAWINPUT"},
     "Windows-only raw input; mouse/keyboard capture needs a portable path."},
    {"xaudio", "XAudio2", SignalArea::Audio, {"XAudio2", "IXAudio2"},
     "Windows-only audio API; OpenAL, SDL audio or miniaudio are portable options."},
    {"coreaudio", "CoreAudio / AudioToolbox", SignalArea::Audio,
     {"AudioToolbox", "AudioUnit", "kAudioFormat"},
     "Apple-only audio API."},
    {"openal", "OpenAL", SignalArea::Audio,
     {"alSourcePlay", "AL/al.h", "OpenAL"},
     "Cross-platform audio; check that the runtime library ships with the build."},
    {"middleware.audio", "FMOD / Wwise", SignalArea::Audio,
     {"FMOD_System", "AK::SoundEngine"},
     "Cross-platform middleware; the matching native library per target is required."},
    {"pthread", "POSIX threads", SignalArea::Threading,
     {"pthread_create", "pthread_mutex_", "<pthread.h>"},
     "Not available on MSVC; std::thread or SDL_Thread remove the difference."},
    {"stdthread", "C++ standard threads", SignalArea::Threading,
     {"std::thread", "std::mutex", "std::atomic"},
     "Portable C++ standard library facilities."},
};

QString exampleLocation(const QString& relativePath, const QString& content, qsizetype index)
{
    const qsizetype line = content.left(index).count(QLatin1Char('\n')) + 1;
    return QStringLiteral("%1:%2").arg(relativePath).arg(line);
}

struct Aggregate {
    int occurrences = 0;
    QStringList files;
    QString example;
};

bool looksBinary(const QByteArray& data)
{
    const qsizetype limit = std::min<qsizetype>(data.size(), kSniffBytes);
    for (qsizetype i = 0; i < limit; ++i) {
        if (data.at(i) == '\0')
            return true;
    }
    return false;
}

} // namespace

QString projectKindName(ProjectKind kind)
{
    switch (kind) {
    case ProjectKind::CMake:
        return QStringLiteral("CMake");
    case ProjectKind::Meson:
        return QStringLiteral("Meson");
    case ProjectKind::Makefile:
        return QStringLiteral("Makefile");
    case ProjectKind::Godot:
        return QStringLiteral("Godot");
    case ProjectKind::Unity:
        return QStringLiteral("Unity");
    case ProjectKind::Unreal:
        return QStringLiteral("Unreal Engine");
    case ProjectKind::Generic:
        return QStringLiteral("Generic folder");
    case ProjectKind::Unknown:
        break;
    }
    return QStringLiteral("Unknown");
}

QString categoryName(FileCategory category)
{
    switch (category) {
    case FileCategory::Source:
        return QStringLiteral("Source");
    case FileCategory::Header:
        return QStringLiteral("Header");
    case FileCategory::Shader:
        return QStringLiteral("Shader");
    case FileCategory::Texture:
        return QStringLiteral("Texture");
    case FileCategory::Model:
        return QStringLiteral("Model");
    case FileCategory::Audio:
        return QStringLiteral("Audio");
    case FileCategory::Asset:
        return QStringLiteral("Asset");
    case FileCategory::Config:
        return QStringLiteral("Config");
    case FileCategory::Build:
        return QStringLiteral("Build");
    case FileCategory::Binary:
        return QStringLiteral("Binary");
    case FileCategory::Script:
        return QStringLiteral("Script");
    case FileCategory::Document:
        return QStringLiteral("Document");
    case FileCategory::Other:
        break;
    }
    return QStringLiteral("Other");
}

FileCategory categoryForSuffix(const QString& suffix)
{
    const QString key = normalizedSuffix(suffix);
    if (key.isEmpty())
        return FileCategory::Other;

    static const QHash<QString, FileCategory> map = {
        {QStringLiteral(".c"), FileCategory::Source},
        {QStringLiteral(".cc"), FileCategory::Source},
        {QStringLiteral(".cpp"), FileCategory::Source},
        {QStringLiteral(".cxx"), FileCategory::Source},
        {QStringLiteral(".m"), FileCategory::Source},
        {QStringLiteral(".mm"), FileCategory::Source},
        {QStringLiteral(".cs"), FileCategory::Source},
        {QStringLiteral(".gd"), FileCategory::Script},
        {QStringLiteral(".lua"), FileCategory::Script},
        {QStringLiteral(".py"), FileCategory::Script},
        {QStringLiteral(".js"), FileCategory::Script},
        {QStringLiteral(".ts"), FileCategory::Script},
        {QStringLiteral(".sh"), FileCategory::Script},
        {QStringLiteral(".bat"), FileCategory::Script},
        {QStringLiteral(".ps1"), FileCategory::Script},
        {QStringLiteral(".h"), FileCategory::Header},
        {QStringLiteral(".hh"), FileCategory::Header},
        {QStringLiteral(".hpp"), FileCategory::Header},
        {QStringLiteral(".hxx"), FileCategory::Header},
        {QStringLiteral(".inl"), FileCategory::Header},
        {QStringLiteral(".glsl"), FileCategory::Shader},
        {QStringLiteral(".hlsl"), FileCategory::Shader},
        {QStringLiteral(".vert"), FileCategory::Shader},
        {QStringLiteral(".frag"), FileCategory::Shader},
        {QStringLiteral(".comp"), FileCategory::Shader},
        {QStringLiteral(".shader"), FileCategory::Shader},
        {QStringLiteral(".png"), FileCategory::Texture},
        {QStringLiteral(".jpg"), FileCategory::Texture},
        {QStringLiteral(".jpeg"), FileCategory::Texture},
        {QStringLiteral(".tga"), FileCategory::Texture},
        {QStringLiteral(".dds"), FileCategory::Texture},
        {QStringLiteral(".ktx"), FileCategory::Texture},
        {QStringLiteral(".ktx2"), FileCategory::Texture},
        {QStringLiteral(".obj"), FileCategory::Model},
        {QStringLiteral(".fbx"), FileCategory::Model},
        {QStringLiteral(".gltf"), FileCategory::Model},
        {QStringLiteral(".glb"), FileCategory::Model},
        {QStringLiteral(".wav"), FileCategory::Audio},
        {QStringLiteral(".ogg"), FileCategory::Audio},
        {QStringLiteral(".mp3"), FileCategory::Audio},
        {QStringLiteral(".flac"), FileCategory::Audio},
        {QStringLiteral(".bank"), FileCategory::Audio},
        {QStringLiteral(".json"), FileCategory::Config},
        {QStringLiteral(".yaml"), FileCategory::Config},
        {QStringLiteral(".yml"), FileCategory::Config},
        {QStringLiteral(".toml"), FileCategory::Config},
        {QStringLiteral(".ini"), FileCategory::Config},
        {QStringLiteral(".cfg"), FileCategory::Config},
        {QStringLiteral(".xml"), FileCategory::Config},
        {QStringLiteral(".cmake"), FileCategory::Build},
        {QStringLiteral(".mk"), FileCategory::Build},
        {QStringLiteral(".vcxproj"), FileCategory::Build},
        {QStringLiteral(".sln"), FileCategory::Build},
        {QStringLiteral(".pro"), FileCategory::Build},
        {QStringLiteral(".gradle"), FileCategory::Build},
        {QStringLiteral(".exe"), FileCategory::Binary},
        {QStringLiteral(".dll"), FileCategory::Binary},
        {QStringLiteral(".so"), FileCategory::Binary},
        {QStringLiteral(".dylib"), FileCategory::Binary},
        {QStringLiteral(".elf"), FileCategory::Binary},
        {QStringLiteral(".pak"), FileCategory::Binary},
        {QStringLiteral(".zip"), FileCategory::Binary},
        {QStringLiteral(".pkg"), FileCategory::Binary},
        {QStringLiteral(".md"), FileCategory::Document},
        {QStringLiteral(".txt"), FileCategory::Document},
        {QStringLiteral(".pdf"), FileCategory::Document},
    };
    return map.value(key, FileCategory::Other);
}

ProjectScan scanProject(const QString& rootPath, const CancelToken& token,
                         const ScanLimits& limits)
{
    ProjectScan scan;
    const QFileInfo rootInfo(rootPath);
    if (rootPath.trimmed().isEmpty() || !rootInfo.exists() || !rootInfo.isDir()) {
        scan.warnings.append(
            QStringLiteral("%1 is not a folder that can be scanned.").arg(rootPath));
        logWarning(QString::fromLatin1(kLogCategory), scan.warnings.constLast());
        return scan;
    }

    const QDir root(rootInfo.absoluteFilePath());
    scan.rootPath = root.absolutePath();
    scan.displayName = rootInfo.fileName().isEmpty() ? scan.rootPath : rootInfo.fileName();
    scan.kind = detectKind(scan.rootPath, &scan.kindEvidence, &scan.buildFiles, &scan.warnings);

    QStringList buildNames = {QStringLiteral("CMakeLists.txt"), QStringLiteral("meson.build"),
                              QStringLiteral("Makefile"), QStringLiteral("makefile"),
                              QStringLiteral("GNUmakefile")};

    bool warnedSymlink = false;
    int filesSeen = 0;

    // Depth-first walk on an explicit stack: a deep tree cannot overflow the
    // call stack, and every entry is checked against the limits.
    struct Pending {
        QString absolute;
        QString relative;
        int depth;
    };
    QVector<Pending> pending;
    pending.append({scan.rootPath, QString(), 0});

    while (!pending.isEmpty()) {
        if (token.isCancelled()) {
            scan.cancelled = true;
            break;
        }
        const Pending current = pending.takeLast();
        if (current.depth > limits.maxDepth) {
            scan.skippedEntries++;
            scan.warnings.append(QStringLiteral("Stopped descending at %1 (depth limit).")
                                     .arg(current.relative.isEmpty()
                                              ? QStringLiteral("the project root")
                                              : current.relative));
            continue;
        }

        QDir dir(current.absolute);
        if (!dir.isReadable()) {
            scan.skippedEntries++;
            scan.warnings.append(
                QStringLiteral("Skipped unreadable folder %1.").arg(current.relative));
            continue;
        }

        const QFileInfoList entries =
            dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden
                                  | QDir::System | QDir::Readable,
                              QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& entry : entries) {
            if (token.isCancelled()) {
                scan.cancelled = true;
                break;
            }
            if (entry.isSymLink()) {
                scan.skippedEntries++;
                if (!warnedSymlink) {
                    warnedSymlink = true;
                    scan.warnings.append(
                        QStringLiteral("Symbolic links were skipped so the scan cannot leave "
                                       "the project folder."));
                }
                continue;
            }
            const QString relative = current.relative.isEmpty()
                ? entry.fileName()
                : current.relative + QLatin1Char('/') + entry.fileName();
            if (entry.isDir()) {
                if (isSkippedDirectory(entry.fileName())) {
                    scan.skippedEntries++;
                    continue;
                }
                scan.directoryCount++;
                pending.append({entry.absoluteFilePath(), relative, current.depth + 1});
                continue;
            }
            if (!entry.isFile())
                continue;

            if (filesSeen >= limits.maxFiles || scan.totalBytes >= limits.maxBytes) {
                scan.truncated = true;
                continue;
            }
            ++filesSeen;

            ProjectFile file;
            file.relativePath = relative;
            file.category = categoryForSuffix(entry.suffix());
            file.size = entry.size();
            if (file.category == FileCategory::Other) {
                if (buildNames.contains(entry.fileName(), Qt::CaseInsensitive))
                    file.category = FileCategory::Build;
                else if (entry.fileName().endsWith(QLatin1String(".uproject"),
                                                   Qt::CaseInsensitive)
                         || entry.fileName() == QLatin1String("project.godot"))
                    file.category = FileCategory::Build;
            }
            scan.totalBytes += file.size;
            scan.filesByCategory[static_cast<int>(file.category)]++;
            scan.bytesByCategory[static_cast<int>(file.category)] += file.size;
            if (file.category == FileCategory::Build
                && !scan.buildFiles.contains(file.relativePath))
                scan.buildFiles.append(file.relativePath);
            scan.files.append(file);
        }
        if (scan.cancelled)
            break;
    }

    std::sort(scan.files.begin(), scan.files.end(),
              [](const ProjectFile& left, const ProjectFile& right) {
                  return QString::compare(left.relativePath, right.relativePath,
                                          Qt::CaseInsensitive)
                      < 0;
              });

    if (scan.truncated) {
        scan.warnings.append(QStringLiteral("The scan stopped at the limit of %1 files; the "
                                            "inventory is a partial view.")
                                 .arg(limits.maxFiles));
    }
    if (scan.cancelled)
        scan.warnings.append(QStringLiteral("The scan was cancelled; the inventory is partial."));
    if (scan.files.isEmpty() && !scan.cancelled && scan.warnings.isEmpty())
        scan.warnings.append(QStringLiteral("The folder contains no readable files."));

    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("Scanned %1: %2 files, %3 folders")
                .arg(scan.displayName)
                .arg(scan.fileCount())
                .arg(scan.directoryCount));
    return scan;
}

QString signalAreaName(SignalArea area)
{
    switch (area) {
    case SignalArea::Platform:
        return QStringLiteral("Platform");
    case SignalArea::Graphics:
        return QStringLiteral("Graphics");
    case SignalArea::Audio:
        return QStringLiteral("Audio");
    case SignalArea::Input:
        return QStringLiteral("Input");
    case SignalArea::Threading:
        return QStringLiteral("Threading");
    case SignalArea::Build:
        break;
    }
    return QStringLiteral("Build");
}

int PortingReport::blockingCount() const
{
    static const QSet<QString> blocking = {
        QStringLiteral("win32"),      QStringLiteral("windows.loader"),
        QStringLiteral("posix"),      QStringLiteral("directx"),
        QStringLiteral("metal"),      QStringLiteral("xinput"),
        QStringLiteral("rawinput"),   QStringLiteral("xaudio"),
        QStringLiteral("coreaudio"),  QStringLiteral("pthread"),
    };
    int count = 0;
    for (const PortingFinding& finding : findings) {
        if (blocking.contains(finding.id))
            ++count;
    }
    return count;
}

PortingReport analyzePortability(const ProjectScan& scan, const CancelToken& token,
                                 const PortingLimits& limits)
{
    PortingReport report;
    if (!scan.readable()) {
        report.notes.append(QStringLiteral("Scan a project folder first."));
        return report;
    }

    QHash<QString, Aggregate> aggregates;
    const qint64 perFile = std::max<qint64>(1, limits.maxBytesPerFile);

    for (const ProjectFile& file : scan.files) {
        if (token.isCancelled()) {
            report.cancelled = true;
            break;
        }
        if (report.scannedFiles >= limits.maxFiles
            || report.scannedBytes >= limits.maxBytesTotal) {
            report.truncated = true;
            break;
        }
        if (!isTextScanCandidate(file.category, file.relativePath) || file.size <= 0) {
            ++report.skippedFiles;
            continue;
        }

        QFile handle(QDir(scan.rootPath).filePath(file.relativePath));
        if (!handle.open(QIODevice::ReadOnly)) {
            ++report.skippedFiles;
            continue;
        }
        const QByteArray raw = handle.read(std::min(file.size, perFile));
        if (raw.isEmpty() || looksBinary(raw)) {
            ++report.skippedFiles;
            continue;
        }
        ++report.scannedFiles;
        report.scannedBytes += raw.size();
        const QString content = QString::fromUtf8(raw);

        for (const TokenSpec& spec : kSpecs) {
            Aggregate& aggregate = aggregates[QString::fromLatin1(spec.id)];
            for (const char* tokenText : spec.tokens) {
                const QString needle = QString::fromLatin1(tokenText);
                qsizetype index = content.indexOf(needle, 0, Qt::CaseInsensitive);
                int matches = 0;
                while (index >= 0 && matches < kMaxMatchesPerTokenPerFile) {
                    ++matches;
                    if (aggregate.example.isEmpty())
                        aggregate.example = exampleLocation(file.relativePath, content, index);
                    index = content.indexOf(needle, index + needle.size(), Qt::CaseInsensitive);
                }
                aggregate.occurrences += matches;
            }
            if (aggregate.occurrences > 0
                && static_cast<int>(aggregate.files.size()) < kMaxExampleListed
                && !aggregate.files.contains(file.relativePath))
                aggregate.files.append(file.relativePath);
        }
    }

    for (const TokenSpec& spec : kSpecs) {
        const auto it = aggregates.constFind(QString::fromLatin1(spec.id));
        if (it == aggregates.constEnd() || it->occurrences <= 0)
            continue;
        PortingFinding finding;
        finding.id = QString::fromLatin1(spec.id);
        finding.label = QString::fromLatin1(spec.label);
        finding.area = spec.area;
        finding.occurrences = it->occurrences;
        finding.files = it->files;
        finding.fileCount = static_cast<int>(it->files.size());
        finding.example = it->example;
        finding.note = QString::fromLatin1(spec.note);
        report.findings.append(finding);
    }

    report.notes.append(QStringLiteral("These are textual matches in source-like files. They "
                                       "show where platform-specific code lives; they are not a "
                                       "build result and not a verdict on the port."));
    report.notes.append(
        QStringLiteral("Read %1 files (%2 KiB). Trimmed or generated sources, and files above "
                       "%3 KiB, were skipped.")
            .arg(report.scannedFiles)
            .arg(report.scannedBytes / 1024)
            .arg(perFile / 1024));
    if (report.skippedFiles > 0) {
        report.notes.append(QStringLiteral("%1 files were skipped (binary, too large or "
                                           "unreadable).")
                                .arg(report.skippedFiles));
    }
    if (report.truncated)
        report.notes.append(QStringLiteral("The analysis stopped at its file/byte limit; the "
                                           "report is partial."));
    if (report.cancelled)
        report.notes.append(QStringLiteral("The analysis was cancelled; the report is partial."));
    return report;
}

} // namespace pkg::gameforge
