#include "core/gameforge/BuildConfig.h"

#include "core/Logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace pkg::gameforge {
namespace {

constexpr auto kLogCategory = "gameforge";
constexpr int kEvidenceLength = 160;

QString collapsed(const QString& text, int limit = kEvidenceLength)
{
    QString out;
    out.reserve(std::min<int>(limit, static_cast<int>(text.size())));
    bool previousSpace = false;
    for (const QChar ch : text) {
        if (out.size() >= limit) {
            out += QStringLiteral("\u2026");
            break;
        }
        const bool space = ch.isSpace();
        if (space) {
            if (!previousSpace && !out.isEmpty())
                out += QLatin1Char(' ');
        } else {
            out += ch;
        }
        previousSpace = space;
    }
    return out.trimmed();
}

QVector<int> lineStartsOf(const QString& text)
{
    QVector<int> starts;
    starts.append(0);
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\n'))
            starts.append(static_cast<int>(i) + 1);
    }
    return starts;
}

int lineFor(const QVector<int>& starts, qsizetype index)
{
    const auto it = std::upper_bound(starts.constBegin(), starts.constEnd(),
                                     static_cast<int>(index));
    return static_cast<int>(it - starts.constBegin());
}

struct Call {
    QString name;
    QStringList args;
    int line = 0;
    QString evidence;
};

/// Splits an argument list on whitespace, honouring quotes, keeping generator
/// expressions and list brackets together, and ignoring `#` comments.
QStringList splitArgs(const QString& arguments)
{
    QStringList out;
    QString current;
    QChar quote;
    bool inQuote = false;
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const QChar ch = arguments.at(i);
        if (inQuote) {
            if (ch == QLatin1Char('\\') && i + 1 < arguments.size()) {
                current += arguments.at(++i);
                continue;
            }
            if (ch == quote)
                inQuote = false;
            else
                current += ch;
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            inQuote = true;
            quote = ch;
            continue;
        }
        if (ch == QLatin1Char('#')) {
            while (i < arguments.size() && arguments.at(i) != QLatin1Char('\n'))
                ++i;
            continue;
        }
        if (ch.isSpace()) {
            if (!current.isEmpty()) {
                out.append(current);
                current.clear();
            }
            continue;
        }
        current += ch;
    }
    if (!current.isEmpty())
        out.append(current);
    return out;
}

/// Extracts `name(args...)` calls. Comments are skipped at top level, quoted
/// text is honoured and an unterminated call simply stops the scan.
QVector<Call> parseCalls(const QString& text, int maxCommands)
{
    QVector<Call> calls;
    const QVector<int> starts = lineStartsOf(text);
    const qsizetype size = text.size();
    qsizetype i = 0;
    while (i < size && calls.size() < maxCommands) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('#')) {
            while (i < size && text.at(i) != QLatin1Char('\n'))
                ++i;
            continue;
        }
        if (!(ch.isLetter() || ch == QLatin1Char('_'))) {
            ++i;
            continue;
        }
        const qsizetype nameStart = i;
        while (i < size && (text.at(i).isLetterOrNumber() || text.at(i) == QLatin1Char('_')))
            ++i;
        const QString name = text.mid(nameStart, i - nameStart);
        qsizetype open = i;
        while (open < size && text.at(open).isSpace())
            ++open;
        if (open >= size || text.at(open) != QLatin1Char('(')) {
            i = open;
            continue;
        }

        qsizetype depth = 0;
        QChar quote;
        bool inQuote = false;
        qsizetype close = -1;
        for (qsizetype k = open; k < size; ++k) {
            const QChar inner = text.at(k);
            if (inQuote) {
                if (inner == QLatin1Char('\\')) {
                    ++k;
                    continue;
                }
                if (inner == quote)
                    inQuote = false;
                continue;
            }
            if (inner == QLatin1Char('"') || inner == QLatin1Char('\'')) {
                inQuote = true;
                quote = inner;
                continue;
            }
            if (inner == QLatin1Char('#')) {
                while (k < size && text.at(k) != QLatin1Char('\n'))
                    ++k;
                continue;
            }
            if (inner == QLatin1Char('(')) {
                ++depth;
            } else if (inner == QLatin1Char(')')) {
                --depth;
                if (depth == 0) {
                    close = k;
                    break;
                }
            }
        }
        if (close < 0) {
            // Unterminated call: report nothing and stop parsing this file.
            break;
        }

        Call call;
        call.name = name;
        call.line = lineFor(starts, nameStart);
        call.evidence = collapsed(text.mid(nameStart, close - nameStart + 1));
        call.args = splitArgs(text.mid(open + 1, close - open - 1));
        calls.append(call);
        i = close + 1;
    }
    return calls;
}

bool looksLikeSource(const QString& token)
{
    static const QStringList suffixes = {
        QStringLiteral(".c"),   QStringLiteral(".cc"),  QStringLiteral(".cpp"),
        QStringLiteral(".cxx"), QStringLiteral(".h"),   QStringLiteral(".hpp"),
        QStringLiteral(".hxx"), QStringLiteral(".m"),   QStringLiteral(".mm"),
        QStringLiteral(".cs"),  QStringLiteral(".glsl"), QStringLiteral(".hlsl"),
        QStringLiteral(".vert"), QStringLiteral(".frag"), QStringLiteral(".rc"),
    };
    for (const QString& suffix : suffixes) {
        if (token.endsWith(suffix, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

/// Recognises an architecture requirement in flags or variables.
QString architectureFrom(const QString& value)
{
    if (value.contains(QLatin1String("-m32"))
        || value.contains(QLatin1String("/arch:x86"))
        || value.contains(QLatin1String("CMAKE_SIZEOF_VOID_P 4")))
        return QStringLiteral("32-bit");
    if (value.contains(QLatin1String("-m64"))
        || value.contains(QLatin1String("/arch:x64"))
        || value.contains(QLatin1String("CMAKE_SIZEOF_VOID_P 8")))
        return QStringLiteral("64-bit");
    return {};
}

bool isCmakeLinkKeyword(const QString& token)
{
    static const QSet<QString> keywords = {
        QStringLiteral("PRIVATE"), QStringLiteral("PUBLIC"), QStringLiteral("INTERFACE"),
        QStringLiteral("LINK_PRIVATE"), QStringLiteral("LINK_PUBLIC"),
        QStringLiteral("debug"), QStringLiteral("optimized"), QStringLiteral("general"),
    };
    return keywords.contains(token);
}

QString baseDependencyName(const QString& name)
{
    // SDL2::SDL2 and Qt6::Widgets name a package plus a component; the graph
    // keeps the full name so evidence stays traceable, but deduplicates on it.
    return name;
}

struct Collector {
    BuildConfigReport report;
    ConfigLimits limits;
    CancelToken token;
    bool limitReached = false;

    bool canAdd() const
    {
        return static_cast<int>(report.findings.size()) < limits.maxFindings;
    }

    void add(ConfigKind kind, const QString& name, const QString& detail, const QString& file,
             int line, const QString& evidence, bool literal = true)
    {
        if (!canAdd()) {
            limitReached = true;
            return;
        }
        ConfigFinding finding;
        finding.kind = kind;
        finding.name = name;
        finding.detail = detail;
        finding.file = file;
        finding.line = line;
        finding.evidence = evidence;
        finding.literal = literal;
        report.findings.append(finding);
    }

    void addDependency(DeclaredDependency dependency)
    {
        report.dependencies.append(dependency);
        add(ConfigKind::Dependency, dependency.name,
            QStringLiteral("%1 declares %2%3.")
                .arg(dependency.declaredBy, dependency.name,
                     dependency.version.isEmpty()
                         ? QString()
                         : QStringLiteral(" (version %1)").arg(dependency.version)),
            dependency.file, dependency.line,
            QStringLiteral("%1(%2)").arg(dependency.declaredBy, dependency.name),
            dependency.explicitDeclaration);
    }
};

void parseCmakeFile(Collector& collector, const QString& relativePath, const QString& text)
{
    const QVector<Call> calls = parseCalls(text, collector.limits.maxCommandsPerFile);
    for (const Call& call : calls) {
        if (collector.token.isCancelled())
            return;
        const QString command = call.name.toLower();
        const QStringList& args = call.args;

        if (command == QLatin1String("project")) {
            if (args.isEmpty())
                continue;
            collector.report.projectName = args.first();
            const int versionIndex = static_cast<int>(
                args.indexOf(QStringLiteral("VERSION"), Qt::CaseInsensitive));
            if (versionIndex >= 0 && versionIndex + 1 < args.size())
                collector.report.projectVersion = args.at(versionIndex + 1);
            collector.add(ConfigKind::Target, args.first(),
                          QStringLiteral("CMake project declaration%1.")
                              .arg(collector.report.projectVersion.isEmpty()
                                       ? QString()
                                       : QStringLiteral(", version %1")
                                             .arg(collector.report.projectVersion)),
                          relativePath, call.line, call.evidence);
            continue;
        }

        if (command == QLatin1String("add_executable")
            || command == QLatin1String("add_library")
            || command == QLatin1String("add_custom_target")) {
            if (args.isEmpty())
                continue;
            BuildTarget target;
            target.name = args.first();
            target.type = command == QLatin1String("add_executable")
                ? QStringLiteral("executable")
                : command == QLatin1String("add_library") ? QStringLiteral("library")
                                                          : QStringLiteral("custom target");
            target.file = relativePath;
            target.line = call.line;
            for (int i = 1; i < args.size(); ++i) {
                const QString& token = args.at(i);
                if (looksLikeSource(token) && target.sources.size() < 200)
                    target.sources.append(token);
            }
            collector.report.targets.append(target);
            collector.add(ConfigKind::Target, target.name,
                          QStringLiteral("%1 '%2' declared with %3 source entr%4.")
                              .arg(target.type, target.name)
                              .arg(target.sources.size())
                              .arg(target.sources.size() == 1 ? QStringLiteral("y")
                                                             : QStringLiteral("ies")),
                          relativePath, call.line, call.evidence);
            continue;
        }

        if (command == QLatin1String("target_link_libraries")) {
            if (args.size() < 2)
                continue;
            const QString targetName = args.first();
            BuildTarget* target = nullptr;
            for (BuildTarget& candidate : collector.report.targets) {
                if (candidate.name == targetName) {
                    target = &candidate;
                    break;
                }
            }
            for (int i = 1; i < args.size(); ++i) {
                const QString& token = args.at(i);
                if (isCmakeLinkKeyword(token) || token.startsWith(QLatin1String("$<"))
                    || token.startsWith(QLatin1String("$ {")))
                    continue;
                if (target != nullptr && target->links.size() < 200)
                    target->links.append(token);
                DeclaredDependency dependency;
                dependency.name = baseDependencyName(token);
                dependency.declaredBy = QStringLiteral("target_link_libraries");
                dependency.file = relativePath;
                dependency.line = call.line;
                // A link entry is explicit, but it does not declare where the
                // library comes from; the graph marks that separately.
                dependency.explicitDeclaration = false;
                collector.addDependency(dependency);
            }
            continue;
        }

        if (command == QLatin1String("find_package")) {
            if (args.isEmpty())
                continue;
            DeclaredDependency dependency;
            dependency.name = args.first();
            dependency.declaredBy = QStringLiteral("find_package");
            dependency.file = relativePath;
            dependency.line = call.line;
            const int versionIndex = static_cast<int>(
                args.indexOf(QStringLiteral("VERSION"), Qt::CaseInsensitive));
            if (versionIndex >= 0 && versionIndex + 1 < args.size()) {
                dependency.version = args.at(versionIndex + 1);
            } else if (args.size() > 1 && !args.at(1).isEmpty()
                       && args.at(1).at(0).isDigit()) {
                dependency.version = args.at(1);
            }
            collector.addDependency(dependency);
            continue;
        }

        if (command == QLatin1String("set") && args.size() >= 2) {
            const QString variable = args.first();
            const QString value = args.mid(1).join(QLatin1Char(' '));
            const bool isCompilerVariable =
                variable.startsWith(QLatin1String("CMAKE_"))
                && (variable.contains(QLatin1String("FLAGS"))
                    || variable.contains(QLatin1String("STANDARD"))
                    || variable.contains(QLatin1String("ARCHITECTURES"))
                    || variable.contains(QLatin1String("SIZEOF_VOID_P")));
            if (isCompilerVariable) {
                const bool architectureVariable =
                    variable.contains(QLatin1String("ARCHITECTURES"))
                    || variable.contains(QLatin1String("SIZEOF_VOID_P"));
                collector.add(architectureVariable ? ConfigKind::Architecture
                                                   : ConfigKind::CompilerSetting,
                              variable,
                              architectureVariable
                                  ? QStringLiteral("%1 is set to '%2'.").arg(variable, value)
                                  : QStringLiteral("%1 is set to '%2'.").arg(variable, value),
                              relativePath, call.line, call.evidence);
                const QString architecture = architectureFrom(value);
                if (!architecture.isEmpty())
                    collector.add(ConfigKind::Architecture,
                                  QStringLiteral("Architecture %1").arg(architecture),
                                  QStringLiteral("%1 requests a %2 build.")
                                      .arg(variable, architecture),
                                  relativePath, call.line, call.evidence, false);
            }
            continue;
        }

        if (command == QLatin1String("target_compile_options")
            || command == QLatin1String("target_compile_definitions")
            || command == QLatin1String("add_compile_options")
            || command == QLatin1String("add_compile_definitions")) {
            const QString joined = args.join(QLatin1Char(' '));
            for (const QString& token : args) {
                if (token == QLatin1String("-pthread") || token == QLatin1String("-lpthread")) {
                    collector.add(ConfigKind::CompilerSetting, QStringLiteral("POSIX threads"),
                                  QStringLiteral("Compiling with %1 requires a POSIX thread "
                                                 "library.").arg(token),
                                  relativePath, call.line, call.evidence);
                } else if (token == QLatin1String("_WIN32")
                           || token == QLatin1String("WIN32")) {
                    collector.add(ConfigKind::PlatformCondition, QStringLiteral("_WIN32 define"),
                                  QStringLiteral("The Windows preprocessor define is compiled in "
                                                 "here."),
                                  relativePath, call.line, call.evidence);
                }
            }
            const QString architecture = architectureFrom(joined);
            if (!architecture.isEmpty())
                collector.add(ConfigKind::Architecture,
                              QStringLiteral("Architecture %1").arg(architecture),
                              QStringLiteral("Compile options request a %1 build.")
                                  .arg(architecture),
                              relativePath, call.line, call.evidence, false);
            continue;
        }

        if (command == QLatin1String("if") || command == QLatin1String("elseif")) {
            const QString condition = args.join(QLatin1Char(' '));
            static const QSet<QString> platformTokens = {
                QStringLiteral("WIN32"),  QStringLiteral("MSVC"),    QStringLiteral("APPLE"),
                QStringLiteral("UNIX"),   QStringLiteral("LINUX"),   QStringLiteral("ANDROID"),
                QStringLiteral("IOS"),    QStringLiteral("MINGW"),   QStringLiteral("EMSCRIPTEN"),
            };
            QStringList matched;
            for (const QString& token : args) {
                const QString upper = token.toUpper();
                if (platformTokens.contains(upper))
                    matched.append(upper);
            }
            if (matched.isEmpty())
                continue;
            collector.add(ConfigKind::PlatformCondition, matched.join(QLatin1Char('/')),
                          QStringLiteral("Conditional branch for %1; the code inside is only "
                                         "built on those platforms.")
                              .arg(matched.join(QStringLiteral(", "))),
                          relativePath, call.line, call.evidence);
            continue;
        }

        if (command == QLatin1String("cmake_minimum_required") && !args.isEmpty()) {
            collector.add(ConfigKind::BuildFile, QStringLiteral("cmake_minimum_required"),
                          QStringLiteral("The project requires CMake %1 or newer.")
                              .arg(args.last()),
                          relativePath, call.line, call.evidence);
        }
    }
}

void parseMesonFile(Collector& collector, const QString& relativePath, const QString& text)
{
    const QVector<Call> calls = parseCalls(text, collector.limits.maxCommandsPerFile);
    for (const Call& call : calls) {
        if (collector.token.isCancelled())
            return;
        const QString command = call.name.toLower();
        // Meson separates arguments with commas; the shared tokenizer keeps them
        // attached, so normalise the list before interpreting it.
        QStringList args = call.args;
        for (QString& argument : args) {
            while (argument.endsWith(QLatin1Char(',')))
                argument.chop(1);
        }

        // Meson uses `key : value` pairs; recover them from the flat token list.
        auto keywordValue = [&args](const QString& keyword) -> QString {
            for (int i = 0; i < args.size() - 1; ++i) {
                if (args.at(i) == keyword + QLatin1Char(':'))
                    return args.at(i + 1);
                if (args.at(i) == keyword && args.at(i + 1) == QLatin1String(":")) {
                    if (i + 2 < args.size())
                        return args.at(i + 2);
                }
            }
            return {};
        };

        if (command == QLatin1String("project")) {
            if (args.isEmpty())
                continue;
            collector.report.projectName = args.first();
            collector.report.projectVersion = keywordValue(QStringLiteral("version"));
            collector.add(ConfigKind::Target, args.first(),
                          QStringLiteral("Meson project declaration%1.")
                              .arg(collector.report.projectVersion.isEmpty()
                                       ? QString()
                                       : QStringLiteral(", version %1")
                                             .arg(collector.report.projectVersion)),
                          relativePath, call.line, call.evidence);
            continue;
        }

        if (command == QLatin1String("executable") || command == QLatin1String("library")
            || command == QLatin1String("shared_library")
            || command == QLatin1String("static_library")) {
            if (args.isEmpty())
                continue;
            BuildTarget target;
            target.name = args.first();
            target.type = command == QLatin1String("executable") ? QStringLiteral("executable")
                                                                 : QStringLiteral("library");
            target.file = relativePath;
            target.line = call.line;
            for (const QString& token : args) {
                if (looksLikeSource(token) && target.sources.size() < 200)
                    target.sources.append(token);
            }
            collector.report.targets.append(target);
            collector.add(ConfigKind::Target, target.name,
                          QStringLiteral("Meson %1 '%2'.")
                              .arg(target.type, target.name),
                          relativePath, call.line, call.evidence);
            continue;
        }

        if (command == QLatin1String("dependency")) {
            if (args.isEmpty())
                continue;
            DeclaredDependency dependency;
            dependency.name = args.first();
            dependency.declaredBy = QStringLiteral("dependency()");
            dependency.version = keywordValue(QStringLiteral("version"));
            dependency.file = relativePath;
            dependency.line = call.line;
            collector.addDependency(dependency);
            continue;
        }

        if (command == QLatin1String("add_project_arguments")
            || command == QLatin1String("add_global_arguments")) {
            if (args.isEmpty())
                continue;
            collector.add(ConfigKind::CompilerSetting, command,
                          QStringLiteral("Project-wide compiler arguments: %1.")
                              .arg(args.join(QLatin1Char(' '))),
                          relativePath, call.line, call.evidence);
            const QString architecture = architectureFrom(args.join(QLatin1Char(' ')));
            if (!architecture.isEmpty())
                collector.add(ConfigKind::Architecture,
                              QStringLiteral("Architecture %1").arg(architecture),
                              QStringLiteral("Compiler arguments request a %1 build.")
                                  .arg(architecture),
                              relativePath, call.line, call.evidence, false);
            continue;
        }

        if (command == QLatin1String("subdir") && !args.isEmpty()) {
            collector.add(ConfigKind::BuildFile, QStringLiteral("subdir"),
                          QStringLiteral("Build descends into '%1'.").arg(args.first()),
                          relativePath, call.line, call.evidence);
        }
    }
}

void parseMakefile(Collector& collector, const QString& relativePath, const QString& text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    static const QSet<QString> flagVariables = {
        QStringLiteral("CFLAGS"), QStringLiteral("CXXFLAGS"), QStringLiteral("LDFLAGS"),
        QStringLiteral("CPPFLAGS"), QStringLiteral("ASFLAGS"),
    };
    for (int index = 0; index < lines.size(); ++index) {
        if (collector.token.isCancelled())
            return;
        const QString line = lines.at(index);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))
            || line.startsWith(QLatin1Char('\t')))
            continue;

        const int assignment = static_cast<int>(trimmed.indexOf(QLatin1Char('=')));
        const int colon = static_cast<int>(trimmed.indexOf(QLatin1Char(':')));
        if (assignment > 0 && (colon < 0 || assignment < colon)) {
            const QString variable = trimmed.left(assignment).remove(QLatin1Char('?')).trimmed();
            const QString value = trimmed.mid(assignment + 1).trimmed();
            if (flagVariables.contains(variable) || variable == QLatin1String("ARCH")
                || variable == QLatin1String("CC") || variable == QLatin1String("CXX")) {
                collector.add(ConfigKind::CompilerSetting, variable,
                              QStringLiteral("Makefile sets %1 to '%2'.").arg(variable, value),
                              relativePath, index + 1, collapsed(trimmed));
                if (value.contains(QLatin1String("-pthread"))) {
                    collector.add(ConfigKind::CompilerSetting, QStringLiteral("POSIX threads"),
                                  QStringLiteral("%1 contains -pthread, which needs a POSIX "
                                                 "thread library.")
                                      .arg(variable),
                                  relativePath, index + 1, collapsed(trimmed));
                }
                const QString architecture = architectureFrom(value);
                if (!architecture.isEmpty())
                    collector.add(ConfigKind::Architecture,
                                  QStringLiteral("Architecture %1").arg(architecture),
                                  QStringLiteral("%1 requests a %2 build.")
                                      .arg(variable, architecture),
                                  relativePath, index + 1, collapsed(trimmed), false);
            }
            continue;
        }

        if (colon <= 0)
            continue;
        const QString targetName = trimmed.left(colon).trimmed();
        if (targetName.isEmpty() || targetName.startsWith(QLatin1Char('.'))
            || targetName.contains(QLatin1Char(' ')) || targetName.contains(QLatin1Char('$')))
            continue;

        BuildTarget target;
        target.name = targetName;
        target.type = QStringLiteral("make target");
        target.file = relativePath;
        target.line = index + 1;
        const QStringList prerequisites =
            trimmed.mid(colon + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString& prerequisite : prerequisites) {
            if (prerequisite.startsWith(QLatin1Char('|')))
                continue;
            target.links.append(prerequisite);
        }
        collector.report.targets.append(target);
        collector.add(ConfigKind::Target, targetName,
                      QStringLiteral("Make target '%1' with %2 prerequisite(s).")
                          .arg(targetName)
                          .arg(prerequisites.size()),
                      relativePath, index + 1, collapsed(trimmed));
        for (const QString& prerequisite : prerequisites) {
            if (prerequisite.startsWith(QLatin1Char('|'))
                || prerequisite.startsWith(QLatin1Char('-')))
                continue;
            DeclaredDependency dependency;
            dependency.name = prerequisite;
            dependency.declaredBy = QStringLiteral("make prerequisite");
            dependency.file = relativePath;
            dependency.line = index + 1;
            collector.addDependency(dependency);
        }
    }
}

void parseGodotProject(Collector& collector, const QString& relativePath, const QString& text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    QString section;
    for (int index = 0; index < lines.size(); ++index) {
        if (collector.token.isCancelled())
            return;
        const QString trimmed = lines.at(index).trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char(';')))
            continue;
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            section = trimmed.mid(1, trimmed.size() - 2);
            continue;
        }
        const int equals = static_cast<int>(trimmed.indexOf(QLatin1Char('=')));
        if (equals <= 0)
            continue;
        const QString key = trimmed.left(equals).trimmed();
        QString value = trimmed.mid(equals + 1).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"'))
            && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);
        const QString evidence = collapsed(trimmed);

        if (key == QLatin1String("config/name")) {
            collector.report.projectName = value;
            collector.add(ConfigKind::Target, value,
                          QStringLiteral("Godot project name from project.godot."),
                          relativePath, index + 1, evidence);
        } else if (key == QLatin1String("config/features")) {
            // PackedStringArray("4.2", "Forward Plus")
            const int firstQuote = static_cast<int>(value.indexOf(QLatin1Char('"')));
            const int secondQuote = firstQuote >= 0
                ? static_cast<int>(value.indexOf(QLatin1Char('"'), firstQuote + 1))
                : -1;
            if (firstQuote >= 0 && secondQuote > firstQuote) {
                collector.report.engineName = QStringLiteral("Godot");
                collector.report.engineVersion =
                    value.mid(firstQuote + 1, secondQuote - firstQuote - 1);
                collector.add(ConfigKind::BuildFile, QStringLiteral("Godot %1")
                                                         .arg(collector.report.engineVersion),
                              QStringLiteral("The project targets Godot %1.")
                                  .arg(collector.report.engineVersion),
                              relativePath, index + 1, evidence);
            }
        } else if (key == QLatin1String("run/main_scene")) {
            collector.add(ConfigKind::BuildFile, QStringLiteral("main scene"),
                          QStringLiteral("The exported game starts at %1.").arg(value),
                          relativePath, index + 1, evidence);
        } else if (key == QLatin1String("renderer/rendering_method")) {
            collector.add(ConfigKind::CompilerSetting, QStringLiteral("renderer"),
                          QStringLiteral("Godot renders with the '%1' backend; the same project "
                                         "exports to Windows and Linux.")
                              .arg(value),
                          relativePath, index + 1, evidence);
        } else if (section.startsWith(QLatin1String("input"))) {
            collector.add(ConfigKind::BuildFile, QStringLiteral("input action %1").arg(key),
                          QStringLiteral("Input action declared in project.godot."),
                          relativePath, index + 1, evidence);
        }
    }
}

void parseUnrealProject(Collector& collector, const QString& relativePath, const QString& text)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (document.isNull() || !document.isObject()) {
        collector.report.warnings.append(
            QStringLiteral("%1 is not valid JSON (%2); no Unreal targets were read.")
                .arg(relativePath, error.errorString()));
        return;
    }
    const QJsonObject root = document.object();
    collector.report.engineName = QStringLiteral("Unreal Engine");
    collector.report.engineVersion = root.value(QStringLiteral("EngineAssociation")).toString();
    collector.report.projectName = QFileInfo(relativePath).completeBaseName();
    collector.add(ConfigKind::BuildFile,
                  QStringLiteral("Unreal project %1").arg(collector.report.projectName),
                  collector.report.engineVersion.isEmpty()
                      ? QStringLiteral("Unreal project descriptor without an engine association.")
                      : QStringLiteral("The project associates with engine %1.")
                            .arg(collector.report.engineVersion),
                  relativePath, 1, QStringLiteral(".uproject descriptor"));

    const QJsonArray modules = root.value(QStringLiteral("Modules")).toArray();
    for (const QJsonValue& value : modules) {
        const QJsonObject module = value.toObject();
        const QString name = module.value(QStringLiteral("Name")).toString();
        if (name.isEmpty())
            continue;
        BuildTarget target;
        target.name = name;
        target.type = QStringLiteral("module");
        target.file = relativePath;
        target.line = 1;
        collector.report.targets.append(target);
        collector.add(ConfigKind::Target, name,
                      QStringLiteral("Unreal module '%1' (%2).")
                          .arg(name, module.value(QStringLiteral("Type")).toString(
                                        QStringLiteral("module"))),
                      relativePath, 1, QStringLiteral("Modules entry"));
    }

    const QJsonArray plugins = root.value(QStringLiteral("Plugins")).toArray();
    for (const QJsonValue& value : plugins) {
        const QJsonObject plugin = value.toObject();
        const QString name = plugin.value(QStringLiteral("Name")).toString();
        if (name.isEmpty() || !plugin.value(QStringLiteral("Enabled")).toBool(true))
            continue;
        DeclaredDependency dependency;
        dependency.name = name;
        dependency.declaredBy = QStringLiteral("plugin");
        dependency.file = relativePath;
        dependency.line = 1;
        collector.addDependency(dependency);
    }
}

bool isSupportedBuildFile(const QString& relativePath)
{
    const QString name = QFileInfo(relativePath).fileName();
    return name.compare(QLatin1String("CMakeLists.txt"), Qt::CaseInsensitive) == 0
        || name.endsWith(QLatin1String(".cmake"), Qt::CaseInsensitive)
        || name.compare(QLatin1String("meson.build"), Qt::CaseInsensitive) == 0
        || name.compare(QLatin1String("Makefile"), Qt::CaseInsensitive) == 0
        || name.compare(QLatin1String("makefile"), Qt::CaseInsensitive) == 0
        || name.compare(QLatin1String("GNUmakefile"), Qt::CaseInsensitive) == 0
        || name.endsWith(QLatin1String(".mk"), Qt::CaseInsensitive)
        || name.compare(QLatin1String("project.godot"), Qt::CaseInsensitive) == 0
        || name.endsWith(QLatin1String(".uproject"), Qt::CaseInsensitive);
}

} // namespace

QString configKindName(ConfigKind kind)
{
    switch (kind) {
    case ConfigKind::Target:
        return QStringLiteral("Target");
    case ConfigKind::Dependency:
        return QStringLiteral("Dependency");
    case ConfigKind::CompilerSetting:
        return QStringLiteral("Compiler setting");
    case ConfigKind::PlatformCondition:
        return QStringLiteral("Platform condition");
    case ConfigKind::Architecture:
        return QStringLiteral("Architecture");
    case ConfigKind::BuildFile:
        break;
    }
    return QStringLiteral("Build file");
}

const BuildTarget* BuildConfigReport::findTarget(const QString& name) const
{
    for (const BuildTarget& target : targets) {
        if (target.name == name)
            return &target;
    }
    return nullptr;
}

BuildConfigReport analyzeBuildConfig(const ProjectScan& scan, const CancelToken& token,
                                     const ConfigLimits& limits)
{
    Collector collector;
    collector.limits = limits;
    collector.token = token;

    if (!scan.readable()) {
        collector.report.warnings.append(QStringLiteral("Scan a project folder first."));
        return collector.report;
    }

    QStringList candidates;
    for (const ProjectFile& file : scan.files) {
        if (isSupportedBuildFile(file.relativePath))
            candidates.append(file.relativePath);
    }
    // CMake and Meson declarations at the root explain the project best, so
    // they are parsed first even when the file list is long.
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const QString& left, const QString& right) {
                         const bool leftRoot = !left.contains(QLatin1Char('/'));
                         const bool rightRoot = !right.contains(QLatin1Char('/'));
                         return leftRoot && !rightRoot;
                     });

    if (candidates.isEmpty()) {
        collector.report.warnings.append(
            QStringLiteral("No supported build files were found (CMakeLists.txt, meson.build, "
                           "Makefile, project.godot or *.uproject)."));
        return collector.report;
    }

    int filesRead = 0;
    for (const QString& relativePath : candidates) {
        if (collector.token.isCancelled()) {
            collector.report.cancelled = true;
            break;
        }
        if (filesRead >= limits.maxFiles) {
            collector.report.truncated = true;
            collector.report.warnings.append(
                QStringLiteral("Build inspection stopped at %1 files; later files were not "
                               "read.")
                    .arg(limits.maxFiles));
            break;
        }
        QFile file(QDir(scan.rootPath).filePath(relativePath));
        if (!file.open(QIODevice::ReadOnly)) {
            collector.report.warnings.append(
                QStringLiteral("Could not read %1.").arg(relativePath));
            continue;
        }
        const qint64 size = file.size();
        const QByteArray raw = file.read(std::min(size, limits.maxBytesPerFile));
        if (raw.isEmpty())
            continue;
        if (raw.contains('\0')) {
            collector.report.warnings.append(
                QStringLiteral("%1 looks binary and was not parsed.").arg(relativePath));
            continue;
        }
        if (size > limits.maxBytesPerFile) {
            collector.report.truncated = true;
            collector.report.warnings.append(
                QStringLiteral("%1 is larger than %2 KiB; only the beginning was parsed.")
                    .arg(relativePath)
                    .arg(limits.maxBytesPerFile / 1024));
        }
        ++filesRead;
        const QString text = QString::fromUtf8(raw);
        collector.report.linesRead += static_cast<int>(text.count(QLatin1Char('\n'))) + 1;
        collector.report.analyzedFiles.append(relativePath);

        const QString name = QFileInfo(relativePath).fileName();
        if (name.compare(QLatin1String("project.godot"), Qt::CaseInsensitive) == 0)
            parseGodotProject(collector, relativePath, text);
        else if (name.endsWith(QLatin1String(".uproject"), Qt::CaseInsensitive))
            parseUnrealProject(collector, relativePath, text);
        else if (name.compare(QLatin1String("meson.build"), Qt::CaseInsensitive) == 0)
            parseMesonFile(collector, relativePath, text);
        else if (name.compare(QLatin1String("CMakeLists.txt"), Qt::CaseInsensitive) == 0
                 || name.endsWith(QLatin1String(".cmake"), Qt::CaseInsensitive))
            parseCmakeFile(collector, relativePath, text);
        else
            parseMakefile(collector, relativePath, text);
    }

    if (collector.limitReached)
        collector.report.warnings.append(
            QStringLiteral("Only the first %1 findings are kept.").arg(limits.maxFindings));
    if (collector.report.cancelled)
        collector.report.warnings.append(
            QStringLiteral("Build inspection was cancelled; results are partial."));

    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("Build config: %1 files, %2 findings, %3 targets, %4 dependencies")
                .arg(collector.report.analyzedFiles.size())
                .arg(collector.report.findings.size())
                .arg(collector.report.targets.size())
                .arg(collector.report.dependencies.size()));
    return collector.report;
}

} // namespace pkg::gameforge
