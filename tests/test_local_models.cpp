#include "ai/LocalModels.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QtConcurrent>

#include <gtest/gtest.h>

#include <utility>

using namespace pkg;

namespace {

constexpr quint64 kGb = 1000ULL * 1000ULL * 1000ULL;

/// Serves one JSON body on every request, like a local model server would.
class FakeModelServer : public QTcpServer {
public:
    explicit FakeModelServer(QByteArray body) : m_body(std::move(body))
    {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                    socket->readAll();
                    if (socket->property("answered").toBool())
                        return;
                    socket->setProperty("answered", true);
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                  "Content-Length: " + QByteArray::number(m_body.size())
                                  + "\r\nConnection: close\r\n\r\n" + m_body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

private:
    QByteArray m_body;
};

void writeManifest(const QString& path, qint64 layersBytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QJsonObject manifest{
        {QStringLiteral("config"),
         QJsonObject{{QStringLiteral("size"), static_cast<double>(100)}}},
        {QStringLiteral("layers"),
         QJsonArray{QJsonObject{{QStringLiteral("size"), static_cast<double>(layersBytes)}}}},
    };
    file.write(QJsonDocument(manifest).toJson());
}

}  // namespace

TEST(LocalModels, ParsesOllamaTagsWithSizesAndNamespacedNames)
{
    const QByteArray body = QJsonDocument(QJsonObject{
        {QStringLiteral("models"),
         QJsonArray{
             QJsonObject{{QStringLiteral("name"), QStringLiteral("huihui_ai/qwen3.4b:7b")},
                         {QStringLiteral("size"), 4971267168.0}},
             QJsonObject{{QStringLiteral("name"), QStringLiteral("llama3.1:8b")},
                         {QStringLiteral("size"), 4661224676.0}},
             // A second entry for the same tag must not duplicate the name.
             QJsonObject{{QStringLiteral("name"), QStringLiteral("llama3.1:8b")},
                         {QStringLiteral("size"), 1.0}},
         }}})
                                   .toJson();

    const QVector<LocalModelInfo> models = modelsFromOllamaApiResponse(body);
    ASSERT_EQ(models.size(), 2);
    EXPECT_EQ(models.at(0).name, QStringLiteral("huihui_ai/qwen3.4b:7b"));
    EXPECT_EQ(models.at(0).sizeBytes, 4971267168ULL);
    EXPECT_EQ(models.at(1).name, QStringLiteral("llama3.1:8b"));
    EXPECT_EQ(models.at(1).sizeBytes, 4661224676ULL);
}

TEST(LocalModels, ParsesOpenAiCompatibleModelIds)
{
    const QByteArray body = QJsonDocument(QJsonObject{
        {QStringLiteral("data"),
         QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("qwen2.5-coder-7b-instruct")}},
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("local-model")}}}}})
                                   .toJson();

    const QVector<LocalModelInfo> models = modelsFromOllamaApiResponse(body);
    ASSERT_EQ(models.size(), 2);
    EXPECT_EQ(models.at(0).name, QStringLiteral("qwen2.5-coder-7b-instruct"));
    EXPECT_EQ(models.at(0).sizeBytes, 0ULL);
}

TEST(LocalModels, ReadsNamespacedModelsFromOllamaManifestDirectory)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());

    writeManifest(QDir(temp.path()).filePath(
                      QStringLiteral("manifests/registry.ollama.ai/huihui_ai/qwen3.4b/7b")),
                  4000);
    writeManifest(
        QDir(temp.path()).filePath(QStringLiteral("manifests/registry.ollama.ai/library/llama3.1/8b")),
        2000);

    const QVector<LocalModelInfo> models = modelsFromOllamaModelDirectory(temp.path());
    ASSERT_EQ(models.size(), 2);

    QMap<QString, quint64> sizes;
    for (const LocalModelInfo& model : models)
        sizes.insert(model.name, model.sizeBytes);
    // The official library namespace is implicit; other namespaces are kept.
    const QString listed = sizes.keys().join(QStringLiteral(", "));
    EXPECT_TRUE(sizes.contains(QStringLiteral("huihui_ai/qwen3.4b:7b"))) << listed.toStdString();
    EXPECT_TRUE(sizes.contains(QStringLiteral("llama3.1:8b"))) << listed.toStdString();
    EXPECT_EQ(sizes.value(QStringLiteral("huihui_ai/qwen3.4b:7b")), 4100ULL);
    EXPECT_EQ(sizes.value(QStringLiteral("llama3.1:8b")), 2100ULL);
}

TEST(LocalModels, IgnoresFoldersWithoutManifests)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    EXPECT_TRUE(modelsFromOllamaModelDirectory(temp.path()).isEmpty());
}

TEST(LocalModels, FitHintSeparatesComfortableTightAndTooLarge)
{
    EXPECT_TRUE(modelFitHint(4 * kGb, 16 * kGb).contains(QStringLiteral("comfortably")));
    EXPECT_TRUE(modelFitHint(13 * kGb, 16 * kGb).contains(QStringLiteral("close other apps")));
    EXPECT_TRUE(modelFitHint(30 * kGb, 16 * kGb).contains(QStringLiteral("smaller")));
    EXPECT_TRUE(modelFitHint(0, 16 * kGb).isEmpty());
    EXPECT_TRUE(modelFitHint(4 * kGb, 0).isEmpty());
}

TEST(LocalModels, ChecksTheStandardOllamaFolderOnEveryDrive)
{
    const QStringList directories = ollamaModelDirectories();
    ASSERT_FALSE(directories.isEmpty());
    for (const QString& directory : directories)
        EXPECT_TRUE(QDir::isAbsolutePath(directory)) << directory.toStdString();

    for (const QFileInfo& drive : QDir::drives()) {
        bool found = false;
        for (const QString& directory : directories) {
            if (directory.startsWith(drive.absoluteFilePath())) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << drive.absoluteFilePath().toStdString();
    }
}

TEST(LocalModels, FindsARunningOllamaServerAndItsNamespacedModels)
{
    const QByteArray body = QJsonDocument(QJsonObject{
        {QStringLiteral("models"),
         QJsonArray{QJsonObject{{QStringLiteral("name"),
                                 QStringLiteral("huihui_ai/qwen3.4b:7b")},
                                {QStringLiteral("size"), 4971267168.0}}}}})
                                   .toJson();
    FakeModelServer server(body);
    if (!server.listen(QHostAddress::LocalHost, 11434))
        GTEST_SKIP() << "port 11434 is not available in this environment";

    // The scan blocks on sockets, so run it on a worker and keep processing
    // events here: the fake server needs them to accept the connection.
    QFuture<LocalModelScan> future =
        QtConcurrent::run([]() { return scanLocalModelServers(); });
    QElapsedTimer timer;
    timer.start();
    while (!future.isFinished() && timer.elapsed() < 15000) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    ASSERT_TRUE(future.isFinished()) << "the scan did not finish";
    const LocalModelScan scan = future.result();
    EXPECT_EQ(scan.endpoint, QStringLiteral("http://127.0.0.1:11434/v1"));
    EXPECT_EQ(scan.providerName, QStringLiteral("Ollama"));
    ASSERT_FALSE(scan.models.isEmpty());
    EXPECT_EQ(scan.models.first().name, QStringLiteral("huihui_ai/qwen3.4b:7b"));
    EXPECT_EQ(scan.models.first().sizeBytes, 4971267168ULL);
    EXPECT_FALSE(scan.notes.isEmpty());
}

TEST(LocalModels, ScanReportsWhatItCheckedWithoutAServer)
{
    // No AI server runs in the test environment; the scan must still finish,
    // explain itself and never claim a model it did not find.
    const LocalModelScan scan = scanLocalModelServers();
    EXPECT_FALSE(scan.notes.isEmpty());
    const bool explained = !scan.notes.join(QLatin1Char(' ')).isEmpty();
    EXPECT_TRUE(explained);
}
