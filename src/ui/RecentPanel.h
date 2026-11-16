#pragma once

#include "data/Database.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;

namespace pkg::ui {

class RecentPanel : public QWidget {
    Q_OBJECT

public:
    explicit RecentPanel(QWidget* parent = nullptr);

    void setEntries(const QList<RecentEntry>& entries);

signals:
    void entryActivated(const QString& path, const QString& kind);
    void removeRequested(const QString& path);
    void clearRequested();

private:
    QLabel* m_emptyHint = nullptr;
    QListWidget* m_list = nullptr;
};

}  // namespace pkg::ui
