#include "devflow/ErrorIntelligence.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace pkg::devflow {
namespace {

const QRegularExpression& gccPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^\s*(.+?):(\d+)(?::(\d+))?:\s*(fatal error|error|warning|note):\s*(.*)$)"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

const QRegularExpression& msvcPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^\s*(.+?)\((\d+)(?:,(\d+))?\)\s*:\s*(fatal error|error|warning|note)\s*([A-Za-z]+\d+)?\s*:\s*(.*)$)"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

const QRegularExpression& ninjaPattern()
{
    static const QRegularExpression pattern(QStringLiteral(R"(^FAILED:\s+(.*)$)"));
    return pattern;
}

DiagnosticSeverity severityFromText(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QLatin1String("fatal error"))
        return DiagnosticSeverity::Fatal;
    if (normalized == QLatin1String("warning"))
        return DiagnosticSeverity::Warning;
    if (normalized == QLatin1String("note"))
        return DiagnosticSeverity::Note;
    return DiagnosticSeverity::Error;
}

QString normalizedMessage(const QString& message)
{
    QString text = message.toLower();
    text.remove(QRegularExpression(QStringLiteral(R"(\s+)")));
    return text;
}

}  // namespace

QString severityName(DiagnosticSeverity severity)
{
    switch (severity) {
    case DiagnosticSeverity::Note:
        return QStringLiteral("Note");
    case DiagnosticSeverity::Warning:
        return QStringLiteral("Warning");
    case DiagnosticSeverity::Error:
        return QStringLiteral("Error");
    case DiagnosticSeverity::Fatal:
        return QStringLiteral("Fatal");
    }
    return QStringLiteral("Error");
}

QString BuildDiagnostic::fingerprint() const
{
    // Deliberately excludes file, line and directory: the same error in two
    // files is a duplicate, and a diagnostic that survives an edit keeps its
    // identity so its resolved state still applies.
    return QStringLiteral("%1|%2|%3")
        .arg(severityName(severity), code.toLower(), normalizedMessage(message));
}

QString DiagnosticGroup::summary() const
{
    QString result = message;
    if (count > 1)
        result += QStringLiteral(" (x%1)").arg(count);
    return result;
}

int ErrorReport::errorCount() const
{
    int count = 0;
    for (const BuildDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error ||
            diagnostic.severity == DiagnosticSeverity::Fatal) {
            ++count;
        }
    }
    return count;
}

int ErrorReport::warningCount() const
{
    int count = 0;
    for (const BuildDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Warning)
            ++count;
    }
    return count;
}

ErrorReport parseBuildOutput(const QString& output, int maxDiagnostics)
{
    ErrorReport report;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (const QString& line : lines) {
        if (line.trimmed().isEmpty())
            continue;
        if (static_cast<int>(report.diagnostics.size()) >= maxDiagnostics) {
            report.truncated = true;
            break;
        }

        QRegularExpressionMatch match = gccPattern().match(line);
        if (match.hasMatch()) {
            BuildDiagnostic diagnostic;
            diagnostic.file = match.captured(1).trimmed();
            diagnostic.line = match.captured(2).toInt();
            diagnostic.column = match.captured(3).toInt();
            diagnostic.severity = severityFromText(match.captured(4));
            diagnostic.message = match.captured(5).trimmed();
            diagnostic.raw = line.trimmed();
            report.diagnostics.append(diagnostic);
            continue;
        }

        match = msvcPattern().match(line);
        if (match.hasMatch()) {
            BuildDiagnostic diagnostic;
            diagnostic.file = match.captured(1).trimmed();
            diagnostic.line = match.captured(2).toInt();
            diagnostic.column = match.captured(3).toInt();
            diagnostic.severity = severityFromText(match.captured(4));
            diagnostic.code = match.captured(5).trimmed();
            diagnostic.message = match.captured(6).trimmed();
            diagnostic.raw = line.trimmed();
            report.diagnostics.append(diagnostic);
            continue;
        }

        if (ninjaPattern().match(line).hasMatch()) {
            // Ninja's FAILED line names the target, not a source location; it
            // is useful context but must not be invented into a diagnostic.
            if (report.unparsedLines.size() < 200)
                report.unparsedLines.append(line.trimmed());
            continue;
        }
        if (report.unparsedLines.size() < 200)
            report.unparsedLines.append(line.trimmed());
    }
    return report;
}

QVector<DiagnosticGroup> groupDiagnostics(const QVector<BuildDiagnostic>& diagnostics)
{
    QVector<DiagnosticGroup> groups;
    QHash<QString, int> indexByFingerprint;
    for (const BuildDiagnostic& diagnostic : diagnostics) {
        const QString fingerprint = diagnostic.fingerprint();
        const auto it = indexByFingerprint.constFind(fingerprint);
        if (it != indexByFingerprint.constEnd()) {
            ++groups[it.value()].count;
            continue;
        }
        DiagnosticGroup group;
        group.fingerprint = fingerprint;
        group.severity = diagnostic.severity;
        group.file = diagnostic.file;
        group.line = diagnostic.line;
        group.code = diagnostic.code;
        group.message = diagnostic.message;
        group.count = 1;
        group.explanation = explainDiagnostic(diagnostic.message);
        group.suggestion = suggestFix(diagnostic.message);
        indexByFingerprint.insert(fingerprint, static_cast<int>(groups.size()));
        groups.append(group);
    }
    std::stable_sort(groups.begin(), groups.end(),
                     [](const DiagnosticGroup& a, const DiagnosticGroup& b) {
                         if (a.severity != b.severity)
                             return static_cast<int>(a.severity) > static_cast<int>(b.severity);
                         return a.count > b.count;
                     });
    return groups;
}

QString explainDiagnostic(const QString& message)
{
    const QString text = message.toLower();
    if (text.contains(QStringLiteral("no such file or directory")) &&
        (text.contains(QStringLiteral(".h")) || text.contains(QStringLiteral("include")))) {
        return QStringLiteral(
            "The compiler could not find a header that this file includes. Either the header "
            "is not installed, or its folder is not on the include path.");
    }
    if (text.contains(QStringLiteral("undefined reference")) ||
        text.contains(QStringLiteral("unresolved external"))) {
        return QStringLiteral(
            "A function or variable is declared but the compiler cannot find its "
            "implementation. The definition is missing, or the library that contains it is "
            "not being linked.");
    }
    if (text.contains(QStringLiteral("cannot find -l")) ||
        (text.contains(QStringLiteral("cannot open file")) &&
         text.contains(QStringLiteral(".lib")))) {
        return QStringLiteral(
            "The linker was asked to link a library that is not installed or is not on the "
            "library search path.");
    }
    if (text.contains(QStringLiteral("redefinition")) ||
        text.contains(QStringLiteral("already defined")) ||
        text.contains(QStringLiteral("multiple definition"))) {
        return QStringLiteral(
            "The same name is defined more than once. This usually means a function was written "
            "in a header without 'inline', or two source files define the same symbol.");
    }
    if (text.contains(QStringLiteral("expected")) &&
        (text.contains(QLatin1Char(';')) || text.contains(QLatin1Char(')')) ||
         text.contains(QLatin1Char('}')))) {
        return QStringLiteral(
            "The parser reached a point where the code is not valid C++. The reported position "
            "is often just after the real mistake, such as a missing semicolon or brace.");
    }
    if (text.contains(QStringLiteral("is not a member of")) ||
        text.contains(QStringLiteral("has no member"))) {
        return QStringLiteral(
            "Code uses a name that the type or namespace does not provide. It may need a "
            "different header, a namespace prefix, or the API changed between versions.");
    }
    if (text.contains(QStringLiteral("permission denied"))) {
        return QStringLiteral(
            "A file could not be written. It may be read-only, open in another program, or "
            "owned by another user.");
    }
    if (text.contains(QStringLiteral("no rule to make target"))) {
        return QStringLiteral(
            "The build system refers to a file or target that does not exist, usually because "
            "a source file was moved or a generated file was not produced.");
    }
    if (text.contains(QStringLiteral("was not declared in this scope")) ||
        text.contains(QStringLiteral("undeclared identifier"))) {
        return QStringLiteral(
            "A name is used before it is declared. Check for a missing include, a typo, or a "
            "declaration that only exists inside another scope.");
    }
    return {};
}

QString suggestFix(const QString& message)
{
    const QString text = message.toLower();
    if (text.contains(QStringLiteral("no such file or directory"))) {
        return QStringLiteral(
            "Check the include spelling first, then whether the package that provides the "
            "header is installed. If it is installed, add its include directory to the target's "
            "include path (for example target_include_directories).");
    }
    if (text.contains(QStringLiteral("undefined reference")) ||
        text.contains(QStringLiteral("unresolved external"))) {
        return QStringLiteral(
            "Add the library that defines the missing symbol to the target's link libraries, "
            "and confirm the built library file is the right architecture and configuration.");
    }
    if (text.contains(QStringLiteral("cannot find -l"))) {
        return QStringLiteral(
            "Install the development package for that library, or add the folder that contains "
            "it to the linker search path.");
    }
    if (text.contains(QStringLiteral("permission denied"))) {
        return QStringLiteral(
            "Close any program using the file, then check its permissions or build into a "
            "different directory.");
    }
    if (text.contains(QStringLiteral("no rule to make target"))) {
        return QStringLiteral(
            "Search the build files for that file name and update or regenerate it, so every "
            "listed source exists.");
    }
    if (text.contains(QStringLiteral("expected")) ||
        text.contains(QStringLiteral("was not declared in this scope"))) {
        return QStringLiteral(
            "Inspect the reported line and the line just before it. Adding the missing include "
            "or punctuation is safer than changing the reported line alone.");
    }
    return {};
}

}  // namespace pkg::devflow
