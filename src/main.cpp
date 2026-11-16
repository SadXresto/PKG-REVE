// PKG Studio entry point.
#include "app/Application.h"
#include "core/Logging.h"
#include "data/Settings.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

#include <cstdio>

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

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PKGStudio"));
    QApplication::setApplicationDisplayName(QStringLiteral("PKG Studio"));
    QApplication::setApplicationVersion(QStringLiteral(PKGSTUDIO_VERSION));
    QApplication::setOrganizationName(QStringLiteral("PKG Studio"));

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
        QStringLiteral("Tab to show in the diagnostic screenshot: hex or analysis."),
        QStringLiteral("tab"));
    const QCommandLineOption selfTestOption(
        QStringLiteral("selftest"),
        QStringLiteral("Start, open --file inputs, verify analysis and exit."));

    parser.addOption(fileOption);
    parser.addOption(projectOption);
    parser.addOption(themeOption);
    parser.addOption(screenshotOption);
    parser.addOption(screenshotTabOption);
    parser.addOption(selfTestOption);
    parser.process(app);

    installMessageHandler();

    pkg::StartupOptions options;
    options.files = parser.values(fileOption);
    options.projectPath = parser.value(projectOption);
    options.themeId = parser.value(themeOption);
    options.screenshotPath = parser.value(screenshotOption);
    options.screenshotTab = parser.value(screenshotTabOption);
    options.selfTest = parser.isSet(selfTestOption);

    pkg::Application application(app);
    if (!application.initialize(options)) {
        QTextStream(stderr) << QStringLiteral("Startup failed: %1\n")
                                   .arg(application.startupError());
        return 1;
    }
    return application.run(options);
}
