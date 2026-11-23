// Plugin SDK data model: manifests, permissions, dependency checks, validation
// and safe packaging.
//
// PKG Studio 0.11.0 does not execute plugin code. A plugin is a declarative
// manifest plus files; the Extension Studio validates, inspects and packages
// it. That limitation is deliberate and stated in the SDK documentation, so
// nothing here pretends to sandbox native code.
#pragma once

#include "core/CancelToken.h"
#include "core/Result.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace pkg::plugins {

struct PluginDependency {
    QString id;
    QString minimumVersion;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static PluginDependency fromJson(const QJsonObject& object);
};

struct PluginManifest {
    QString id;
    QString name;
    QString version = QStringLiteral("0.1.0");
    QString apiVersion = QStringLiteral("1");
    QString description;
    QString author;
    /// Declarative entry file, relative to the plugin folder. Never executed.
    QString entry = QStringLiteral("plugin.json");
    /// Permission ids the plugin declares. All are off unless declared.
    QStringList permissions;
    QStringList capabilities;
    QVector<PluginDependency> dependencies;

    [[nodiscard]] static QStringList knownPermissions();
    [[nodiscard]] static QString permissionDescription(const QString& permission);
    [[nodiscard]] static QString supportedApiVersion();

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static Result<PluginManifest> fromJson(const QJsonObject& object);
};

[[nodiscard]] bool isValidPluginId(const QString& id);
[[nodiscard]] bool isValidSemver(const QString& version);
/// -1 when `left` < `right`, 0 when equal, 1 when greater. Invalid versions
/// compare as 0 and should be rejected by validation before use.
[[nodiscard]] int compareSemver(const QString& left, const QString& right);

struct ValidationIssue {
    QString field;
    QString message;
    bool warning = false;
};

struct ValidationReport {
    QVector<ValidationIssue> issues;
    [[nodiscard]] bool valid() const;
    [[nodiscard]] QStringList errors() const;
    [[nodiscard]] QStringList warnings() const;
    [[nodiscard]] QString summary() const;
};

/// Checks the manifest itself plus the plugin folder (`entry` exists, stays
/// inside the folder, and JSON entries parse). Installed manifests are used for
/// dependency and duplicate-id checks.
[[nodiscard]] ValidationReport validateManifest(const PluginManifest& manifest,
                                                const QString& pluginDirectory,
                                                const QVector<PluginManifest>& installed = {});

[[nodiscard]] Result<PluginManifest> loadManifest(const QString& pluginJsonPath);
[[nodiscard]] Status saveManifest(const QString& pluginJsonPath, const PluginManifest& manifest);

/// Files written by "Create example plugin": relative path -> content.
[[nodiscard]] QMap<QString, QString> examplePluginFiles();
[[nodiscard]] QString sdkReadme();

struct PackageFile {
    QString relativePath;
    qint64 size = 0;
    QString sha256;
};

struct PackageResult {
    QString directory;
    QString checksumFile;
    QVector<PackageFile> files;
    qint64 totalBytes = 0;
    bool skippedLargeFile = false;
};

/// Copies the plugin folder into `destinationRoot/<id>-<version>/`, writes a
/// manifest.json and SHA256SUMS.txt, and refuses paths that escape the folder.
/// Nothing is executed. `sourceDirectory` is never modified.
[[nodiscard]] Result<PackageResult> exportBundle(const PluginManifest& manifest,
                                                 const QString& sourceDirectory,
                                                 const QString& destinationRoot,
                                                 const QString& checksumFile = {},
                                                 const CancelToken& token = {},
                                                 qint64 maxFileBytes = 25LL * 1024 * 1024);

}  // namespace pkg::plugins
