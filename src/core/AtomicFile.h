// Crash-safe file writes.
//
// Every write goes to a temporary file in the destination directory, is
// flushed to disk, and is then renamed over the target. A failure before the
// rename leaves the original untouched.
#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QString>

namespace pkg::atomic {

/// Atomically replaces `path` with `data`.
[[nodiscard]] Status writeFile(const QString& path, const QByteArray& data);

/// Reads at most `maxBytes`; fails when the file is larger than that.
[[nodiscard]] Result<QByteArray> readFile(const QString& path, quint64 maxBytes);

/// Copies `source` to `backupPath` atomically. Returns the backup size.
[[nodiscard]] Result<quint64> backupFile(const QString& source, const QString& backupPath);

}  // namespace pkg::atomic
