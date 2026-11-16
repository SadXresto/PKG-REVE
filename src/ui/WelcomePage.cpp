#include "ui/WelcomePage.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace pkg::ui {

WelcomePage::WelcomePage(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("PKG STUDIO"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 10);
    titleFont.setBold(true);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* subtitle = new QLabel(
        QStringLiteral("Package analysis, metadata inspection and authorized binary "
                       "development."),
        this);
    subtitle->setObjectName(QStringLiteral("hintLabel"));
    layout->addWidget(subtitle);

    m_projectLabel = new QLabel(this);
    layout->addWidget(m_projectLabel);

    auto* buttons = new QHBoxLayout();
    auto* openFile = new QPushButton(QStringLiteral("Open file\u2026"), this);
    auto* openProject = new QPushButton(QStringLiteral("Open project folder\u2026"), this);
    auto* newProject = new QPushButton(QStringLiteral("New project\u2026"), this);
    buttons->addWidget(openFile);
    buttons->addWidget(openProject);
    buttons->addWidget(newProject);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    auto* recentLabel = new QLabel(QStringLiteral("RECENT"), this);
    recentLabel->setObjectName(QStringLiteral("sideBarHeader"));
    layout->addWidget(recentLabel);

    m_recent = new QListWidget(this);
    m_recent->setAlternatingRowColors(false);
    layout->addWidget(m_recent, 1);

    auto* notice = new QLabel(
        QStringLiteral("PKG Studio analyzes files you are authorized to inspect. It does not "
                       "decrypt or extract protected commercial content, and a PKG Studio "
                       "licence grants no rights to third-party material."),
        this);
    notice->setObjectName(QStringLiteral("hintLabel"));
    notice->setWordWrap(true);
    layout->addWidget(notice);

    connect(openFile, &QPushButton::clicked, this, &WelcomePage::openFileRequested);
    connect(openProject, &QPushButton::clicked, this, &WelcomePage::openProjectRequested);
    connect(newProject, &QPushButton::clicked, this, &WelcomePage::newProjectRequested);
    connect(m_recent, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (item != nullptr)
            emit recentActivated(item->data(Qt::UserRole).toString(),
                                 item->data(Qt::UserRole + 1).toString());
    });
    connect(m_recent, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item != nullptr)
            emit recentActivated(item->data(Qt::UserRole).toString(),
                                 item->data(Qt::UserRole + 1).toString());
    });
}

void WelcomePage::setProjectName(const QString& name)
{
    m_projectLabel->setText(name.isEmpty()
                                ? QStringLiteral("No project folder is open.")
                                : QStringLiteral("Project: %1").arg(name));
}

void WelcomePage::setRecent(const QList<RecentEntry>& entries)
{
    m_recent->clear();
    if (entries.isEmpty()) {
        auto* item = new QListWidgetItem(QStringLiteral("Nothing opened yet."), m_recent);
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        return;
    }
    for (const RecentEntry& entry : entries) {
        const QFileInfo info(entry.path);
        auto* item = new QListWidgetItem(
            QStringLiteral("[%1]  %2").arg(entry.kind, info.fileName()), m_recent);
        item->setToolTip(entry.path);
        item->setData(Qt::UserRole, entry.path);
        item->setData(Qt::UserRole + 1, entry.kind);
    }
}

}  // namespace pkg::ui
