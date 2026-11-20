#include "ai/AiClient.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <gtest/gtest.h>

#include <memory>

using namespace pkg;

namespace {

/// Minimal HTTP/1.1 server that answers every request with one JSON body.
class FakeEndpoint : public QTcpServer {
public:
    explicit FakeEndpoint(QByteArray body, QObject* parent = nullptr)
        : QTcpServer(parent), m_body(std::move(body))
    {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = nextPendingConnection()) {
                auto answered = std::make_shared<bool>(false);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, answered]() {
                    m_request.append(socket->readAll());
                    if (*answered)
                        return;
                    const qsizetype headerEnd = m_request.indexOf("\r\n\r\n");
                    if (headerEnd < 0)
                        return;
                    // Wait for the whole body: the request headers and the JSON
                    // payload can arrive in separate packets.
                    qsizetype expected = headerEnd + 4;
                    const QList<QByteArray> lines = m_request.left(headerEnd).split('\n');
                    for (const QByteArray& line : lines) {
                        if (line.toLower().startsWith("content-length:")) {
                            bool ok = false;
                            const qsizetype length =
                                line.mid(static_cast<qsizetype>(16)).trimmed().toLongLong(&ok);
                            if (ok)
                                expected += length;
                        }
                    }
                    if (m_request.size() < expected)
                        return;
                    *answered = true;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                  "Content-Length: " + QByteArray::number(m_body.size())
                                  + "\r\nConnection: close\r\n\r\n");
                    socket->write(m_body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    [[nodiscard]] QByteArray request() const { return m_request; }

private:
    QByteArray m_body;
    QByteArray m_request;
};

Result<QStringList> runModelList(const QString& endpoint)
{
    AiClient client;
    AiSettingsValues settings;
    settings.endpoint = endpoint;

    Result<QStringList> result = makeError(QStringLiteral("no reply"));
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(10000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &client, &AiClient::abort);
    client.listModels(settings, QString(), [&result, &loop](const Result<QStringList>& models) {
        result = models;
        loop.quit();
    });
    timeout.start();
    loop.exec();
    return result;
}

Result<AiClient::Reply> runRequest(const QString& endpoint)
{
    AiClient client;
    AiSettingsValues settings;
    settings.enabled = true;
    settings.endpoint = endpoint;
    settings.model = QStringLiteral("test-model");

    QVector<AiMessage> messages;
    messages.append(AiMessage{QStringLiteral("user"), QStringLiteral("Which bytes change?")});

    Result<AiClient::Reply> result = makeError(QStringLiteral("no reply"));
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(10000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &client, &AiClient::abort);
    client.send(settings, QStringLiteral("secret-key"), messages,
                [&result, &loop](const Result<AiClient::Reply>& reply) {
                    result = reply;
                    loop.quit();
                });
    timeout.start();
    loop.exec();
    return result;
}

}  // namespace

TEST(AiClient, PostsOpenAiCompatibleRequestAndReadsReply)
{
    const QByteArray body = QJsonDocument(QJsonObject{
        {QStringLiteral("model"), QStringLiteral("test-model")},
        {QStringLiteral("choices"),
         QJsonArray{QJsonObject{
             {QStringLiteral("message"),
              QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                          {QStringLiteral("content"),
                           QStringLiteral("Patch:\n```pkgstudio-edit\n00000000  90 90\n```")}}}}}}})
        .toJson();

    FakeEndpoint server(body);
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    const QString endpoint =
        QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());

    const auto reply = runRequest(endpoint);
    ASSERT_TRUE(reply) << reply.error().message.toUtf8().constData();
    EXPECT_TRUE(reply.value().content.contains(QStringLiteral("pkgstudio-edit")));
    EXPECT_EQ(reply.value().httpStatus, 200);

    const QByteArray request = server.request();
    const QByteArray lower = request.toLower();
    EXPECT_TRUE(request.startsWith("POST /v1/chat/completions "))
        << request.toStdString();
    // Qt writes header names in lower case.
    EXPECT_TRUE(lower.contains("authorization: bearer secret-key"))
        << request.toStdString();
    EXPECT_TRUE(request.contains("\"model\": \"test-model\"")) << request.toStdString();
    EXPECT_TRUE(request.contains("\"stream\": false")) << request.toStdString();
    EXPECT_TRUE(request.contains("\"role\": \"user\"")) << request.toStdString();

    const QVector<ListingEntry> edits = extractEdits(reply.value().content);
    ASSERT_EQ(edits.size(), 1);
    EXPECT_EQ(edits.first().bytes, QByteArray::fromHex("9090"));
}

TEST(AiClient, ListsInstalledModelsFromTheEndpoint)
{
    const QByteArray body = QJsonDocument(QJsonObject{
        {QStringLiteral("data"),
         QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("qwen2.5-coder:7b")}},
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("llama3.1")}},
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("llama3.1")}}}}})
        .toJson();

    FakeEndpoint server(body);
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    const QString endpoint =
        QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());

    const auto models = runModelList(endpoint);
    ASSERT_TRUE(models) << models.error().message.toUtf8().constData();
    ASSERT_EQ(models.value().size(), 2);  // duplicates are dropped
    EXPECT_EQ(models.value().at(0), QStringLiteral("qwen2.5-coder:7b"));
    EXPECT_TRUE(server.request().startsWith("GET /v1/models "))
        << server.request().toStdString();
}

TEST(AiClient, OffersLocalProvidersAndBuildsModelUrls)
{
    const QVector<AiProviderPreset> presets = providerPresets();
    ASSERT_GE(presets.size(), 4);
    EXPECT_EQ(presets.first().id, QStringLiteral("ollama"));
    EXPECT_TRUE(AiSettingsStore::isLoopbackEndpoint(presets.first().endpoint));
    bool hasHosted = false;
    for (const AiProviderPreset& preset : presets)
        hasHosted = hasHosted || preset.requiresKey;
    EXPECT_TRUE(hasHosted);

    EXPECT_EQ(AiSettingsStore::modelsUrl(QStringLiteral("http://127.0.0.1:11434/v1/")),
              QStringLiteral("http://127.0.0.1:11434/v1/models"));
    EXPECT_EQ(AiSettingsStore::modelsUrl(
                  QStringLiteral("http://127.0.0.1:11434/v1/chat/completions")),
              QStringLiteral("http://127.0.0.1:11434/v1/models"));
}

TEST(AiClient, ReportsEndpointFailureWithoutHanging)
{
    // Port 1 on loopback is not listening, so the request must fail cleanly.
    const auto reply = runRequest(QStringLiteral("http://127.0.0.1:1/v1"));
    ASSERT_FALSE(reply);
    EXPECT_FALSE(reply.error().message.isEmpty());
}

TEST(AiClient, NormalisesUrlsAndDetectsLoopback)
{
    EXPECT_EQ(AiSettingsStore::chatCompletionsUrl(QStringLiteral("http://127.0.0.1:11434/v1/")),
              QStringLiteral("http://127.0.0.1:11434/v1/chat/completions"));
    EXPECT_EQ(AiSettingsStore::chatCompletionsUrl(
                  QStringLiteral("http://host/v1/chat/completions")),
              QStringLiteral("http://host/v1/chat/completions"));

    EXPECT_TRUE(AiSettingsStore::isLoopbackEndpoint(QStringLiteral("http://127.0.0.1:11434/v1")));
    EXPECT_TRUE(AiSettingsStore::isLoopbackEndpoint(QStringLiteral("http://localhost:1234/v1")));
    EXPECT_FALSE(AiSettingsStore::isLoopbackEndpoint(QStringLiteral("https://api.example.com/v1")));
}
