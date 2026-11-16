#include "ui/RecentPanel.h"

#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QVBoxLayout>

namespace pkg::ui {

RecentPanel::RecentPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("sideBar"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QLabel(QStringLiteral("RECENT"), this);
    header->setObjectName(QStringLiteral("sideBarHeader"));
    layout->addWidget(header);

    m_list = new QListWidget(this);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setAlternatingRowColors(false);
    layout->addWidget(m_list, 1);

    m_emptyHint = new QLabel(
        QStringLiteral("Nothing opened yet.\nUse File \u2192 Open file or open a project folder."),
        this);
    m_emptyHint->setObjectName(QStringLiteral("sideBarHint"));
    m_emptyHint->setWordWrap(true);
    layout->addWidget(m_emptyHint);

    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (item == nullptr)
            return;
        emit entryActivated(item->data(Qt::UserRole).toString(),
                            item->data(Qt::UserRole + 1).toString());
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item == nullptr)
            return;
        emit entryActivated(item->data(Qt::UserRole).toString(),
                            item->data(Qt::UserRole + 1).toString());
    });
    connect(m_list, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& position) {
                QListWidgetItem* item = m_list->itemAt(position);
                if (item == nullptr)
                    return;
                const QString path = item->data(Qt::UserRole).toString();
                QMenu menu(this);
                QAction* open = menu.addAction(QStringLiteral("Open"));
                QAction* remove = menu.addAction(QStringLiteral("Remove from list"));
                menu.addSeparator();
                QAction* clear = menu.addAction(QStringLiteral("Clear recent list"));
                QAction* chosen = menu.exec(m_list->viewport()->mapToGlobal(position));
                if (chosen == open)
                    emit entryActivated(path, item->data(Qt::UserRole + 1).toString());
                else if (chosen == remove)
                    emit removeRequested(path);
                else if (chosen == clear)
                    emit clearRequested();
            });
}

void RecentPanel::setEntries(const QList<RecentEntry>& entries)
{
    m_list->clear();
    for (const RecentEntry& entry : entries) {
        const QFileInfo info(entry.path);
        const QString label = QStringLiteral("%1\n%2").arg(info.fileName(), entry.path);
        auto* item = new QListWidgetItem(label, m_list);
        item->setToolTip(QStringLiteral("%1\nopened %2")
                             .arg(entry.path,
                                  entry.openedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        item->setData(Qt::UserRole, entry.path);
        item->setData(Qt::UserRole + 1, entry.kind);
    }
    m_emptyHint->setVisible(entries.isEmpty());
}

}  // namespace pkg::ui
