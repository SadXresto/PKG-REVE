#pragma once

#include "data/Database.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

namespace pkg::ui {

/// First screen: build identity, bundled synthetic samples, open actions and
/// recent files. Sample buttons are disabled (with a tooltip) when the demo
/// files are not installed next to the application, instead of failing later.
class WelcomePage : public QWidget {
    Q_OBJECT

public:
    explicit WelcomePage(QWidget* parent = nullptr);

    void setProjectName(const QString& name);
    void setRecent(const QList<RecentEntry>& entries);
    void setDevelopmentBuild(const QString& version);
    void setDemoSamples(bool available, const QString& directory);

signals:
    void openFileRequested();
    void openProjectRequested();
    void newProjectRequested();
    void recentActivated(const QString& path, const QString& kind);
    /// Bundled sample file name, e.g. "sample.elf".
    void sampleFileRequested(const QString& fileName);
    void sampleProjectRequested();
    void featureStatusRequested();

private:
    QLabel* m_projectLabel = nullptr;
    QLabel* m_buildLabel = nullptr;
    QLabel* m_demoHint = nullptr;
    QListWidget* m_recent = nullptr;
    QPushButton* m_demoElf = nullptr;
    QPushButton* m_demoApk = nullptr;
    QPushButton* m_demoProject = nullptr;
    QString m_samplesDirectory;
    bool m_samplesAvailable = false;
};

}  // namespace pkg::ui
