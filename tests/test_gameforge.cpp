// GameForge project scanner and portability analyzer.
#include "core/gameforge/GameProject.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace pkg;
using pkg::gameforge::FileCategory;
using pkg::gameforge::ProjectKind;

namespace {

void writeFile(const QString& path, const QByteArray& contents)
{
    const QFileInfo info(path);
    ASSERT_TRUE(QDir().mkpath(info.absolutePath()));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(contents), contents.size());
}

const gameforge::ProjectFile* findFile(const gameforge::ProjectScan& scan,
                                      const QString& relativePath)
{
    for (const auto& file : scan.files) {
        if (file.relativePath == relativePath)
            return &file;
    }
    return nullptr;
}

} // namespace

TEST(GameForge, MissingFolderIsReportedNotGuessed)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto scan = gameforge::scanProject(dir.filePath(QStringLiteral("does-not-exist")));
    EXPECT_FALSE(scan.readable());
    EXPECT_EQ(scan.kind, ProjectKind::Unknown);
    EXPECT_EQ(scan.fileCount(), 0);
    EXPECT_FALSE(scan.warnings.isEmpty());
}

TEST(GameForge, CmakeProjectInventory)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("CMakeLists.txt")),
              QByteArray("cmake_minimum_required(VERSION 3.21)\n"));
    writeFile(dir.filePath(QStringLiteral("src/main.cpp")),
              QByteArray("#include <windows.h>\n#include <SDL.h>\nint main() { SDL_Init(0); }\n"));
    writeFile(dir.filePath(QStringLiteral("src/Game.h")), QByteArray("struct Game {};\n"));
    writeFile(dir.filePath(QStringLiteral("assets/icon.png")),
              QByteArray("\x89PNG\r\n\x1a\nwindows.h", 17));
    writeFile(dir.filePath(QStringLiteral("README.md")), QByteArray("# Game\n"));
    // Build output folders are skipped so the inventory stays about the project.
    writeFile(dir.filePath(QStringLiteral("build/cache.o")), QByteArray("binary"));

    const auto scan = gameforge::scanProject(dir.path());
    ASSERT_TRUE(scan.readable());
    EXPECT_EQ(scan.kind, ProjectKind::CMake);
    EXPECT_EQ(scan.kindEvidence, QStringLiteral("CMakeLists.txt"));
    EXPECT_EQ(scan.fileCount(), 5);
    EXPECT_EQ(scan.directoryCount, 2); // src, assets
    EXPECT_FALSE(scan.truncated);

    const auto* mainFile = findFile(scan, QStringLiteral("src/main.cpp"));
    ASSERT_NE(mainFile, nullptr);
    EXPECT_EQ(mainFile->category, FileCategory::Source);
    EXPECT_GT(mainFile->size, 0);

    const auto* icon = findFile(scan, QStringLiteral("assets/icon.png"));
    ASSERT_NE(icon, nullptr);
    EXPECT_EQ(icon->category, FileCategory::Texture);

    EXPECT_EQ(findFile(scan, QStringLiteral("build/cache.o")), nullptr);
    EXPECT_TRUE(scan.buildFiles.contains(QStringLiteral("CMakeLists.txt")));

    qint64 sum = 0;
    for (const auto& file : scan.files)
        sum += file.size;
    EXPECT_EQ(scan.totalBytes, sum);
    EXPECT_EQ(scan.filesByCategory.value(static_cast<int>(FileCategory::Source)), 1);
}

TEST(GameForge, DetectsUnityAndGodotLayouts)
{
    QTemporaryDir unity;
    ASSERT_TRUE(unity.isValid());
    writeFile(unity.filePath(QStringLiteral("Assets/Scripts/Player.cs")),
              QByteArray("using UnityEngine;\nclass Player : MonoBehaviour {}\n"));
    writeFile(unity.filePath(QStringLiteral("ProjectSettings/ProjectVersion.txt")),
              QByteArray("m_EditorVersion: 2022.3\n"));
    const auto unityScan = gameforge::scanProject(unity.path());
    EXPECT_EQ(unityScan.kind, ProjectKind::Unity);
    EXPECT_EQ(unityScan.kindEvidence, QStringLiteral("Assets/ + ProjectSettings/"));

    QTemporaryDir godot;
    ASSERT_TRUE(godot.isValid());
    writeFile(godot.filePath(QStringLiteral("project.godot")), QByteArray("config_version=5\n"));
    writeFile(godot.filePath(QStringLiteral("main.gd")), QByteArray("extends Node\n"));
    const auto godotScan = gameforge::scanProject(godot.path());
    EXPECT_EQ(godotScan.kind, ProjectKind::Godot);
}

TEST(GameForge, LimitsAndCancellationLeavePartialScan)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (int i = 0; i < 6; ++i) {
        writeFile(dir.filePath(QStringLiteral("f%1.txt").arg(i)), QByteArray("x"));
    }

    gameforge::ScanLimits limits;
    limits.maxFiles = 2;
    const auto limited = gameforge::scanProject(dir.path(), {}, limits);
    EXPECT_EQ(limited.fileCount(), 2);
    EXPECT_TRUE(limited.truncated);
    EXPECT_FALSE(limited.warnings.isEmpty());

    CancelToken token;
    token.cancel();
    const auto cancelled = gameforge::scanProject(dir.path(), token);
    EXPECT_TRUE(cancelled.cancelled);
    EXPECT_TRUE(cancelled.fileCount() <= 6);
}

TEST(GameForge, PortabilityFindingsCarryEvidence)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath(QStringLiteral("CMakeLists.txt")), QByteArray("project(Game)\n"));
    writeFile(dir.filePath(QStringLiteral("src/window.cpp")),
              QByteArray("// platform layer\n#include <windows.h>\n"
                         "HWND hwnd = CreateWindowEx(0, L\"x\", L\"y\", 0, 0, 0, 0, 0, 0, 0, 0);\n"
                         "SDL_Init(SDL_INIT_VIDEO);\n"));
    // A texture containing API names must never be read as source.
    writeFile(dir.filePath(QStringLiteral("assets/tex.png")),
              QByteArray("\x89PNG\r\n\x1a\nd3d11 XAudio2", 20));

    const auto scan = gameforge::scanProject(dir.path());
    ASSERT_TRUE(scan.readable());
    const auto report = gameforge::analyzePortability(scan);
    EXPECT_GE(report.scannedFiles, 1);
    EXPECT_FALSE(report.findings.isEmpty());
    EXPECT_FALSE(report.notes.isEmpty());
    EXPECT_GT(report.blockingCount(), 0);

    bool sawWin32 = false;
    bool sawSdl = false;
    for (const auto& finding : report.findings) {
        EXPECT_FALSE(finding.note.isEmpty());
        EXPECT_GT(finding.occurrences, 0);
        if (finding.id == QLatin1String("win32")) {
            sawWin32 = true;
            EXPECT_EQ(finding.files.first(), QStringLiteral("src/window.cpp"));
            EXPECT_TRUE(finding.example.startsWith(QStringLiteral("src/window.cpp:")));
            EXPECT_LT(finding.example.count(QLatin1Char(':')), 3);
        }
        if (finding.id == QLatin1String("sdl"))
            sawSdl = true;
        EXPECT_NE(finding.id, QLatin1String("directx")) << "textures must not be scanned";
    }
    EXPECT_TRUE(sawWin32);
    EXPECT_TRUE(sawSdl);

    // Nothing was written: the project is read-only.
    EXPECT_TRUE(QFileInfo::exists(dir.filePath(QStringLiteral("src/window.cpp"))));
}

TEST(GameForge, PortabilityOfUnscannedFolderIsEmpty)
{
    gameforge::ProjectScan empty;
    const auto report = gameforge::analyzePortability(empty);
    EXPECT_TRUE(report.findings.isEmpty());
    EXPECT_FALSE(report.notes.isEmpty());
}
