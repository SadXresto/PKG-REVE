#pragma once

#include "data/Database.h"
#include "data/Settings.h"

#include <QObject>
#include <QString>
#include <QStringList>

class QApplication;

namespace pkg::ui {
class MainWindow;
}

namespace pkg {

struct StartupOptions {
    QStringList files;
    QString projectPath;
    QString themeId;
    QString screenshotPath;
    QString screenshotTab;
    /// With --screenshot-tab gameforge: which workspace tab to show
    /// (inventory, graph, config, portability or intel; graph is the default).
    QString screenshotGameForgeTab;
    /// With --screenshot: capture a dialog instead of the main window
    /// ("about" or "feature-status").
    QString screenshotDialog;
    bool selfTest = false;
};

/// Owns process level state: logging, database, settings, theme and the main
/// window. Kept separate from main() so that headless self-test and screenshot
/// modes exercise the same startup path as an interactive session.
class Application : public QObject {
    Q_OBJECT

public:
    explicit Application(QApplication& app, QObject* parent = nullptr);
    ~Application() override;

    bool initialize(const StartupOptions& options);
    int run(const StartupOptions& options);

    [[nodiscard]] ui::MainWindow* window() const { return m_window; }
    [[nodiscard]] Database& database() { return m_database; }
    [[nodiscard]] const QString& startupError() const { return m_startupError; }

private:
    void scheduleSelfTest();
    void printSelfTestReport(bool passed, const QString& reason) const;

    QApplication& m_app;
    Database m_database;
    ui::MainWindow* m_window = nullptr;
    QString m_startupError;
    bool m_initialized = false;
};

}  // namespace pkg
