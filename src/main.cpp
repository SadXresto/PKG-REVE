// PKG Studio entry point.
#include "app/Application.h"
#include "core/Logging.h"
#include "data/Settings.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QTextStream>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

void installMessageHandler()
{
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& context,
                              const QString& message) {
        pkg::LogLevel level = pkg::LogLevel::Info;
        switch (type) {
        case QtDebugMsg:
            level = pkg::LogLevel::Debug;
            break;
        case QtInfoMsg:
            level = pkg::LogLevel::Info;
            break;
        case QtWarningMsg:
            level = pkg::LogLevel::Warning;
            break;
        case QtCriticalMsg:
        case QtFatalMsg:
            level = pkg::LogLevel::Error;
            break;
        }
        const QString category =
            context.category != nullptr ? QString::fromLatin1(context.category)
                                        : QStringLiteral("qt");
        pkg::Logger::instance().append(level, category, pkg::Logger::redact(message));
    });
}

#ifdef Q_OS_WIN
/// The Windows build is a GUI-subsystem executable, so double-clicking it does
/// not open a console window. Command-line modes still need one: attach to the
/// caller's console, or create one when launched from Explorer.
bool wantsConsoleOutput(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--selftest") || arg == QLatin1String("--help")
            || arg == QLatin1String("-h") || arg == QLatin1String("-?")
            || arg == QLatin1String("--version") || arg == QLatin1String("-v"))
            return true;
    }
    return false;
}

void attachConsoleIfRequested(int argc, char* argv[])
{
    if (!wantsConsoleOutput(argc, argv) || GetConsoleWindow() != nullptr)
        return;
    // A redirected stdout/stderr (a pipe or a file) already works without a
    // console, and attaching would steal it.
    const HANDLE standardOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (standardOut != nullptr && standardOut != INVALID_HANDLE_VALUE
        && GetFileType(standardOut) != FILE_TYPE_UNKNOWN)
        return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        AllocConsole();
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
}
#endif

}  // namespace

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    attachConsoleIfRequested(argc, argv);
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PKGStudio"));
    QApplication::setApplicationDisplayName(QStringLiteral("PKG Studio"));
    QApplication::setApplicationVersion(QStringLiteral(PKGSTUDIO_VERSION));
    QApplication::setOrganizationName(QStringLiteral("PKG Studio"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/pkgstudio-logo.png")));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("PKG Studio - package analysis and authorized binary development."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption fileOption(
        {QStringLiteral("f"), QStringLiteral("file")},
        QStringLiteral("Open a file on startup (may be repeated)."),
        QStringLiteral("path"));
    const QCommandLineOption projectOption(
        {QStringLiteral("p"), QStringLiteral("project")},
        QStringLiteral("Open a project folder on startup."), QStringLiteral("path"));
    const QCommandLineOption themeOption(
        QStringLiteral("theme"), QStringLiteral("Start with a theme id, for example "
                                                "pkgstudio.cyberpunk."),
        QStringLiteral("id"));
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Render the main window to a PNG file and exit (diagnostics)."),
        QStringLiteral("path"));
    const QCommandLineOption screenshotTabOption(
        QStringLiteral("screenshot-tab"),
        QStringLiteral("View to show in the diagnostic screenshot: hex, analysis, code, "
                       "inspector, structure or chat."),
        QStringLiteral("tab"));
    const QCommandLineOption screenshotDialogOption(
        QStringLiteral("screenshot-dialog"),
        QStringLiteral("Capture a dialog instead of the main window: about, "
                       "feature-status or ai. Requires --screenshot."),
        QStringLiteral("id"));
    const QCommandLineOption selfTestOption(
        QStringLiteral("selftest"),
        QStringLiteral("Start, open --file inputs, verify analysis and exit."));

    parser.addOption(fileOption);
    parser.addOption(projectOption);
    parser.addOption(themeOption);
    parser.addOption(screenshotOption);
    parser.addOption(screenshotTabOption);
    parser.addOption(screenshotDialogOption);
    parser.addOption(selfTestOption);
    parser.process(app);

    installMessageHandler();

    pkg::StartupOptions options;
    options.files = parser.values(fileOption);
    options.projectPath = parser.value(projectOption);
    options.themeId = parser.value(themeOption);
    options.screenshotPath = parser.value(screenshotOption);
    options.screenshotTab = parser.value(screenshotTabOption);
    options.screenshotDialog = parser.value(screenshotDialogOption);
    options.selfTest = parser.isSet(selfTestOption);

    pkg::Application application(app);
    if (!application.initialize(options)) {
        QTextStream(stderr) << QStringLiteral("Startup failed: %1\n")
                                   .arg(application.startupError());
        return 1;
    }
    return application.run(options);
}
