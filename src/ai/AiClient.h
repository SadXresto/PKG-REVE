// Local-first AI assistant transport.
//
// The assistant talks to an OpenAI-compatible `/chat/completions` endpoint that
// the user configures, typically a model running on this computer (Ollama, LM
// Studio, llama.cpp server, vLLM). No vendor account, cloud relay or telemetry
// is involved: the only network traffic is the request to that endpoint.
//
// The API key, when one is needed, is written to a separate file in the
// application data directory with owner-only permissions. It is never logged.
#pragma once

#include "core/CodeListing.h"
#include "core/Result.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace pkg {

class Database;

struct AiMessage {
    QString role;  // "system", "user" or "assistant"
    QString content;
};

struct AiSettingsValues {
    bool enabled = false;
    QString endpoint = QStringLiteral("http://127.0.0.1:11434/v1");
    QString model = QStringLiteral("llama3.1");
    bool includeFileContext = true;
};

/// Reads and writes the AI configuration. The endpoint, model and switches live
/// in the local SQLite settings table; the key lives in `ai.key`.
class AiSettingsStore {
public:
    [[nodiscard]] static AiSettingsValues load(Database& database);
    static void save(Database& database, const AiSettingsValues& values);

    [[nodiscard]] static QString keyFilePath();
    [[nodiscard]] static QString loadKey();
    /// Empty removes the stored key.
    static void storeKey(const QString& key);

    /// True when `endpoint` is a loopback address (nothing leaves the machine).
    [[nodiscard]] static bool isLoopbackEndpoint(const QString& endpoint);
    /// Normalises a base URL into the chat completions URL.
    [[nodiscard]] static QString chatCompletionsUrl(const QString& endpoint);
};

class AiClient : public QObject {
    Q_OBJECT

public:
    struct Reply {
        QString content;
        int httpStatus = 0;
        QString model;
    };
    using Callback = std::function<void(const Result<Reply>&)>;

    explicit AiClient(QObject* parent = nullptr);

    /// Sends the conversation. `callback` runs exactly once on the UI thread,
    /// with an error when the endpoint is unreachable or the reply is invalid.
    void send(const AiSettingsValues& settings, const QString& apiKey,
              const QVector<AiMessage>& messages, Callback callback);
    void abort();

    [[nodiscard]] static QString systemPrompt();

private:
    QNetworkAccessManager* m_manager = nullptr;
    QPointer<QNetworkReply> m_reply;
};

/// Pulls byte edits out of a model reply: fenced ```pkgstudio-edit blocks, or
/// bare "OFFSET  HEX BYTES" lines when no fence is present.
[[nodiscard]] QVector<ListingEntry> extractEdits(const QString& reply);

}  // namespace pkg
