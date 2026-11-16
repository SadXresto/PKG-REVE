// Crash-safe file writes.
//
// Every write goes to a temporary file in the destination directory, is
// flushed to disk, and is then renamed over the target. A failure before the
// rename leaves the original untouched.
#pragma once

#include "core/CancelToken.h"
#include "core/Result.h"

#include <QByteArray>
#include <QString>

#include <functional>

namespace pkg::atomic {

/// Atomically replaces `path` with `data`.
[[nodiscard]] Status writeFile(const QString& path, const QByteArray& data);

/// Reads at most `maxBytes`; fails when the file is larger than that.
[[nodiscard]] Result<QByteArray> readFile(const QString& path, quint64 maxBytes);

/// Copies `source` to `backupPath` atomically. Returns the backup size.
/// Cancellation removes the temporary file; the source and any existing backup
/// at `backupPath` are untouched.
[[nodiscard]] Result<quint64> backupFile(
    const QString& source, const QString& backupPath,
    const CancelToken& token = CancelToken(),
    const std::function<void(quint64, quint64)>& progress = {});

}  // namespace pkg::atomic
