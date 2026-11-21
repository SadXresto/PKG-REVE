// Finds AI models that are already on this computer.
//
// Everything here is on-demand: nothing runs until the user asks for it. The
// scan sends one tiny HTTP request to the usual local AI ports and looks only in
// the standard Ollama model folders (one set per local drive, never a deep disk
// walk), so it stays cheap on low-end machines. Disconnected network drives are
// skipped: touching one can block for seconds and make the scan look hung.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace pkg {

/// One model that exists locally: either the endpoint reports it (with or
/// without a size) or its manifest was found in an Ollama model folder.
struct LocalModelInfo {
    QString name;  // exactly as the server/tag names it, e.g. huihui_ai/qwen3.4b:7b
    quint64 sizeBytes = 0;
    QString source;
};

struct LocalModelScan {
    /// Base URL of the local server that answered, e.g. http://127.0.0.1:11434/v1.
    QString endpoint;
    QString providerName;
    QVector<LocalModelInfo> models;
    /// Human-readable findings: what was found where, what is missing, and how
    /// the largest model fits this machine's RAM.
    QStringList notes;
};

/// Parses an Ollama `/api/tags` body (`{"models": [...]}`) or an
/// OpenAI-compatible `/v1/models` body (`{"data": [...]}`). This is the one
/// model-list parser in the application; the chat's "List models" button uses it
/// too. Names are kept exactly, including namespaces and tags, and duplicates
/// are merged (largest reported size wins).
[[nodiscard]] QVector<LocalModelInfo> modelsFromOllamaApiResponse(const QByteArray& body);

/// Reads `manifests/<registry>/<namespace>/<model>/<tag>` under an Ollama model
/// folder and derives the model names plus their on-disk size.
[[nodiscard]] QVector<LocalModelInfo> modelsFromOllamaModelDirectory(
    const QString& modelsDirectory);

/// True when `path` is an existing local drive root that is safe to probe. A
/// disconnected mapped network drive is rejected without touching it, because
/// the first access to one can block for a long time.
[[nodiscard]] bool isScannableDriveRoot(const QString& path);

/// Standard Ollama model folders: OLLAMA_MODELS, the user profile, and one
/// candidate set per scannable local drive (so a model library on D: is found).
[[nodiscard]] QStringList ollamaModelDirectories();

/// Probes the usual local ports and, if no server is running, lists the models
/// found on disk. Blocking and bounded; call it from a worker thread.
[[nodiscard]] LocalModelScan scanLocalModelServers();

/// RAM guidance for a model of `modelBytes` on a machine with `ramBytes`.
/// Empty when either value is unknown.
[[nodiscard]] QString modelFitHint(quint64 modelBytes, quint64 ramBytes);

}  // namespace pkg
