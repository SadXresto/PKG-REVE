#include "ai/AiClient.h"

#include "core/Logging.h"
#include "data/Database.h"
#include "data/Settings.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

#include <cstring>
#include <memory>

namespace pkg {
namespace {

constexpr auto kLogCategory = "ai";
constexpr auto kKeyFile = "ai.key";

QString trimmedKey(const QString& key)
{
    return key.trimmed();
}

}  // namespace

QString AiSettingsStore::keyFilePath()
{
    return QDir(Settings::dataDirectory()).filePath(QString::fromLatin1(kKeyFile));
}

AiSettingsValues AiSettingsStore::load(Database& database)
{
    AiSettingsValues values;
    values.enabled = database.setting(QStringLiteral("ai.enabled"), QStringLiteral("false"))
                    == QLatin1String("true");
    const QString endpoint = database.setting(QStringLiteral("ai.endpoint"));
    if (!endpoint.trimmed().isEmpty())
        values.endpoint = endpoint.trimmed();
    const QString model = database.setting(QStringLiteral("ai.model"));
    if (!model.trimmed().isEmpty())
        values.model = model.trimmed();
    values.includeFileContext =
        database.setting(QStringLiteral("ai.includeFileContext"), QStringLiteral("true"))
        != QLatin1String("false");
    return values;
}

void AiSettingsStore::save(Database& database, const AiSettingsValues& values)
{
    database.setSetting(QStringLiteral("ai.enabled"),
                        values.enabled ? QStringLiteral("true") : QStringLiteral("false"));
    database.setSetting(QStringLiteral("ai.endpoint"), values.endpoint.trimmed());
    database.setSetting(QStringLiteral("ai.model"), values.model.trimmed());
    database.setSetting(QStringLiteral("ai.includeFileContext"),
                        values.includeFileContext ? QStringLiteral("true")
                                                  : QStringLiteral("false"));
}

QString AiSettingsStore::loadKey()
{
    QFile file(keyFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return trimmedKey(QString::fromUtf8(file.readAll()));
}

void AiSettingsStore::storeKey(const QString& key)
{
    const QString path = keyFilePath();
    const QString value = trimmedKey(key);
    if (value.isEmpty()) {
        QFile::remove(path);
        return;
    }
    QFile file(path);
    if (file.exists())
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logWarning(QString::fromLatin1(kLogCategory),
                   QStringLiteral("Cannot store the AI key at %1").arg(path));
        return;
    }
    file.write(value.toUtf8());
    file.close();
    // Re-apply after writing: some platforms reset permissions on truncate.
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
}

bool AiSettingsStore::isLoopbackEndpoint(const QString& endpoint)
{
    const QUrl url(endpoint.trimmed());
    if (!url.isValid() || url.host().isEmpty())
        return false;
    const QString host = url.host().toLower();
    return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1")
        || host == QLatin1String("::1") || host.endsWith(QLatin1String(".localhost"));
}

QString AiSettingsStore::chatCompletionsUrl(const QString& endpoint)
{
    QString base = endpoint.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (base.endsWith(QLatin1String("/chat/completions")))
        return base;
    return base + QStringLiteral("/chat/completions");
}

QString AiSettingsStore::modelsUrl(const QString& endpoint)
{
    QString base = endpoint.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (base.endsWith(QLatin1String("/chat/completions")))
        base.chop(static_cast<int>(std::strlen("/chat/completions")));
    if (base.endsWith(QLatin1String("/models")))
        return base;
    return base + QStringLiteral("/models");
}

QVector<AiProviderPreset> providerPresets()
{
    return {
        {QStringLiteral("ollama"), QStringLiteral("Ollama \u2014 model on this computer"),
         QStringLiteral("http://127.0.0.1:11434/v1"), QStringLiteral("llama3.1"),
         QStringLiteral("Uses Ollama's OpenAI-compatible endpoint. Nothing leaves this "
                        "computer."),
         false},
        {QStringLiteral("lmstudio"), QStringLiteral("LM Studio \u2014 model on this computer"),
         QStringLiteral("http://127.0.0.1:1234/v1"),
         QStringLiteral("qwen2.5-coder-7b-instruct"),
         QStringLiteral("Start the LM Studio local server first; the default port is 1234."),
         false},
        {QStringLiteral("llamacpp"), QStringLiteral("llama.cpp server \u2014 this computer"),
         QStringLiteral("http://127.0.0.1:8080/v1"), QStringLiteral("local-model"),
         QStringLiteral("For `llama-server -m model.gguf`; it answers on port 8080 by "
                        "default."),
         false},
        {QStringLiteral("openai"), QStringLiteral("OpenAI API \u2014 needs an API key"),
         QStringLiteral("https://api.openai.com/v1"), QStringLiteral("gpt-4o-mini"),
         QStringLiteral("Hosted endpoint: your prompt and the open file's listing are sent "
                        "to OpenAI. The key is stored locally, owner-only."),
         true},
        {QStringLiteral("custom"), QStringLiteral("Custom OpenAI-compatible endpoint"),
         QString(), QString(),
         QStringLiteral("Any server that implements POST /chat/completions and GET "
                        "/models."),
         false},
    };
}

QString AiClient::systemPrompt()
{
    return QStringLiteral(
        "You are the AI assistant inside PKG Studio, a local package-analysis IDE.\n"
        "Rules:\n"
        "1. Only help with files the user owns or is legally allowed to inspect or modify "
        "(homebrew, their own builds, public test files). Refuse requests to bypass DRM, "
        "console security or copy protection.\n"
        "2. Be precise about uncertainty. Never invent file offsets, field values or "
        "capabilities you cannot verify from the supplied context.\n"
        "3. When the user asks you to change bytes, answer with one fenced block:\n"
        "```pkgstudio-edit\n"
        "00000010  90 90 90 90\n"
        "```\n"
        "Use uppercase hex pairs, eight or more offset digits, offsets relative to the "
        "start of the file, and at least four bytes of context around each change. Add a "
        "short explanation before the block listing each change and why.\n"
        "4. Byte edits replace bytes only; insertions, deletions, decompression, "
        "repacking and code signing are not available.\n"
        "5. If the user asks for an edit you cannot express as a byte patch, say so and "
        "explain what would be needed instead.\n");
}

AiClient::AiClient(QObject* parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this))
{
}

void AiClient::abort()
{
    if (m_reply != nullptr)
        m_reply->abort();
}

void AiClient::send(const AiSettingsValues& settings, const QString& apiKey,
                    const QVector<AiMessage>& messages, Callback callback)
{
    const QString urlText = AiSettingsStore::chatCompletionsUrl(settings.endpoint);
    const QUrl url(urlText);
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) {
        callback(makeError(QStringLiteral("Invalid AI endpoint"),
                           QStringLiteral("Use an http:// or https:// base URL, for example "
                                          "http://127.0.0.1:11434/v1")));
        return;
    }
    if (settings.model.trimmed().isEmpty()) {
        callback(makeError(QStringLiteral("No model name is configured")));
        return;
    }

    QJsonArray messageArray;
    messageArray.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                    {QStringLiteral("content"), systemPrompt()}});
    for (const AiMessage& message : messages) {
        if (message.role.isEmpty() || message.content.isEmpty())
            continue;
        messageArray.append(QJsonObject{{QStringLiteral("role"), message.role},
                                        {QStringLiteral("content"), message.content}});
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), settings.model.trimmed());
    payload.insert(QStringLiteral("messages"), messageArray);
    payload.insert(QStringLiteral("temperature"), 0.2);
    payload.insert(QStringLiteral("stream"), false);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    const QString key = trimmedKey(apiKey);
    if (!key.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    request.setTransferTimeout(180000);

    if (m_reply != nullptr) {
        m_reply->abort();
        m_reply = nullptr;
    }
    auto finished = std::make_shared<bool>(false);
    const auto deliver = [callback, finished](const Result<Reply>& result) {
        if (*finished)
            return;
        *finished = true;
        callback(result);
    };

    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("AI request to %1 (model %2, %3 message(s))")
                .arg(urlText, settings.model.trimmed())
                .arg(messageArray.size()));

    QNetworkReply* reply = m_manager->post(request, QJsonDocument(payload).toJson());
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, deliver]() {
        reply->deleteLater();
        if (m_reply == reply)
            m_reply = nullptr;

        const QByteArray body = reply->readAll();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            QString detail = reply->errorString();
            if (!body.isEmpty())
                detail += QStringLiteral(" \u2014 ") + QString::fromUtf8(body.left(600));
            deliver(makeError(QStringLiteral("The AI endpoint could not complete the request"),
                              detail));
            return;
        }
        if (status >= 400) {
            deliver(makeError(QStringLiteral("The AI endpoint returned HTTP %1").arg(status),
                              QString::fromUtf8(body.left(600))));
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(body);
        const QJsonObject object = document.object();
        QString content;
        const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            const QJsonObject message =
                choices.first().toObject().value(QStringLiteral("message")).toObject();
            content = message.value(QStringLiteral("content")).toString();
        }
        if (content.isEmpty()) {
            // Some local servers answer /v1/completions-style or wrap an error.
            content = object.value(QStringLiteral("response")).toString();
        }
        if (content.isEmpty()) {
            const QString errorText =
                object.value(QStringLiteral("error")).toObject()
                    .value(QStringLiteral("message")).toString();
            deliver(makeError(QStringLiteral("The AI endpoint returned no text"),
                              errorText.isEmpty() ? QString::fromUtf8(body.left(600))
                                                  : errorText));
            return;
        }

        Reply result;
        result.content = content;
        result.httpStatus = status;
        result.model = object.value(QStringLiteral("model")).toString();
        logInfo(QString::fromLatin1(kLogCategory),
                QStringLiteral("AI reply received (%1 characters)").arg(content.size()));
        deliver(result);
    });
}

void AiClient::listModels(const AiSettingsValues& settings, const QString& apiKey,
                          ModelCallback callback)
{
    const QString urlText = AiSettingsStore::modelsUrl(settings.endpoint);
    const QUrl url(urlText);
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) {
        callback(makeError(QStringLiteral("Invalid AI endpoint"),
                           QStringLiteral("Use an http:// or https:// base URL, for example "
                                          "http://127.0.0.1:11434/v1")));
        return;
    }

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    const QString key = trimmedKey(apiKey);
    if (!key.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    request.setTransferTimeout(20000);

    if (m_modelsReply != nullptr) {
        m_modelsReply->abort();
        m_modelsReply = nullptr;
    }
    auto finished = std::make_shared<bool>(false);
    const auto deliver = [callback, finished](const Result<QStringList>& result) {
        if (*finished)
            return;
        *finished = true;
        callback(result);
    };

    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("AI model list request to %1").arg(urlText));

    QNetworkReply* reply = m_manager->get(request);
    m_modelsReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, deliver]() {
        reply->deleteLater();
        if (m_modelsReply == reply)
            m_modelsReply = nullptr;

        const QByteArray body = reply->readAll();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            QString detail = reply->errorString();
            if (!body.isEmpty())
                detail += QStringLiteral(" \u2014 ") + QString::fromUtf8(body.left(400));
            deliver(makeError(
                QStringLiteral("The endpoint did not answer the model list request"), detail));
            return;
        }
        if (status >= 400) {
            deliver(makeError(QStringLiteral("The endpoint returned HTTP %1").arg(status),
                              QString::fromUtf8(body.left(400))));
            return;
        }

        const QJsonObject object = QJsonDocument::fromJson(body).object();
        QStringList names;
        const auto addName = [&names](const QJsonValue& value) {
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
            }
            name = name.trimmed();
            if (!name.isEmpty() && !names.contains(name))
                names.append(name);
        };
        // OpenAI-compatible servers answer {"data": [...]}; Ollama's native
        // /api/tags answers {"models": [...]} with a name field.
        for (const QJsonValue& value : object.value(QStringLiteral("data")).toArray())
            addName(value);
        for (const QJsonValue& value : object.value(QStringLiteral("models")).toArray())
            addName(value);

        logInfo(QString::fromLatin1(kLogCategory),
                QStringLiteral("AI endpoint reported %1 model(s)").arg(names.size()));
        deliver(names);
    });
}

QVector<ListingEntry> extractEdits(const QString& reply)
{
    static const QRegularExpression fence(
        QStringLiteral("```(?:pkgstudio-edit|edit|hex)\\s*\\n(.*?)```"),
        QRegularExpression::DotMatchesEverythingOption
            | QRegularExpression::CaseInsensitiveOption);

    QStringList blocks;
    auto it = fence.globalMatch(reply);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        blocks << match.captured(1);
    }
    if (blocks.isEmpty())
        blocks << reply;

    QVector<ListingEntry> entries;
    for (const QString& block : blocks) {
        auto parsed = parseListing(block);
        if (!parsed)
            continue;
        for (const ListingEntry& entry : parsed.value()) {
            if (!entries.isEmpty()) {
                ListingEntry& last = entries.last();
                if (last.offset + static_cast<quint64>(last.bytes.size()) == entry.offset) {
                    last.bytes.append(entry.bytes);
                    continue;
                }
            }
            entries.append(entry);
        }
    }
    return entries;
}

}  // namespace pkg
