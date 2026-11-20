#include "ai/LocalModels.h"

#include "core/FormatUtil.h"
#include "core/ProcessInfo.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTcpSocket>

#include <algorithm>

namespace pkg {
namespace {

/// Cap on manifest files read per folder, so a huge library cannot turn the
/// scan into a long disk job.
constexpr int kMaxManifests = 400;
/// Per-request budget on a local socket. A closed port fails immediately; this
/// only bounds a server that accepts the connection and then stalls.
constexpr int kRequestTimeoutMs = 900;

struct HttpResponse {
    int status = 0;
    QByteArray body;
};

QByteArray dechunked(const QByteArray& data)
{
    QByteArray out;
    qsizetype position = 0;
    while (position < data.size()) {
        const qsizetype lineEnd = data.indexOf("\r\n", position);
        if (lineEnd < 0)
            break;
        bool ok = false;
        const int size = data.mid(position, lineEnd - position).trimmed().toInt(&ok, 16);
        if (!ok || size <= 0)
            break;
        out += data.mid(lineEnd + 2, size);
        position = lineEnd + 2 + size + 2;
    }
    return out;
}

HttpResponse httpGet(quint16 port, const QByteArray& path)
{
    HttpResponse response;
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!socket.waitForConnected(200))
        return response;

    socket.write("GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                 "Accept: application/json\r\nConnection: close\r\n\r\n");
    socket.flush();

    QByteArray raw;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < kRequestTimeoutMs) {
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(50)) {
            if (socket.state() == QAbstractSocket::UnconnectedState)
                break;
            continue;
        }
        raw += socket.readAll();
    }
    raw += socket.readAll();
    socket.abort();

    const qsizetype headerEnd = raw.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return response;

    const QByteArray head = raw.left(headerEnd);
    const QList<QByteArray> statusParts = head.left(head.indexOf("\r\n")).split(' ');
    if (statusParts.size() >= 2)
        response.status = statusParts.at(1).toInt();
    response.body = raw.mid(headerEnd + 4);
    if (head.toLower().contains("transfer-encoding: chunked"))
        response.body = dechunked(response.body);
    return response;
}

void addModel(QVector<LocalModelInfo>& models, const QString& rawName, quint64 sizeBytes,
              const QString& source)
{
    const QString name = rawName.trimmed();
    if (name.isEmpty())
        return;
    for (LocalModelInfo& existing : models) {
        if (existing.name == name) {
            existing.sizeBytes = std::max(existing.sizeBytes, sizeBytes);
            return;
        }
    }
    models.append(LocalModelInfo{name, sizeBytes, source});
}

quint64 manifestBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    const QJsonObject manifest = QJsonDocument::fromJson(file.read(256 * 1024)).object();
    quint64 total = 0;
    for (const QJsonValue& layer : manifest.value(QStringLiteral("layers")).toArray())
        total += static_cast<quint64>(layer.toObject().value(QStringLiteral("size")).toDouble());
    total += static_cast<quint64>(
        manifest.value(QStringLiteral("config")).toObject().value(QStringLiteral("size")).toDouble());
    return total;
}

struct RespondingServer {
    QString endpoint;
    QString providerName;
    quint16 port = 0;
    QVector<LocalModelInfo> models;
};

}  // namespace

QVector<LocalModelInfo> modelsFromOllamaApiResponse(const QByteArray& body)
{
    QVector<LocalModelInfo> models;
    const QJsonObject object = QJsonDocument::fromJson(body).object();

    const auto readArray = [&models](const QJsonArray& array) {
        for (const QJsonValue& value : array) {
            quint64 sizeBytes = 0;
            QString name;
            if (value.isString()) {
                name = value.toString();
            } else {
                const QJsonObject entry = value.toObject();
                name = entry.value(QStringLiteral("id")).toString();
                if (name.isEmpty())
                    name = entry.value(QStringLiteral("name")).toString();
                if (name.isEmpty())
                    name = entry.value(QStringLiteral("model")).toString();
                sizeBytes =
                    static_cast<quint64>(entry.value(QStringLiteral("size")).toDouble());
            }
            addModel(models, name, sizeBytes, QStringLiteral("endpoint"));
        }
    };
    // Ollama's /api/tags answers {"models": [...]}; OpenAI-compatible servers
    // answer {"data": [...]}. Both are accepted so one parser covers either.
    readArray(object.value(QStringLiteral("models")).toArray());
    readArray(object.value(QStringLiteral("data")).toArray());
    return models;
}

QVector<LocalModelInfo> modelsFromOllamaModelDirectory(const QString& modelsDirectory)
{
    QVector<LocalModelInfo> models;
    const QDir manifests(QDir(modelsDirectory).filePath(QStringLiteral("manifests")));
    if (!manifests.exists())
        return models;

    // manifests/<registry>/<namespace>/<model>/<tag>
    const QFileInfoList registries =
        manifests.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& registry : registries) {
        const QString registryName = registry.fileName();
        const QDir namespaces(registry.absoluteFilePath());
        for (const QFileInfo& namespaceInfo :
             namespaces.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString namespaceName = namespaceInfo.fileName();
            const QDir modelNames(namespaceInfo.absoluteFilePath());
            for (const QFileInfo& modelInfo :
                 modelNames.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                const QDir tags(modelInfo.absoluteFilePath());
                for (const QFileInfo& tagInfo :
                     tags.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
                    if (models.size() >= kMaxManifests)
                        return models;
                    const bool official = registryName == QLatin1String("registry.ollama.ai");
                    const QString base =
                        official && namespaceName == QLatin1String("library")
                            ? modelInfo.fileName()
                            : namespaceName + QLatin1Char('/') + modelInfo.fileName();
                    const QString name = official
                                             ? base + QLatin1Char(':') + tagInfo.fileName()
                                             : registryName + QLatin1Char('/') + base
                                                   + QLatin1Char(':') + tagInfo.fileName();
                    addModel(models, name, manifestBytes(tagInfo.absoluteFilePath()),
                             modelsDirectory);
                }
            }
        }
    }
    return models;
}

QStringList ollamaModelDirectories()
{
    QStringList directories;
    const auto add = [&directories](const QString& path) {
        if (path.trimmed().isEmpty())
            return;
        const QString clean = QDir::cleanPath(path);
        if (!directories.contains(clean))
            directories.append(clean);
    };

    add(qEnvironmentVariable("OLLAMA_MODELS"));
    const QString home = QDir::homePath();
    add(QDir(home).filePath(QStringLiteral(".ollama/models")));
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty())
        add(localAppData + QStringLiteral("/Ollama/models"));
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty())
        add(appData + QStringLiteral("/Ollama/models"));
    const QString programData = qEnvironmentVariable("ProgramData");
    if (!programData.isEmpty())
        add(programData + QStringLiteral("/ollama/models"));

    // Every mounted drive is checked for the same handful of names, so an
    // Ollama folder on D: or E: is found without walking whole disks.
    for (const QFileInfo& drive : QDir::drives()) {
        const QString root = drive.absoluteFilePath();
        add(root + QStringLiteral("ollama/models"));
        add(root + QStringLiteral(".ollama/models"));
        add(root + QStringLiteral("Ollama/models"));
        add(root + QStringLiteral("ProgramData/ollama/models"));
        add(root + QStringLiteral("tools/ollama/models"));
    }
    return directories;
}

QString modelFitHint(quint64 modelBytes, quint64 ramBytes)
{
    if (modelBytes == 0 || ramBytes == 0)
        return {};
    const double modelGb = static_cast<double>(modelBytes) / 1e9;
    const double ramGb = static_cast<double>(ramBytes) / 1e9;
    const QString size = QStringLiteral("%1 GB").arg(modelGb, 0, 'f', 1);
    const QString memory = QStringLiteral("%1 GB RAM").arg(ramGb, 0, 'f', 0);
    if (modelBytes * 5 <= ramBytes * 3)
        return QStringLiteral("needs about %1 and fits comfortably in %2").arg(size, memory);
    if (modelBytes * 10 <= ramBytes * 9)
        return QStringLiteral("needs about %1 of %2 \\u2014 close other apps first")
            .arg(size, memory);
    return QStringLiteral("needs about %1 but this computer has %2 \\u2014 expect slowdown; a "
                          "smaller quantisation runs better")
        .arg(size, memory);
}

LocalModelScan scanLocalModelServers()
{
    LocalModelScan scan;

    struct Candidate {
        quint16 port;
        const char* providerName;
    };
    static const Candidate candidates[] = {
        {11434, "Ollama"},  {1234, "LM Studio"}, {8080, "llama.cpp server"},
        {11435, "Ollama"},  {8000, "vLLM"},
    };

    QVector<RespondingServer> responding;
    for (const Candidate& candidate : candidates) {
        const QString provider = QString::fromLatin1(candidate.providerName);
        QVector<LocalModelInfo> found;
        bool answered = false;

        // Ollama also has a native tag list that carries names and sizes.
        if (candidate.port == 11434 || candidate.port == 11435) {
            const HttpResponse tags = httpGet(candidate.port, "/api/tags");
            answered = tags.status != 0;
            if (tags.status == 200)
                found = modelsFromOllamaApiResponse(tags.body);
        }
        if (found.isEmpty()) {
            const HttpResponse list = httpGet(candidate.port, "/v1/models");
            answered = answered || list.status != 0;
            if (list.status == 200) {
                for (const LocalModelInfo& model :
                     modelsFromOllamaApiResponse(list.body)) {
                    addModel(found, model.name, model.sizeBytes, model.source);
                }
            }
        }
        if (!answered)
            continue;

        RespondingServer server;
        server.endpoint =
            QStringLiteral("http://127.0.0.1:%1/v1").arg(static_cast<int>(candidate.port));
        server.providerName = provider;
        server.port = candidate.port;
        server.models = found;
        responding.append(server);
    }

    if (!responding.isEmpty()) {
        // Prefer a server that actually has models; otherwise the first answer.
        auto best = std::max_element(responding.begin(), responding.end(),
                                     [](const RespondingServer& left,
                                        const RespondingServer& right) {
                                         return left.models.size() < right.models.size();
                                     });
        scan.endpoint = best->endpoint;
        scan.providerName = best->providerName;
        scan.models = best->models;
        if (scan.models.isEmpty()) {
            scan.notes << QStringLiteral("%1 answered on 127.0.0.1:%2 but reported no models.")
                              .arg(scan.providerName)
                              .arg(best->port);
        } else {
            scan.notes << QStringLiteral("%1 is running on 127.0.0.1:%2 with %3 installed "
                                         "model(s).")
                              .arg(scan.providerName)
                              .arg(best->port)
                              .arg(scan.models.size());
        }
        for (const RespondingServer& server : responding) {
            if (server.port == best->port || server.models.isEmpty())
                continue;
            scan.notes << QStringLiteral("%1 is also running on port %2 with %3 model(s); this "
                                         "panel uses %4.")
                              .arg(server.providerName)
                              .arg(server.port)
                              .arg(server.models.size())
                              .arg(scan.providerName);
        }
    }

    QStringList foundDirectories;
    QVector<LocalModelInfo> onDisk;
    for (const QString& directory : ollamaModelDirectories()) {
        if (!QFileInfo::exists(QDir(directory).filePath(QStringLiteral("manifests"))))
            continue;
        const QVector<LocalModelInfo> models = modelsFromOllamaModelDirectory(directory);
        foundDirectories << QStringLiteral("%1 (%2 model(s))").arg(directory).arg(models.size());
        for (const LocalModelInfo& model : models)
            addModel(onDisk, model.name, model.sizeBytes, directory);
    }

    if (scan.endpoint.isEmpty()) {
        if (!onDisk.isEmpty()) {
            scan.models = onDisk;
            scan.notes << QStringLiteral(
                              "No local AI server is answering, but the Ollama model folder "
                              "already holds models: %1. Start Ollama (or `ollama serve`), then "
                              "press Find on this computer again.")
                              .arg(foundDirectories.join(QStringLiteral("; ")));
        } else if (!foundDirectories.isEmpty()) {
            scan.notes << QStringLiteral("The Ollama model folder is there (%1) but it is empty: "
                                         "download a model with `ollama pull <name>` first.")
                              .arg(foundDirectories.join(QStringLiteral("; ")));
        } else {
            const QString executable = QStandardPaths::findExecutable(QStringLiteral("ollama"));
            scan.notes << QStringLiteral(
                              "No local AI server answered on ports 11434 (Ollama), 1234 (LM "
                              "Studio), 8080 (llama.cpp) or 8000, and no Ollama model folder was "
                              "found on any drive.");
            if (!executable.isEmpty()) {
                scan.notes << QStringLiteral("Ollama is installed at %1, so starting it should be "
                                             "enough.")
                                  .arg(executable);
            }
        }
    } else if (!foundDirectories.isEmpty()) {
        scan.notes << QStringLiteral("Ollama's model folder: %1.")
                          .arg(foundDirectories.join(QStringLiteral("; ")));
    }

    const quint64 ramBytes = sysinfo::totalRamBytes();
    if (ramBytes > 0) {
        scan.notes << QStringLiteral("This computer has %1 of RAM.").arg(fmt::humanBytes(ramBytes));
    }
    if (!scan.models.isEmpty()) {
        const LocalModelInfo* largest = &scan.models.first();
        for (const LocalModelInfo& model : scan.models) {
            if (model.sizeBytes > largest->sizeBytes)
                largest = &model;
        }
        const QString hint = modelFitHint(largest->sizeBytes, ramBytes);
        if (!hint.isEmpty()) {
            scan.notes << QStringLiteral("Largest model, %1: %2.").arg(largest->name, hint);
        }
    } else if (ramBytes > 0) {
        scan.notes << QStringLiteral("A 4B to 8B model quantised to Q4 needs roughly 3 to 6 GB "
                                     "of RAM, which this machine can run beside PKG Studio.");
    }

    return scan;
}

}  // namespace pkg
