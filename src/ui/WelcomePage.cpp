#include "ui/WelcomePage.h"

#include <QFileInfo>
#include <QFont>
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

    m_buildLabel = new QLabel(this);
    m_buildLabel->setObjectName(QStringLiteral("buildIdentityLabel"));
    m_buildLabel->setText(QStringLiteral("Development build"));
    layout->addWidget(m_buildLabel);

    m_projectLabel = new QLabel(this);
    layout->addWidget(m_projectLabel);

    auto* demoHeader = new QLabel(QStringLiteral("START HERE"), this);
    demoHeader->setObjectName(QStringLiteral("sideBarHeader"));
    layout->addWidget(demoHeader);

    m_demoHint = new QLabel(
        QStringLiteral("Open a synthetic sample to see format analysis, the live byte inspector, "
                       "byte editing and the structure builder. Every sample is generated for "
                       "this project and contains no commercial content."),
        this);
    m_demoHint->setObjectName(QStringLiteral("hintLabel"));
    m_demoHint->setWordWrap(true);
    layout->addWidget(m_demoHint);

    auto* demoButtons = new QHBoxLayout();
    demoButtons->setSpacing(8);
    m_demoElf = new QPushButton(QStringLiteral("Open ELF sample"), this);
    m_demoElf->setObjectName(QStringLiteral("demoElfButton"));
    m_demoApk = new QPushButton(QStringLiteral("Open APK sample"), this);
    m_demoApk->setObjectName(QStringLiteral("demoApkButton"));
    m_demoProject = new QPushButton(QStringLiteral("Open sample project"), this);
    m_demoProject->setObjectName(QStringLiteral("demoProjectButton"));
    demoButtons->addWidget(m_demoElf);
    demoButtons->addWidget(m_demoApk);
    demoButtons->addWidget(m_demoProject);
    demoButtons->addStretch(1);
    layout->addLayout(demoButtons);

    auto* demoFooter = new QHBoxLayout();
    auto* whatWorks = new QPushButton(QStringLiteral("What works in this build?"), this);
    whatWorks->setObjectName(QStringLiteral("featureStatusButton"));
    whatWorks->setToolTip(QStringLiteral("Implemented, partial and planned features"));
    demoFooter->addWidget(whatWorks);
    demoFooter->addStretch(1);
    layout->addLayout(demoFooter);

    auto* tipsHeader = new QLabel(QStringLiteral("DEMO TIPS"), this);
    tipsHeader->setObjectName(QStringLiteral("sideBarHeader"));
    layout->addWidget(tipsHeader);

    auto* tips = new QLabel(
        QStringLiteral("• Hover a byte for the live inspector; Ctrl+F searches hex or text.\n"
                       "• Ctrl+Shift+B opens the structure builder with a built-in template.\n"
                       "• Ctrl+Shift+P searches every command, setting and help page."),
        this);
    tips->setObjectName(QStringLiteral("hintLabel"));
    tips->setWordWrap(true);
    layout->addWidget(tips);

    auto* actionsHeader = new QLabel(QStringLiteral("OPEN FILES AND PROJECTS"), this);
    actionsHeader->setObjectName(QStringLiteral("sideBarHeader"));
    layout->addWidget(actionsHeader);

    auto* buttons = new QHBoxLayout();
    auto* openFile = new QPushButton(QStringLiteral("Open file…"), this);
    auto* openProject = new QPushButton(QStringLiteral("Open project folder…"), this);
    auto* newProject = new QPushButton(QStringLiteral("New project…"), this);
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
                       "licence grants no rights to third-party material. No telemetry and no "
                       "upload; the optional AI assistant contacts only the endpoint you set."),
        this);
    notice->setObjectName(QStringLiteral("hintLabel"));
    notice->setWordWrap(true);
    layout->addWidget(notice);

    connect(openFile, &QPushButton::clicked, this, &WelcomePage::openFileRequested);
    connect(openProject, &QPushButton::clicked, this, &WelcomePage::openProjectRequested);
    connect(newProject, &QPushButton::clicked, this, &WelcomePage::newProjectRequested);
    connect(m_demoElf, &QPushButton::clicked, this,
            [this]() { emit sampleFileRequested(QStringLiteral("sample.elf")); });
    connect(m_demoApk, &QPushButton::clicked, this,
            [this]() { emit sampleFileRequested(QStringLiteral("sample.apk")); });
    connect(m_demoProject, &QPushButton::clicked, this,
            &WelcomePage::sampleProjectRequested);
    connect(whatWorks, &QPushButton::clicked, this, &WelcomePage::featureStatusRequested);
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
        auto* item = new QListWidgetItem(
            QStringLiteral("Nothing opened yet. The sample buttons above are the quickest tour."),
            m_recent);
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

void WelcomePage::setDevelopmentBuild(const QString& version)
{
    m_buildLabel->setText(
        QStringLiteral("Development build %1 · demo samples included · not a signed release")
            .arg(version));
    m_buildLabel->setToolTip(QStringLiteral("PKG Studio is under active development. Help → "
                                            "Feature status lists what is implemented, partial "
                                            "and planned."));
}

void WelcomePage::setDemoSamples(bool available, const QString& directory)
{
    m_samplesAvailable = available;
    m_samplesDirectory = directory;

    m_demoElf->setEnabled(available);
    m_demoApk->setEnabled(available);
    m_demoProject->setEnabled(available);

    const QString unavailable =
        QStringLiteral("Sample files are not installed next to this build. Use Open file… to "
                       "open your own authorized file.");
    m_demoElf->setToolTip(available ? QStringLiteral("Open %1/sample.elf").arg(directory)
                                    : unavailable);
    m_demoApk->setToolTip(available ? QStringLiteral("Open %1/sample.apk").arg(directory)
                                    : unavailable);
    m_demoProject->setToolTip(
        available ? QStringLiteral("Show the sample folder in the explorer: %1").arg(directory)
                  : unavailable);

    if (!available) {
        m_demoHint->setText(
            QStringLiteral("The bundled sample files were not found next to this application. "
                           "You can still open any file you are authorized to inspect."));
    } else if (m_demoHint->text().isEmpty()) {
        m_demoHint->setText(QStringLiteral("Samples: %1").arg(directory));
    }
}

}  // namespace pkg::ui
