// DevFlow Error Intelligence: turn raw compiler/build output into grouped,
// navigable diagnostics with optional beginner-friendly explanations.
//
// Explanations are static knowledge derived from the diagnostic text itself.
// No AI call is made here, and no fix is ever applied automatically.
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace pkg::devflow {

enum class DiagnosticSeverity { Note = 0, Warning = 1, Error = 2, Fatal = 3 };

[[nodiscard]] QString severityName(DiagnosticSeverity severity);

struct BuildDiagnostic {
    QString file;
    int line = 0;
    int column = 0;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    QString code;
    QString message;
    QString raw;
    /// Stable across runs for the same problem, independent of line numbers
    /// and absolute paths.
    [[nodiscard]] QString fingerprint() const;
};

struct DiagnosticGroup {
    QString fingerprint;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    QString file;
    int line = 0;
    QString code;
    QString message;
    int count = 0;
    QString explanation;
    QString suggestion;
    [[nodiscard]] QString summary() const;
};

struct ErrorReport {
    QVector<BuildDiagnostic> diagnostics;
    QStringList unparsedLines;
    bool truncated = false;
    [[nodiscard]] int errorCount() const;
    [[nodiscard]] int warningCount() const;
    [[nodiscard]] bool isEmpty() const { return diagnostics.isEmpty(); }
};

/// Parses GCC/Clang and MSVC style diagnostics. Unrecognised lines are kept in
/// `unparsedLines` (bounded) so the caller can show the raw build output.
[[nodiscard]] ErrorReport parseBuildOutput(const QString& output, int maxDiagnostics = 3000);

/// Groups diagnostics with the same fingerprint. Groups keep the first
/// occurrence's location and the duplicate count.
[[nodiscard]] QVector<DiagnosticGroup> groupDiagnostics(const QVector<BuildDiagnostic>& diagnostics);

/// Plain-language explanation for a diagnostic; empty when no rule matches.
[[nodiscard]] QString explainDiagnostic(const QString& message);
/// Possible next step; always phrased as a suggestion, never a fact.
[[nodiscard]] QString suggestFix(const QString& message);

}  // namespace pkg::devflow
