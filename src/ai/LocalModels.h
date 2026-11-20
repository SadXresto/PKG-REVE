// Finds AI models that are already on this computer.
//
// Everything here is on-demand: nothing runs until the user asks for it. The
// scan sends one tiny HTTP request to the usual local AI ports and looks only in
// the standard Ollama model folders (one per drive, never a deep disk walk), so
// it stays cheap on low-end machines.
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
/// OpenAI-compatible `/v1/models` body (`{"data": [...]}`). Names are kept
/// exactly, including namespaces and tags.
[[nodiscard]] QVector<LocalModelInfo> modelsFromOllamaApiResponse(const QByteArray& body);

/// Reads `manifests/<registry>/<namespace>/<model>/<tag>` under an Ollama model
/// folder and derives the model names plus their on-disk size.
[[nodiscard]] QVector<LocalModelInfo> modelsFromOllamaModelDirectory(
    const QString& modelsDirectory);

/// Standard Ollama model folders: OLLAMA_MODELS, the user profile, and one
/// candidate set per drive (so a model library on D: is found as well).
[[nodiscard]] QStringList ollamaModelDirectories();

/// Probes the usual local ports and, if no server is running, lists the models
/// found on disk. Blocking; call it from a worker thread.
[[nodiscard]] LocalModelScan scanLocalModelServers();

/// RAM guidance for a model of `modelBytes` on a machine with `ramBytes`.
/// Empty when either value is unknown.
[[nodiscard]] QString modelFitHint(quint64 modelBytes, quint64 ramBytes);

}  // namespace pkg
