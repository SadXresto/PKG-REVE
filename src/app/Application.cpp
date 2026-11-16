#include "app/Application.h"

#include "core/FormatUtil.h"
#include "core/IFormatParser.h"
#include "core/Logging.h"
#include "core/ProcessInfo.h"
#include "data/ThemeManager.h"
#include "ui/FileEditor.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPixmap>
#include <QStyle>
#include <QStyleFactory>
#include <QThread>
#include <QTextStream>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace pkg {
namespace {

constexpr auto kLogCategory = "app";

int effectiveWorkerCount(int configured)
{
    if (configured > 0)
        return std::clamp(configured, 1, 64);
    const int cores = QThread::idealThreadCount();
    // Low-end machines often report 2-4 threads; never exceed 4 by default so
    // that the UI thread keeps its share of the CPU.
    return std::clamp(cores, 1, 4);
}

}  // namespace

Application::Application(QApplication& app, QObject* parent)
    : QObject(parent), m_app(app)
{
}

Application::~Application()
{
    m_database.close();
}

bool Application::initialize(const StartupOptions& options)
{
    if (m_initialized)
        return true;

    // A predictable, flat look; also avoids style-driven animation work on
    // low-end hardware.
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        m_app.setStyle(fusion);

    const QString dataDirectory = Settings::dataDirectory();
    if (!QDir().mkpath(dataDirectory)) {
        m_startupError = QStringLiteral("Cannot create the PKG Studio data directory: %1")
                             .arg(dataDirectory);
        return false;
    }

    Logger::instance().initialize(Settings::logsDirectory(), LogLevel::Info);
    Logger::installCrashHandler(Settings::logsDirectory(),
                                QApplication::applicationVersion());
    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("Starting PKG Studio %1").arg(QApplication::applicationVersion()));
    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("Data directory: %1").arg(dataDirectory));

    const Status opened = m_database.open(Settings::databasePath());
    if (!opened) {
        m_startupError = QStringLiteral("Cannot open the local database: %1")
                             .arg(opened.error().detail.isEmpty() ? opened.error().message
                                                                  : opened.error().detail);
        logError(QString::fromLatin1(kLogCategory), m_startupError);
        return false;
    }

    Settings::instance().attach(&m_database);
    ParserRegistry::registerBuiltins();
    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("Registered parsers: %1")
                .arg(ParserRegistry::instance().parserIds().join(QStringLiteral(", "))));

    AppSettings settings = Settings::instance().values();
    Logger::instance().setMinLevel(settings.logLevel);
    Logger::instance().setFileLoggingEnabled(settings.fileLogging);

    ThemeManager::instance().initialize(Settings::themesDirectory());
    if (!options.themeId.isEmpty() && ThemeManager::instance().contains(options.themeId))
        settings.themeId = options.themeId;
    if (!ThemeManager::instance().setCurrent(settings.themeId)) {
        logWarning(QString::fromLatin1(kLogCategory),
                   QStringLiteral("Configured theme '%1' is unavailable; using default")
                       .arg(settings.themeId));
        settings.themeId = ThemeManager::instance().current().id;
    }
    ThemeManager::apply(ThemeManager::instance().current(), m_app);

    QFont appFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (!settings.fontFamily.isEmpty())
        appFont.setFamily(settings.fontFamily);
    m_app.setFont(appFont);

    if (settings.lightweight) {
        QApplication::setEffectEnabled(Qt::UI_AnimateMenu, false);
        QApplication::setEffectEnabled(Qt::UI_AnimateCombo, false);
        QApplication::setEffectEnabled(Qt::UI_AnimateTooltip, false);
        QApplication::setEffectEnabled(Qt::UI_FadeMenu, false);
        QApplication::setEffectEnabled(Qt::UI_FadeTooltip, false);
    }

    QThreadPool::globalInstance()->setMaxThreadCount(effectiveWorkerCount(settings.workerThreads));
    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("Worker threads: %1, logical cores: %2")
                .arg(QThreadPool::globalInstance()->maxThreadCount())
                .arg(QThread::idealThreadCount()));

    m_window = new ui::MainWindow(m_database);
    m_initialized = true;
    return true;
}

int Application::run(const StartupOptions& options)
{
    if (m_window == nullptr) {
        QTextStream(stderr) << QStringLiteral("PKG Studio failed to start: %1\n")
                                   .arg(m_startupError);
        return 1;
    }

    m_window->resize(1440, 900);
    m_window->show();

    if (!options.projectPath.isEmpty())
        m_window->openProject(options.projectPath);
    for (const QString& file : options.files) {
        if (!m_window->openFile(file))
            logWarning(QString::fromLatin1(kLogCategory),
                       QStringLiteral("Could not open %1").arg(file));
    }
    const bool hasStartupContent = !options.files.isEmpty() || !options.projectPath.isEmpty();
    if (!hasStartupContent && !options.screenshotPath.isEmpty()) {
        // Show a populated workspace in documentation screenshots.
        m_window->restoreSession();
    } else if (!hasStartupContent) {
        m_window->restoreSession();
    }

    if (!options.screenshotPath.isEmpty()) {
        if (options.screenshotTab == QLatin1String("analysis"))
            m_window->showAnalysisTabForCurrentEditor();
        else if (!options.screenshotTab.isEmpty() && options.screenshotTab != QLatin1String("hex"))
            logWarning(QString::fromLatin1(kLogCategory),
                       QStringLiteral("Unknown --screenshot-tab '%1'")
                           .arg(options.screenshotTab));
        const QString path = options.screenshotPath;
        QTimer::singleShot(1500, this, [this, path]() {
            const QPixmap shot = m_window->grab();
            const bool saved = shot.save(path);
            if (saved) {
                logInfo(QString::fromLatin1(kLogCategory),
                        QStringLiteral("Screenshot saved: %1").arg(path));
                QTextStream(stdout) << QStringLiteral("screenshot: %1 (%2x%3)\n")
                                           .arg(path)
                                           .arg(shot.width())
                                           .arg(shot.height());
            } else {
                logError(QString::fromLatin1(kLogCategory),
                         QStringLiteral("Cannot save screenshot: %1").arg(path));
            }
            QCoreApplication::exit(saved ? 0 : 2);
        });
        return m_app.exec();
    }

    if (options.selfTest) {
        scheduleSelfTest();
        return m_app.exec();
    }

    return m_app.exec();
}

void Application::scheduleSelfTest()
{
    auto* timer = new QTimer(this);
    timer->setInterval(150);
    auto attempts = std::make_shared<int>(0);
    auto* window = m_window;

    connect(timer, &QTimer::timeout, this, [this, timer, attempts, window]() {
        ++(*attempts);
        const bool filesOpen = window->openFileCount() > 0;
        const bool analyzed = window->allEditorsAnalyzed();
        const bool noPending = window->activeAnalysisCount() == 0;

        if (filesOpen && analyzed && noPending) {
            timer->stop();
            printSelfTestReport(true, QStringLiteral("analysis completed"));
            QCoreApplication::exit(0);
            return;
        }
        if (*attempts >= 80) {  // ~12 seconds
            timer->stop();
            const QString reason =
                QStringLiteral("timed out (files open: %1, analyzed: %2, pending: %3)")
                    .arg(filesOpen)
                    .arg(analyzed)
                    .arg(window->activeAnalysisCount());
            printSelfTestReport(false, reason);
            QCoreApplication::exit(1);
        }
    });
    timer->start();
}

void Application::printSelfTestReport(bool passed, const QString& reason) const
{
    QTextStream out(stdout);
    out << QStringLiteral("PKG Studio self-test\n");
    out << QStringLiteral("  result: %1 (%2)\n")
               .arg(passed ? QStringLiteral("PASS") : QStringLiteral("FAIL"), reason);
    out << QStringLiteral("  version: %1\n").arg(QApplication::applicationVersion());
    out << QStringLiteral("  platform: %1\n").arg(m_app.platformName());
    out << QStringLiteral("  theme: %1\n").arg(ThemeManager::instance().current().id);
    out << QStringLiteral("  database schema: %1\n").arg(m_database.schemaVersion());
    out << QStringLiteral("  parsers: %1\n")
               .arg(ParserRegistry::instance().parserIds().join(QStringLiteral(", ")));
    out << QStringLiteral("  project: %1\n")
               .arg(m_window ? m_window->projectPath() : QString());
    out << QStringLiteral("  open files: %1\n").arg(m_window ? m_window->openFileCount() : 0);
    if (m_window != nullptr) {
        for (const QString& path : m_window->openPaths())
            out << QStringLiteral("    - %1\n").arg(path);
    }
    const auto resident = sysinfo::currentResidentBytes();
    out << QStringLiteral("  resident memory: %1\n")
               .arg(resident ? fmt::humanBytes(resident.value()) : QStringLiteral("n/a"));
    out.flush();

    logInfo(QString::fromLatin1(kLogCategory),
            QStringLiteral("Self-test %1: %2")
                .arg(passed ? QStringLiteral("passed") : QStringLiteral("failed"), reason));
}

}  // namespace pkg
