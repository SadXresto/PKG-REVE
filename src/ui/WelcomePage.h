#pragma once

#include "data/Database.h"

#include <QWidget>

class QLabel;
class QListWidget;

namespace pkg::ui {

class WelcomePage : public QWidget {
    Q_OBJECT

public:
    explicit WelcomePage(QWidget* parent = nullptr);

    void setProjectName(const QString& name);
    void setRecent(const QList<RecentEntry>& entries);

signals:
    void openFileRequested();
    void openProjectRequested();
    void newProjectRequested();
    void recentActivated(const QString& path, const QString& kind);

private:
    QLabel* m_projectLabel = nullptr;
    QListWidget* m_recent = nullptr;
};

}  // namespace pkg::ui
