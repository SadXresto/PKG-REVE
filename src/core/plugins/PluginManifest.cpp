#include "core/plugins/PluginManifest.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

namespace pkg::plugins {
namespace {

constexpr qint64 kManifestReadLimit = 512 * 1024;

QString sha256OfFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(256 * 1024);
        if (chunk.isEmpty())
            break;
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool isSafeRelative(const QString& path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path))
        return false;
    const QString normalized = QDir::cleanPath(path);
    return normalized != QLatin1String("..") && !normalized.startsWith(QStringLiteral("../"))
        && !normalized.contains(QStringLiteral("/../"));
}

QString sanitizedBundleName(const PluginManifest& manifest)
{
    QString name = manifest.id;
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("-"));
    QString version = manifest.version;
    version.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("-"));
    return QStringLiteral("%1-%2").arg(name, version);
}

ValidationIssue issue(const QString& field, const QString& message, bool warning = false)
{
    ValidationIssue result;
    result.field = field;
    result.message = message;
    result.warning = warning;
    return result;
}

}  // namespace

QJsonObject PluginDependency::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("minimumVersion"), minimumVersion);
    return object;
}

PluginDependency PluginDependency::fromJson(const QJsonObject& object)
{
    PluginDependency dependency;
    dependency.id = object.value(QStringLiteral("id")).toString();
    dependency.minimumVersion = object.value(QStringLiteral("minimumVersion")).toString();
    return dependency;
}

QStringList PluginManifest::knownPermissions()
{
    return {QStringLiteral("files.read"), QStringLiteral("files.write"),
            QStringLiteral("network"), QStringLiteral("process.spawn"),
            QStringLiteral("clipboard"), QStringLiteral("ai.prompt")};
}

QString PluginManifest::permissionDescription(const QString& permission)
{
    if (permission == QLatin1String("files.read"))
        return QStringLiteral("Read files the user explicitly opens or selects.");
    if (permission == QLatin1String("files.write"))
        return QStringLiteral("Write files the user explicitly saves.");
    if (permission == QLatin1String("network"))
        return QStringLiteral("Contact a network endpoint.");
    if (permission == QLatin1String("process.spawn"))
        return QStringLiteral("Start an external process.");
    if (permission == QLatin1String("clipboard"))
        return QStringLiteral("Read or write the clipboard.");
    if (permission == QLatin1String("ai.prompt"))
        return QStringLiteral("Send a prompt to the configured AI endpoint.");
    return QStringLiteral("Unknown permission.");
}

QString PluginManifest::supportedApiVersion()
{
    return QStringLiteral("1");
}

QJsonObject PluginManifest::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("version"), version);
    object.insert(QStringLiteral("apiVersion"), apiVersion);
    object.insert(QStringLiteral("description"), description);
    object.insert(QStringLiteral("author"), author);
    object.insert(QStringLiteral("entry"), entry);
    QJsonArray permissionArray;
    for (const QString& permission : permissions)
        permissionArray.append(permission);
    object.insert(QStringLiteral("permissions"), permissionArray);
    QJsonArray capabilityArray;
    for (const QString& capability : capabilities)
        capabilityArray.append(capability);
    object.insert(QStringLiteral("capabilities"), capabilityArray);
    QJsonArray dependencyArray;
    for (const PluginDependency& dependency : dependencies)
        dependencyArray.append(dependency.toJson());
    object.insert(QStringLiteral("dependencies"), dependencyArray);
    return object;
}

Result<PluginManifest> PluginManifest::fromJson(const QJsonObject& object)
{
    if (object.isEmpty())
        return makeError(QStringLiteral("The plugin manifest is empty"));
    PluginManifest manifest;
    manifest.id = object.value(QStringLiteral("id")).toString();
    manifest.name = object.value(QStringLiteral("name")).toString();
    manifest.version = object.value(QStringLiteral("version")).toString();
    manifest.apiVersion = object.value(QStringLiteral("apiVersion")).toString();
    manifest.description = object.value(QStringLiteral("description")).toString();
    manifest.author = object.value(QStringLiteral("author")).toString();
    manifest.entry = object.value(QStringLiteral("entry")).toString();
    const QJsonArray permissions = object.value(QStringLiteral("permissions")).toArray();
    for (const QJsonValue& value : permissions)
        manifest.permissions.append(value.toString());
    const QJsonArray capabilities = object.value(QStringLiteral("capabilities")).toArray();
    for (const QJsonValue& value : capabilities)
        manifest.capabilities.append(value.toString());
    const QJsonArray dependencies = object.value(QStringLiteral("dependencies")).toArray();
    for (const QJsonValue& value : dependencies) {
        if (value.isObject())
            manifest.dependencies.append(PluginDependency::fromJson(value.toObject()));
    }
    return manifest;
}

bool isValidPluginId(const QString& id)
{
    static const QRegularExpression expression(
        QStringLiteral("^[a-z0-9][a-z0-9._-]{1,63}$"));
    return expression.match(id).hasMatch();
}

bool isValidSemver(const QString& version)
{
    static const QRegularExpression expression(
        QStringLiteral("^(0|[1-9]\\d*)\\.(0|[1-9]\\d*)\\.(0|[1-9]\\d*)"
                       "(?:-[0-9A-Za-z.-]+)?(?:\\+[0-9A-Za-z.-]+)?$"));
    return expression.match(version).hasMatch();
}

int compareSemver(const QString& left, const QString& right)
{
    auto parts = [](QString version) {
        const int suffix = static_cast<int>(
            version.indexOf(QRegularExpression(QStringLiteral("[-+]"))));
        if (suffix >= 0)
            version = version.left(suffix);
        const QStringList numbers = version.split(QLatin1Char('.'));
        QVector<int> result{0, 0, 0};
        for (int i = 0; i < numbers.size() && i < 3; ++i)
            result[i] = numbers.at(i).toInt();
        return result;
    };
    if (!isValidSemver(left) || !isValidSemver(right))
        return 0;
    const QVector<int> a = parts(left);
    const QVector<int> b = parts(right);
    for (int i = 0; i < 3; ++i) {
        if (a.at(i) < b.at(i))
            return -1;
        if (a.at(i) > b.at(i))
            return 1;
    }
    return 0;
}

bool ValidationReport::valid() const
{
    for (const ValidationIssue& issueValue : issues) {
        if (!issueValue.warning)
            return false;
    }
    return true;
}

QStringList ValidationReport::errors() const
{
    QStringList result;
    for (const ValidationIssue& issueValue : issues) {
        if (!issueValue.warning)
            result.append(QStringLiteral("%1: %2").arg(issueValue.field, issueValue.message));
    }
    return result;
}

QStringList ValidationReport::warnings() const
{
    QStringList result;
    for (const ValidationIssue& issueValue : issues) {
        if (issueValue.warning)
            result.append(QStringLiteral("%1: %2").arg(issueValue.field, issueValue.message));
    }
    return result;
}

QString ValidationReport::summary() const
{
    if (valid() && warnings().isEmpty())
        return QStringLiteral("Manifest is valid; no warnings.");
    if (valid())
        return QStringLiteral("Manifest is valid with %1 warning(s).").arg(warnings().size());
    return QStringLiteral("%1 error(s), %2 warning(s).")
        .arg(errors().size())
        .arg(warnings().size());
}

ValidationReport validateManifest(const PluginManifest& manifest,
                                  const QString& pluginDirectory,
                                  const QVector<PluginManifest>& installed)
{
    ValidationReport report;
    if (!isValidPluginId(manifest.id)) {
        report.issues.append(issue(QStringLiteral("id"),
                                   QStringLiteral("Use 2-64 lowercase letters, digits, dots, "
                                                  "hyphens or underscores, starting with a letter "
                                                  "or digit.")));
    }
    if (manifest.name.trimmed().isEmpty())
        report.issues.append(issue(QStringLiteral("name"), QStringLiteral("Name is required.")));
    if (!isValidSemver(manifest.version)) {
        report.issues.append(issue(QStringLiteral("version"),
                                   QStringLiteral("Version must be MAJOR.MINOR.PATCH, for "
                                                  "example 0.1.0.")));
    }
    if (manifest.apiVersion != PluginManifest::supportedApiVersion()) {
        report.issues.append(issue(
            QStringLiteral("apiVersion"),
            QStringLiteral("Unsupported API version '%1'; this build supports %2.")
                .arg(manifest.apiVersion, PluginManifest::supportedApiVersion())));
    }
    if (manifest.entry.trimmed().isEmpty()) {
        report.issues.append(issue(QStringLiteral("entry"),
                                   QStringLiteral("An entry file is required.")));
    } else if (!isSafeRelative(manifest.entry)) {
        report.issues.append(issue(QStringLiteral("entry"),
                                   QStringLiteral("The entry must be a relative path inside the "
                                                  "plugin folder.")));
    } else if (!pluginDirectory.isEmpty()) {
        const QFileInfo entryInfo(QDir(pluginDirectory).filePath(manifest.entry));
        if (!entryInfo.isFile()) {
            report.issues.append(issue(QStringLiteral("entry"),
                                       QStringLiteral("Entry file not found: %1")
                                           .arg(manifest.entry)));
        } else if (entryInfo.size() > kManifestReadLimit) {
            report.issues.append(issue(QStringLiteral("entry"),
                                       QStringLiteral("Entry file is larger than 512 KiB.")));
        } else if (manifest.entry.endsWith(QLatin1String(".json"))) {
            QFile file(entryInfo.absoluteFilePath());
            if (file.open(QIODevice::ReadOnly)) {
                const QByteArray data = file.read(kManifestReadLimit);
                QJsonParseError parseError{};
                QJsonDocument::fromJson(data, &parseError);
                if (parseError.error != QJsonParseError::NoError) {
                    report.issues.append(issue(
                        QStringLiteral("entry"),
                        QStringLiteral("Entry JSON is malformed at offset %1: %2")
                            .arg(parseError.offset)
                            .arg(parseError.errorString())));
                }
            }
        }
    }

    QStringList seenPermissions;
    for (const QString& permission : manifest.permissions) {
        if (!PluginManifest::knownPermissions().contains(permission)) {
            report.issues.append(issue(QStringLiteral("permissions"),
                                       QStringLiteral("Unknown permission '%1'. Known: %2")
                                           .arg(permission,
                                                PluginManifest::knownPermissions()
                                                    .join(QStringLiteral(", ")))));
        }
        if (seenPermissions.contains(permission)) {
            report.issues.append(issue(QStringLiteral("permissions"),
                                       QStringLiteral("Permission '%1' is declared twice.")
                                           .arg(permission),
                                       true));
        }
        seenPermissions.append(permission);
        if (permission == QLatin1String("process.spawn")) {
            report.issues.append(issue(
                QStringLiteral("permissions"),
                QStringLiteral("process.spawn is declared, but this build never executes "
                               "plugin code; the declaration is documentation only."),
                true));
        }
        if (permission == QLatin1String("files.write")) {
            report.issues.append(issue(
                QStringLiteral("permissions"),
                QStringLiteral("files.write is declared; the host never grants write access to "
                               "plugin code in this build."),
                true));
        }
    }

    QStringList dependencyIds;
    for (const PluginDependency& dependency : manifest.dependencies) {
        if (!isValidPluginId(dependency.id)) {
            report.issues.append(issue(QStringLiteral("dependencies"),
                                       QStringLiteral("Invalid dependency id '%1'.")
                                           .arg(dependency.id)));
            continue;
        }
        if (dependency.id == manifest.id) {
            report.issues.append(issue(QStringLiteral("dependencies"),
                                       QStringLiteral("A plugin cannot depend on itself.")));
        }
        if (!isValidSemver(dependency.minimumVersion)) {
            report.issues.append(issue(
                QStringLiteral("dependencies"),
                QStringLiteral("Dependency '%1' needs a MAJOR.MINOR.PATCH minimumVersion.")
                    .arg(dependency.id)));
        }
        if (dependencyIds.contains(dependency.id)) {
            report.issues.append(issue(QStringLiteral("dependencies"),
                                       QStringLiteral("Dependency '%1' is listed twice.")
                                           .arg(dependency.id),
                                       true));
        }
        dependencyIds.append(dependency.id);
        // An empty installed list still means the dependency is missing; that is
        // a real validation error, not an unknown to skip.
        bool found = false;
        for (const PluginManifest& other : installed) {
            if (other.id != dependency.id)
                continue;
            found = true;
            if (isValidSemver(other.version) && isValidSemver(dependency.minimumVersion)
                && compareSemver(other.version, dependency.minimumVersion) < 0) {
                report.issues.append(issue(
                    QStringLiteral("dependencies"),
                    QStringLiteral("Installed %1 %2 is older than the required %3.")
                        .arg(dependency.id, other.version, dependency.minimumVersion)));
            }
            break;
        }
        if (!found) {
            report.issues.append(issue(
                QStringLiteral("dependencies"),
                QStringLiteral("Dependency '%1' is not installed.").arg(dependency.id)));
        }
    }

    for (const QString& capability : manifest.capabilities) {
        if (capability.trimmed().isEmpty()) {
            report.issues.append(issue(QStringLiteral("capabilities"),
                                       QStringLiteral("Empty capability name.")));
        }
    }

    return report;
}

Result<PluginManifest> loadManifest(const QString& pluginJsonPath)
{
    QFile file(pluginJsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return makeError(QStringLiteral("Cannot open the plugin manifest"), pluginJsonPath);
    const QByteArray data = file.read(kManifestReadLimit + 1);
    if (data.size() > kManifestReadLimit)
        return makeError(QStringLiteral("The plugin manifest is larger than 512 KiB"));
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return makeError(QStringLiteral("Malformed plugin manifest"),
                         QStringLiteral("%1 at offset %2")
                             .arg(parseError.errorString())
                             .arg(parseError.offset));
    }
    if (!document.isObject())
        return makeError(QStringLiteral("The plugin manifest must be a JSON object"));
    return PluginManifest::fromJson(document.object());
}

Status saveManifest(const QString& pluginJsonPath, const PluginManifest& manifest)
{
    const QFileInfo info(pluginJsonPath);
    if (!QDir().mkpath(info.absolutePath()))
        return makeError(QStringLiteral("Cannot create the plugin folder"), info.absolutePath());
    QFile file(pluginJsonPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return makeError(QStringLiteral("Cannot write the plugin manifest"), pluginJsonPath);
    const QByteArray data = QJsonDocument(manifest.toJson()).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size())
        return makeError(QStringLiteral("The plugin manifest was only partly written"),
                         pluginJsonPath);
    file.flush();
    return okStatus();
}

QMap<QString, QString> examplePluginFiles()
{
    QMap<QString, QString> files;
    files.insert(QStringLiteral("plugin.json"),
                 QStringLiteral("{\n"
                                "  \"id\": \"com.example.hello-toolkit\",\n"
                                "  \"name\": \"Hello Toolkit\",\n"
                                "  \"version\": \"0.1.0\",\n"
                                "  \"apiVersion\": \"1\",\n"
                                "  \"description\": \"Declarative example plugin used by the "
                                "Extension Studio.\",\n"
                                "  \"author\": \"Your name\",\n"
                                "  \"entry\": \"actions.json\",\n"
                                "  \"permissions\": [\"files.read\"],\n"
                                "  \"capabilities\": [\"declarative-commands\"],\n"
                                "  \"dependencies\": []\n"
                                "}\n"));
    files.insert(QStringLiteral("actions.json"),
                 QStringLiteral("{\n"
                                "  \"version\": 1,\n"
                                "  \"note\": \"Declarative only. PKG Studio 0.11.0 validates and "
                                "packages plugins but does not execute plugin code.\",\n"
                                "  \"actions\": [\n"
                                "    {\n"
                                "      \"id\": \"hello.inspect\",\n"
                                "      \"title\": \"Inspect the active file\",\n"
                                "      \"hostCommand\": \"file.hashSha256\",\n"
                                "      \"requires\": \"files.read\"\n"
                                "    }\n"
                                "  ]\n"
                                "}\n"));
    files.insert(QStringLiteral("README.md"), sdkReadme());
    return files;
}

QString sdkReadme()
{
    return QStringLiteral(
        "# PKG Studio plugin SDK (API version 1)\n"
        "\n"
        "A plugin is a folder with `plugin.json`, a declarative entry file and optional assets.\n"
        "\n"
        "## Manifest\n"
        "\n"
        "- `id`: 2-64 lowercase letters, digits, dots, hyphens or underscores.\n"
        "- `name`, `description`, `author`: shown in the Extension Studio.\n"
        "- `version` and dependency `minimumVersion`: semantic version (`MAJOR.MINOR.PATCH`).\n"
        "- `apiVersion`: must be `1` in this build.\n"
        "- `entry`: a relative file inside the plugin folder. JSON entries are parsed during\n"
        "  validation; nothing is executed.\n"
        "- `permissions`: a subset of `files.read`, `files.write`, `network`, `process.spawn`,\n"
        "  `clipboard`, `ai.prompt`. Declaring a permission documents intent only.\n"
        "- `dependencies`: plugin ids with a minimum version; validation checks installed\n"
        "  manifests.\n"
        "\n"
        "## What this build does\n"
        "\n"
        "The Extension Studio validates manifests, checks versions and dependencies, audits\n"
        "permissions, runs the validation workflow and exports a checksummed bundle. It does\n"
        "not load, link, interpret or execute plugin code, and no permission is granted at\n"
        "runtime. A plugin cannot read files, open sockets or start processes through PKG\n"
        "Studio in this version.\n"
        "\n"
        "## Packaging\n"
        "\n"
        "Export copies the folder into `<id>-<version>/`, writes `manifest.json` and\n"
        "`SHA256SUMS.txt`, and reports every file's size and digest. Files above 25 MiB are\n"
        "skipped and named in the report.\n");
}

Result<PackageResult> exportBundle(const PluginManifest& manifest,
                                   const QString& sourceDirectory,
                                   const QString& destinationRoot,
                                   const QString& checksumFile, const CancelToken& token,
                                   qint64 maxFileBytes)
{
    if (destinationRoot.trimmed().isEmpty())
        return makeError(QStringLiteral("Choose a destination folder"));
    if (!QFileInfo(sourceDirectory).isDir())
        return makeError(QStringLiteral("The plugin folder does not exist"), sourceDirectory);

    const ValidationReport validation = validateManifest(manifest, sourceDirectory);
    if (!validation.valid()) {
        return makeError(QStringLiteral("The plugin manifest is not valid"),
                         validation.errors().join(QStringLiteral("; ")));
    }

    const QString bundleName = sanitizedBundleName(manifest);
    const QString targetPath = QDir(destinationRoot).filePath(bundleName);
    if (QFileInfo::exists(targetPath)) {
        return makeError(QStringLiteral("The export folder already exists"),
                         QStringLiteral("%1 (remove or rename it first)").arg(targetPath));
    }
    if (!QDir().mkpath(targetPath))
        return makeError(QStringLiteral("Cannot create the export folder"), targetPath);

    PackageResult result;
    result.directory = targetPath;
    result.checksumFile = QDir(targetPath).filePath(
        checksumFile.isEmpty() ? QStringLiteral("SHA256SUMS.txt") : checksumFile);

    const QDir source(sourceDirectory);
    QDirIterator it(sourceDirectory, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (token.isCancelled()) {
            return makeError(QStringLiteral("Packaging was cancelled"),
                             QStringLiteral("The partial bundle remains at %1").arg(targetPath));
        }
        const QString path = it.next();
        const QString relative = source.relativeFilePath(path);
        if (!isSafeRelative(relative))
            continue;
        if (relative.startsWith(QStringLiteral(".git/"))
            || relative.startsWith(QStringLiteral("build/")))
            continue;
        const QFileInfo info(path);
        if (info.size() > maxFileBytes) {
            result.skippedLargeFile = true;
            continue;
        }
        const QString destination = QDir(targetPath).filePath(relative);
        if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
            return makeError(QStringLiteral("Cannot create a bundle subfolder"),
                             QFileInfo(destination).absolutePath());
        }
        if (!QFile::copy(path, destination)) {
            return makeError(QStringLiteral("Cannot copy a plugin file"), relative);
        }
        PackageFile packaged;
        packaged.relativePath = relative;
        packaged.size = info.size();
        packaged.sha256 = sha256OfFile(destination);
        result.files.append(packaged);
        result.totalBytes += info.size();
    }

    // The manifest written into the bundle is normalised to the validated model.
    QFile manifestFile(QDir(targetPath).filePath(QStringLiteral("manifest.json")));
    if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return makeError(QStringLiteral("Cannot write the packaged manifest"));
    }
    manifestFile.write(QJsonDocument(manifest.toJson()).toJson(QJsonDocument::Indented));
    manifestFile.close();

    if (!QFileInfo::exists(QDir(targetPath).filePath(QStringLiteral("README.md")))) {
        QFile readme(QDir(targetPath).filePath(QStringLiteral("README.md")));
        if (readme.open(QIODevice::WriteOnly | QIODevice::Truncate))
            readme.write(sdkReadme().toUtf8());
    }

    QString checksums;
    for (const PackageFile& file : result.files)
        checksums += QStringLiteral("%1  %2\n").arg(file.sha256, file.relativePath);
    QFile checksum(result.checksumFile);
    if (!checksum.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return makeError(QStringLiteral("Cannot write the checksum file"), result.checksumFile);
    checksum.write(checksums.toUtf8());
    checksum.close();

    std::sort(result.files.begin(), result.files.end(),
              [](const PackageFile& a, const PackageFile& b) {
                  return a.relativePath < b.relativePath;
              });
    return result;
}

}  // namespace pkg::plugins
