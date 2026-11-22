#include "ui/DevFlowPanel.h"

#include "core/FormatUtil.h"
#include "data/Database.h"
#include "data/Settings.h"
#include "data/ThemeManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>

namespace pkg::ui {
namespace {

constexpr auto kLogCategory = "devflow";

QStringList splitArguments(const QString& text)
{
    // Arguments are whitespace separated; quotes group a single argument. No
    // shell expansion, globbing or command substitution is performed.
    QStringList arguments;
    QString current;
    bool quoted = false;
    for (const QChar character : text) {
        if (character == QLatin1Char('"')) {
            quoted = !quoted;
            continue;
        }
        if (!quoted && character.isSpace()) {
            if (!current.isEmpty()) {
                arguments.append(current);
                current.clear();
            }
            continue;
        }
        current.append(character);
    }
    if (!current.isEmpty())
        arguments.append(current);
    return arguments;
}

QMap<QString, QString> parseEnvironment(const QString& text, QStringList* problems)
{
    QMap<QString, QString> environment;
    for (const QString& raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            if (problems != nullptr)
                problems->append(QStringLiteral("Ignored environment line: %1").arg(line));
            continue;
        }
        environment.insert(line.left(equals).trimmed(), line.mid(equals + 1));
    }
    return environment;
}

QString environmentText(const QMap<QString, QString>& environment)
{
    QStringList lines;
    for (auto it = environment.constBegin(); it != environment.constEnd(); ++it)
        lines.append(it.key() + QLatin1Char('=') + it.value());
    return lines.join(QLatin1Char('\n'));
}

QString severityColorKey(devflow::DiagnosticSeverity severity)
{
    switch (severity) {
    case devflow::DiagnosticSeverity::Fatal:
    case devflow::DiagnosticSeverity::Error:
        return QStringLiteral("hexAscii");
    case devflow::DiagnosticSeverity::Warning:
        return QStringLiteral("accent");
    case devflow::DiagnosticSeverity::Note:
        return QStringLiteral("panelText");
    }
    return QStringLiteral("panelText");
}

}  // namespace

DevFlowPanel::DevFlowPanel(Database& database, DevFlowHooks hooks, QWidget* parent)
    : QWidget(parent), m_database(database), m_hooks(std::move(hooks)), m_store(database)
{
    setObjectName(QStringLiteral("devFlowPanel"));
    m_buildRunner = new devflow::BuildRunner(this);
    m_workflowRunner = new devflow::WorkflowRunner(this);
    buildUi();

    connect(m_buildRunner, &devflow::BuildRunner::started, this, [this](const QString& command) {
        m_buildStatus->setText(QStringLiteral("Running: %1").arg(command));
        m_buildRunButton->setEnabled(false);
        m_buildCancelButton->setEnabled(true);
        m_buildOutput->appendPlainText(QStringLiteral("$ %1").arg(command));
    });
    connect(m_buildRunner, &devflow::BuildRunner::outputReceived, this,
            &DevFlowPanel::onBuildOutput);
    connect(m_buildRunner, &devflow::BuildRunner::finished, this, &DevFlowPanel::onBuildFinished);
    connect(m_buildRunner, &devflow::BuildRunner::failed, this, [this](const QString& message) {
        m_buildOutput->appendPlainText(QStringLiteral("[devflow] %1").arg(message));
        m_buildStatus->setText(message);
        m_buildRunButton->setEnabled(true);
        m_buildCancelButton->setEnabled(false);
        addActivity(QStringLiteral("Build could not start: %1").arg(message));
    });

    connect(m_workflowRunner, &devflow::WorkflowRunner::stepStarted, this,
            &DevFlowPanel::onWorkflowStepStarted);
    connect(m_workflowRunner, &devflow::WorkflowRunner::stepFinished, this,
            &DevFlowPanel::onWorkflowStepFinished);
    connect(m_workflowRunner, &devflow::WorkflowRunner::logLine, this,
            [this](const QString& line) {
                m_workflowLog->appendPlainText(line);
            });
    connect(m_workflowRunner, &devflow::WorkflowRunner::finished, this,
            &DevFlowPanel::onWorkflowFinished);

    refresh();
}

DevFlowPanel::~DevFlowPanel()
{
    if (m_healthRunning)
        m_healthToken.cancel();
    if (m_buildRunner->isRunning())
        m_buildRunner->cancel();
}

QString DevFlowPanel::selectTab(const QString& id)
{
    const QString normalized = id.trimmed().toLower();
    for (int index = 0; index < m_tabs->count(); ++index) {
        const QString title = m_tabs->tabText(index);
        if (title.toLower() == normalized ||
            (normalized == QLatin1String("build") && title.startsWith(QLatin1String("Build"))) ||
            (normalized == QLatin1String("errors") && title.startsWith(QLatin1String("Errors")))) {
            m_tabs->setCurrentIndex(index);
            return title;
        }
    }
    return {};
}

QString DevFlowPanel::projectPath() const
{
    return m_hooks.projectPath ? m_hooks.projectPath() : QString();
}

QString DevFlowPanel::projectName() const
{
    const QString path = projectPath();
    return path.isEmpty() ? QStringLiteral("No project open") : QFileInfo(path).fileName();
}

bool DevFlowPanel::isBuildRunning() const
{
    return m_buildRunner->isRunning();
}

bool DevFlowPanel::isWorkflowRunning() const
{
    return m_workflowRunner->isRunning();
}

bool DevFlowPanel::isHealthRunning() const
{
    return m_healthRunning;
}

void DevFlowPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("devflowHeader"));
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(12, 8, 12, 8);
    headerLayout->setSpacing(1);
    auto* title = new QLabel(QStringLiteral("DEVFLOW"), header);
    title->setObjectName(QStringLiteral("devflowTitle"));
    headerLayout->addWidget(title);
    auto* subtitle = new QLabel(
        QStringLiteral("Your development workflow command center"), header);
    subtitle->setObjectName(QStringLiteral("hintLabel"));
    headerLayout->addWidget(subtitle);
    m_projectLabel = new QLabel(header);
    m_projectLabel->setObjectName(QStringLiteral("sectionLabel"));
    headerLayout->addWidget(m_projectLabel);
    layout->addWidget(header);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    // Seven compact workspaces must stay readable in the 640 px sidebar: share
    // the width evenly and elide labels instead of hiding tabs behind arrows.
    m_tabs->setElideMode(Qt::ElideRight);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setUsesScrollButtons(true);
    layout->addWidget(m_tabs, 1);

    m_tabs->addTab(buildDashboardTab(), QStringLiteral("Dashboard"));
    m_tabs->addTab(buildBuildTab(), QStringLiteral("Builds"));
    m_tabs->addTab(buildErrorsTab(), QStringLiteral("Errors"));
    m_tabs->addTab(buildTasksTab(), QStringLiteral("Tasks"));
    m_tabs->addTab(buildSnapshotsTab(), QStringLiteral("Snapshots"));
    m_tabs->addTab(buildHealthTab(), QStringLiteral("Health"));
    m_tabs->addTab(buildWorkflowsTab(), QStringLiteral("Workflows"));

    // Seven tabs are unreadable in a narrow dock and Qt elides them to two
    // letters. A selector always shows the full workspace name; the tabs stay
    // available for programmatic navigation and keyboard shortcuts.
    auto* selector = new QComboBox(this);
    selector->setObjectName(QStringLiteral("devflowTabSelector"));
    selector->setToolTip(QStringLiteral("DevFlow workspace"));
    for (int index = 0; index < m_tabs->count(); ++index)
        selector->addItem(m_tabs->tabText(index));
    layout->insertWidget(1, selector);
    connect(selector, &QComboBox::currentIndexChanged, m_tabs, &QTabWidget::setCurrentIndex);
    connect(m_tabs, &QTabWidget::currentChanged, selector, &QComboBox::setCurrentIndex);
    m_tabs->tabBar()->hide();
}

QWidget* DevFlowPanel::buildDashboardTab()
{
    auto* page = new QWidget(m_tabs);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    auto* stats = new QWidget(page);
    auto* statsLayout = new QGridLayout(stats);
    statsLayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->setSpacing(6);
    const auto addStat = [&](QLabel*& label, const QString& caption, int column) {
        auto* box = new QGroupBox(stats);
        auto* boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(10, 8, 10, 8);
        boxLayout->setSpacing(1);
        label = new QLabel(QStringLiteral("0"), box);
        label->setObjectName(QStringLiteral("devflowStat"));
        boxLayout->addWidget(label);
        auto* description = new QLabel(caption, box);
        description->setObjectName(QStringLiteral("hintLabel"));
        boxLayout->addWidget(description);
        statsLayout->addWidget(box, 0, column);
    };
    addStat(m_profileCount, QStringLiteral("Build profiles"), 0);
    addStat(m_taskCount, QStringLiteral("Open tasks"), 1);
    addStat(m_snapshotCount, QStringLiteral("Snapshots"), 2);
    addStat(m_errorCount, QStringLiteral("Tracked errors"), 3);
    layout->addWidget(stats);

    auto* actions = new QGroupBox(QStringLiteral("Quick actions"), page);
    auto* actionsLayout = new QGridLayout(actions);
    actionsLayout->setContentsMargins(10, 8, 10, 8);
    actionsLayout->setSpacing(6);
    auto* newTask = new QPushButton(QStringLiteral("New task"), actions);
    auto* snapshot = new QPushButton(QStringLiteral("Save workspace snapshot"), actions);
    auto* health = new QPushButton(QStringLiteral("Run health check"), actions);
    auto* workflows = new QPushButton(QStringLiteral("Run workflow"), actions);
    auto* project = new QPushButton(QStringLiteral("Open project folder"), actions);
    actionsLayout->addWidget(newTask, 0, 0);
    actionsLayout->addWidget(snapshot, 0, 1);
    actionsLayout->addWidget(health, 1, 0);
    actionsLayout->addWidget(workflows, 1, 1);
    actionsLayout->addWidget(project, 2, 0);
    layout->addWidget(actions);

    connect(newTask, &QPushButton::clicked, this, &DevFlowPanel::createTaskInteractive);
    connect(snapshot, &QPushButton::clicked, this, &DevFlowPanel::saveSnapshotInteractive);
    connect(health, &QPushButton::clicked, this, [this]() {
        m_tabs->setCurrentIndex(5);
        runHealthCheck();
    });
    connect(workflows, &QPushButton::clicked, this, [this]() {
        m_tabs->setCurrentIndex(6);
        runWorkflow();
    });
    connect(project, &QPushButton::clicked, this, [this]() {
        const QString path = projectPath();
        if (path.isEmpty()) {
            if (m_hooks.openProject)
                m_hooks.openProject({});
            return;
        }
        if (m_hooks.openProject)
            m_hooks.openProject(path);
    });

    auto* recent = new QGroupBox(QStringLiteral("Recent activity"), page);
    auto* recentLayout = new QVBoxLayout(recent);
    recentLayout->setContentsMargins(8, 6, 8, 6);
    m_activity = new QListWidget(recent);
    m_activity->setObjectName(QStringLiteral("devflowActivity"));
    recentLayout->addWidget(m_activity);
    layout->addWidget(recent, 1);

    auto* note = new QLabel(
        QStringLiteral("DevFlow only reports what it can verify. No health score is invented, "
                       "and no command runs without showing it first."),
        page);
    note->setObjectName(QStringLiteral("hintLabel"));
    note->setWordWrap(true);
    layout->addWidget(note);
    return page;
}

QWidget* DevFlowPanel::buildBuildTab()
{
    auto* page = new QWidget(m_tabs);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* splitter = new QSplitter(Qt::Horizontal, page);

    auto* listPane = new QWidget(splitter);
    auto* listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(4);
    m_profileList = new QListWidget(listPane);
    m_profileList->setObjectName(QStringLiteral("devflowList"));
    listLayout->addWidget(m_profileList, 1);
    auto* listButtons = new QHBoxLayout();
    auto* add = new QPushButton(QStringLiteral("New"), listPane);
    auto* duplicate = new QPushButton(QStringLiteral("Duplicate"), listPane);
    listButtons->addWidget(add);
    listButtons->addWidget(duplicate);
    listLayout->addLayout(listButtons);
    splitter->addWidget(listPane);

    auto* formPane = new QWidget(splitter);
    auto* formLayout = new QVBoxLayout(formPane);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(6);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(5);
    m_profileName = new QLineEdit(formPane);
    m_profileName->setPlaceholderText(QStringLiteral("Profile name"));
    form->addRow(QStringLiteral("Name"), m_profileName);
    m_profileSystem = new QComboBox(formPane);
    for (const auto system : devflow::supportedBuildSystems())
        m_profileSystem->addItem(devflow::buildSystemName(system), static_cast<int>(system));
    form->addRow(QStringLiteral("Build system"), m_profileSystem);
    m_profileBuildDir = new QLineEdit(formPane);
    m_profileBuildDir->setPlaceholderText(QStringLiteral("build"));
    form->addRow(QStringLiteral("Build folder"), m_profileBuildDir);
    m_profileConfiguration = new QLineEdit(formPane);
    m_profileConfiguration->setPlaceholderText(QStringLiteral("Debug or Release (optional)"));
    form->addRow(QStringLiteral("Configuration"), m_profileConfiguration);
    m_profileTarget = new QLineEdit(formPane);
    m_profileTarget->setPlaceholderText(QStringLiteral("Target (optional)"));
    form->addRow(QStringLiteral("Target"), m_profileTarget);
    m_profileCompiler = new QLineEdit(formPane);
    m_profileCompiler->setPlaceholderText(
        QStringLiteral("C++ compiler (Makefile/Meson only; CMake uses its cache)"));
    form->addRow(QStringLiteral("Compiler"), m_profileCompiler);
    m_profileArguments = new QLineEdit(formPane);
    m_profileArguments->setPlaceholderText(
        QStringLiteral("Extra arguments, quoted, no shell expansion"));
    form->addRow(QStringLiteral("Arguments"), m_profileArguments);
    m_profileEnvironment = new QPlainTextEdit(formPane);
    m_profileEnvironment->setPlaceholderText(
        QStringLiteral("Environment, one KEY=VALUE per line (secrets are masked in logs)"));
    m_profileEnvironment->setMaximumHeight(70);
    form->addRow(QStringLiteral("Environment"), m_profileEnvironment);
    formLayout->addLayout(form);

    auto* saveButtons = new QHBoxLayout();
    auto* save = new QPushButton(QStringLiteral("Save"), formPane);
    auto* remove = new QPushButton(QStringLiteral("Delete"), formPane);
    saveButtons->addWidget(save);
    saveButtons->addWidget(remove);
    saveButtons->addStretch(1);
    formLayout->addLayout(saveButtons);

    auto* transferButtons = new QHBoxLayout();
    auto* import = new QPushButton(QStringLiteral("Import"), formPane);
    auto* exportButton = new QPushButton(QStringLiteral("Export"), formPane);
    import->setToolTip(QStringLiteral("Import a build profile from JSON"));
    exportButton->setToolTip(QStringLiteral("Export this build profile as JSON"));
    transferButtons->addWidget(import);
    transferButtons->addWidget(exportButton);
    transferButtons->addStretch(1);
    formLayout->addLayout(transferButtons);

    auto* runRow = new QHBoxLayout();
    m_buildRunButton = new QPushButton(QStringLiteral("Run build"), formPane);
    m_buildRunButton->setObjectName(QStringLiteral("editToggle"));
    m_buildCancelButton = new QPushButton(QStringLiteral("Cancel"), formPane);
    m_buildCancelButton->setEnabled(false);
    m_buildStatus = new QLabel(QStringLiteral("No build running"), formPane);
    m_buildStatus->setObjectName(QStringLiteral("hintLabel"));
    m_buildStatus->setWordWrap(true);
    runRow->addWidget(m_buildRunButton);
    runRow->addWidget(m_buildCancelButton);
    runRow->addStretch(1);
    formLayout->addLayout(runRow);
    formLayout->addWidget(m_buildStatus);

    m_buildOutput = new QPlainTextEdit(formPane);
    m_buildOutput->setReadOnly(true);
    m_buildOutput->setMaximumBlockCount(4000);
    m_buildOutput->setPlaceholderText(
        QStringLiteral("Real build output appears here; the exact command is shown first."));
    formLayout->addWidget(m_buildOutput, 1);

    splitter->addWidget(formPane);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({190, 560});
    layout->addWidget(splitter, 1);

    auto* note = new QLabel(
        QStringLiteral("Builds run directly through the installed tool, never through a shell. "
                       "Arguments are passed verbatim. CMake build folders must already be "
                       "configured."),
        page);
    note->setObjectName(QStringLiteral("hintLabel"));
    note->setWordWrap(true);
    layout->addWidget(note);

    connect(add, &QPushButton::clicked, this, &DevFlowPanel::newProfile);
    connect(duplicate, &QPushButton::clicked, this, &DevFlowPanel::duplicateProfile);
    connect(save, &QPushButton::clicked, this, &DevFlowPanel::saveProfileFromForm);
    connect(remove, &QPushButton::clicked, this, &DevFlowPanel::deleteProfile);
    connect(import, &QPushButton::clicked, this, &DevFlowPanel::importProfile);
    connect(exportButton, &QPushButton::clicked, this, &DevFlowPanel::exportProfile);
    connect(m_buildRunButton, &QPushButton::clicked, this, &DevFlowPanel::runBuild);
    connect(m_buildCancelButton, &QPushButton::clicked, this, &DevFlowPanel::cancelBuild);
    connect(m_profileList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* item, QListWidgetItem*) {
                if (item == nullptr)
                    return;
                loadProfile(item->data(Qt::UserRole).toString());
            });
    return page;
}

void DevFlowPanel::refresh()
{
    if (projectPath().isEmpty()) {
        m_projectLabel->setText(QStringLiteral("No project open"));
        m_projectLabel->setToolTip(
            QStringLiteral("Open a project folder to enable project checks"));
    } else {
        m_projectLabel->setText(QStringLiteral("Project: %1").arg(projectName()));
        m_projectLabel->setToolTip(projectPath());
    }
    refreshDashboard();
    refreshProfileList();
    refreshTasks();
    refreshSnapshots();
    refreshWorkflowList();
    refreshErrorTree();
}

void DevFlowPanel::refreshDashboard()
{
    const int tasks = static_cast<int>(m_store.tasks(projectPath()).size());
    int open = 0;
    for (const devflow::ProjectTask& task : m_store.tasks(projectPath())) {
        if (task.status != devflow::TaskStatus::Done)
            ++open;
    }
    Q_UNUSED(tasks)
    m_profileCount->setText(QString::number(m_store.profiles().size()));
    m_taskCount->setText(QString::number(open));
    m_snapshotCount->setText(QString::number(m_store.snapshots().size()));
    m_errorCount->setText(QString::number(m_errorGroups.size()));
    emit taskCountChanged(open);
    loadActivity();
}

void DevFlowPanel::loadActivity()
{
    if (m_activity == nullptr)
        return;
    m_activity->clear();
    for (const auto& item : m_database.devflowItems(QStringLiteral("activity"))) {
        const QJsonObject object = QJsonDocument::fromJson(item.second.toUtf8()).object();
        const QString text = object.value(QStringLiteral("text")).toString();
        const QString when = object.value(QStringLiteral("when")).toString();
        if (text.isEmpty())
            continue;
        const QDateTime time = QDateTime::fromString(when, Qt::ISODate);
        m_activity->addItem(QStringLiteral("%1  %2")
                                .arg(time.isValid() ? time.toString(QStringLiteral("MM-dd HH:mm"))
                                                    : QStringLiteral("--"),
                                     text));
    }
    if (m_activity->count() == 0) {
        auto* item = new QListWidgetItem(QStringLiteral("No DevFlow activity yet."), m_activity);
        item->setFlags(Qt::NoItemFlags);
    }
}

void DevFlowPanel::addActivity(const QString& text)
{
    QJsonObject object;
    object.insert(QStringLiteral("text"), text);
    object.insert(QStringLiteral("when"), QDateTime::currentDateTime().toString(Qt::ISODate));
    m_database.saveDevflowItem(
        QStringLiteral("activity"), devflow::makeId(QStringLiteral("activity")),
        QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));

    const auto items = m_database.devflowItems(QStringLiteral("activity"));
    constexpr int kKeep = 40;
    for (int index = kKeep; index < static_cast<int>(items.size()); ++index)
        m_database.removeDevflowItem(QStringLiteral("activity"), items.at(index).first);
    if (m_activity != nullptr)
        loadActivity();
}

void DevFlowPanel::refreshProfileList()
{
    const QString previous = m_currentProfileId;
    m_profileList->clear();
    for (const devflow::BuildProfile& profile : m_store.profiles()) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1\n%2").arg(profile.name, devflow::buildSystemName(profile.buildSystem)),
            m_profileList);
        item->setData(Qt::UserRole, profile.id);
        item->setToolTip(profile.projectPath);
        if (profile.id == previous)
            m_profileList->setCurrentItem(item);
    }
    if (m_profileList->count() == 0) {
        auto* item = new QListWidgetItem(QStringLiteral("No profiles saved yet."), m_profileList);
        item->setFlags(Qt::NoItemFlags);
    }
}

void DevFlowPanel::newProfile()
{
    m_currentProfileId.clear();
    m_profileName->clear();
    m_profileName->setPlaceholderText(QStringLiteral("%1 build").arg(projectName()));
    m_profileBuildDir->setText(QStringLiteral("build"));
    m_profileConfiguration->setText(QStringLiteral("Debug"));
    m_profileTarget->clear();
    m_profileCompiler->clear();
    m_profileArguments->clear();
    m_profileEnvironment->clear();

    // Pick the build system from real marker files in the open project.
    const QString path = projectPath();
    const QDir directory(path);
    if (directory.exists(QStringLiteral("CMakeLists.txt")))
        m_profileSystem->setCurrentIndex(
            std::max(0, m_profileSystem->findData(static_cast<int>(devflow::BuildSystem::CMake))));
    else if (directory.exists(QStringLiteral("meson.build")))
        m_profileSystem->setCurrentIndex(
            std::max(0, m_profileSystem->findData(static_cast<int>(devflow::BuildSystem::Meson))));
    else if (directory.exists(QStringLiteral("Makefile")) ||
             directory.exists(QStringLiteral("makefile")))
        m_profileSystem->setCurrentIndex(std::max(
            0, m_profileSystem->findData(static_cast<int>(devflow::BuildSystem::Makefile))));
}

devflow::BuildProfile DevFlowPanel::profileFromForm() const
{
    devflow::BuildProfile profile;
    profile.id = m_currentProfileId.isEmpty() ? devflow::makeId(QStringLiteral("profile"))
                                              : m_currentProfileId;
    profile.name = m_profileName->text().trimmed();
    profile.projectPath = projectPath();
    profile.buildSystem =
        static_cast<devflow::BuildSystem>(m_profileSystem->currentData().toInt());
    profile.buildDirectory = m_profileBuildDir->text().trimmed();
    profile.configuration = m_profileConfiguration->text().trimmed();
    profile.target = m_profileTarget->text().trimmed();
    profile.compiler = m_profileCompiler->text().trimmed();
    profile.arguments = splitArguments(m_profileArguments->text());
    profile.environment = parseEnvironment(m_profileEnvironment->toPlainText(), nullptr);
    profile.createdAt = QDateTime::currentDateTime();
    return profile;
}

void DevFlowPanel::showProfileOnForm(const devflow::BuildProfile& profile)
{
    m_currentProfileId = profile.id;
    m_profileName->setText(profile.name);
    const int index = m_profileSystem->findData(static_cast<int>(profile.buildSystem));
    m_profileSystem->setCurrentIndex(std::max(0, index));
    m_profileBuildDir->setText(profile.buildDirectory);
    m_profileConfiguration->setText(profile.configuration);
    m_profileTarget->setText(profile.target);
    m_profileCompiler->setText(profile.compiler);
    m_profileArguments->setText(profile.arguments.join(QLatin1Char(' ')));
    m_profileEnvironment->setPlainText(environmentText(profile.environment));
}

void DevFlowPanel::loadProfile(const QString& id)
{
    for (const devflow::BuildProfile& profile : m_store.profiles()) {
        if (profile.id == id) {
            showProfileOnForm(profile);
            return;
        }
    }
}

void DevFlowPanel::saveProfileFromForm()
{
    QStringList problems;
    parseEnvironment(m_profileEnvironment->toPlainText(), &problems);
    if (!problems.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Environment lines"),
                             problems.join(QLatin1Char('\n')));
    }
    devflow::BuildProfile profile = profileFromForm();
    QString error;
    if (!m_store.saveProfile(profile, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot save profile"), error);
        return;
    }
    m_currentProfileId = profile.id;
    refreshProfileList();
    refreshDashboard();
    addActivity(QStringLiteral("Saved build profile \"%1\"").arg(profile.name));
    if (m_hooks.statusMessage)
        m_hooks.statusMessage(QStringLiteral("Build profile saved"));
}

void DevFlowPanel::duplicateProfile()
{
    if (m_currentProfileId.isEmpty())
        return;
    devflow::BuildProfile profile = profileFromForm();
    profile.id = devflow::makeId(QStringLiteral("profile"));
    profile.name += QStringLiteral(" copy");
    QString error;
    if (m_store.saveProfile(profile, &error)) {
        m_currentProfileId = profile.id;
        showProfileOnForm(profile);
        refreshProfileList();
        addActivity(QStringLiteral("Duplicated build profile \"%1\"").arg(profile.name));
    } else {
        QMessageBox::warning(this, QStringLiteral("Cannot duplicate profile"), error);
    }
}

void DevFlowPanel::deleteProfile()
{
    if (m_currentProfileId.isEmpty())
        return;
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Delete profile"),
        QStringLiteral("Delete this build profile? The project itself is not touched."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    m_store.removeProfile(m_currentProfileId);
    m_currentProfileId.clear();
    refreshProfileList();
    refreshDashboard();
}

void DevFlowPanel::importProfile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import build profile"), QString(), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("Cannot read profile"), file.errorString());
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        QMessageBox::warning(this, QStringLiteral("Invalid profile"),
                             QStringLiteral("The file is not a JSON object."));
        return;
    }
    auto profile = devflow::BuildProfile::fromJson(document.object());
    if (!profile) {
        QMessageBox::warning(this, QStringLiteral("Invalid profile"),
                             profile.error().detail.isEmpty() ? profile.error().message
                                                              : profile.error().detail);
        return;
    }
    if (profile.value().projectPath != projectPath()) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Different project"),
            QStringLiteral("This profile was saved for another project. Import it and point it "
                           "at the current project?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        profile.value().projectPath = projectPath();
    }
    profile.value().id = devflow::makeId(QStringLiteral("profile"));
    QString error;
    if (!m_store.saveProfile(profile.value(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot import profile"), error);
        return;
    }
    refreshProfileList();
    showProfileOnForm(profile.value());
    addActivity(QStringLiteral("Imported build profile \"%1\"").arg(profile.value().name));
}

void DevFlowPanel::exportProfile()
{
    devflow::BuildProfile profile = profileFromForm();
    if (profile.name.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("No profile"),
                                 QStringLiteral("Fill in a profile name first."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export build profile"),
        QDir::home().filePath(profile.name + QStringLiteral(".json")),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("Cannot write profile"), file.errorString());
        return;
    }
    file.write(QJsonDocument(profile.toJson()).toJson(QJsonDocument::Indented));
    addActivity(QStringLiteral("Exported build profile \"%1\"").arg(profile.name));
}

void DevFlowPanel::runBuild()
{
    if (m_buildRunner->isRunning())
        return;
    const devflow::BuildProfile profile = profileFromForm();
    QString error;
    const devflow::BuildCommand command = devflow::BuildRunner::commandFor(profile, &error);
    if (!command.isValid()) {
        QMessageBox::warning(this, QStringLiteral("Cannot run this profile"),
                             error.isEmpty() ? QStringLiteral("The profile is incomplete")
                                             : error);
        return;
    }

    const auto answer = QMessageBox::question(
        this, QStringLiteral("Run build"),
        QStringLiteral("Run this command in:\n%1\n\n%2\n\nBuild output is shown and parsed "
                       "for errors. Nothing else is executed.")
            .arg(command.workingDirectory, command.display),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    m_buildOutput->clear();
    m_buildStatus->setText(QStringLiteral("Starting\u2026"));
    addActivity(QStringLiteral("Ran build profile \"%1\"").arg(profile.name));
    m_buildRunner->start(profile);
}

void DevFlowPanel::cancelBuild()
{
    m_buildRunner->cancel();
    m_buildCancelButton->setEnabled(false);
    m_buildStatus->setText(QStringLiteral("Cancelling\u2026"));
}

void DevFlowPanel::onBuildOutput(const QString& text)
{
    m_buildOutput->moveCursor(QTextCursor::End);
    m_buildOutput->insertPlainText(text);
    m_buildOutput->moveCursor(QTextCursor::End);
}

void DevFlowPanel::onBuildFinished(int exitCode, QProcess::ExitStatus status,
                                   const QString& command)
{
    Q_UNUSED(command)
    m_buildRunButton->setEnabled(true);
    m_buildCancelButton->setEnabled(false);
    const bool success = status == QProcess::NormalExit && exitCode == 0;
    m_buildStatus->setText(success
                               ? QStringLiteral("Build finished successfully")
                               : QStringLiteral("Build failed with exit code %1").arg(exitCode));
    addActivity(success ? QStringLiteral("Build succeeded")
                        : QStringLiteral("Build failed (exit code %1)").arg(exitCode));

    analyzeText(m_buildOutput->toPlainText(), QStringLiteral("build output"));
    if (m_errorGroups.size() > 0)
        m_tabs->setCurrentIndex(2);
}

QWidget* DevFlowPanel::buildErrorsTab()
{
    auto* page = new QWidget(m_tabs);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* toolbar = new QHBoxLayout();
    m_errorFilter = new QComboBox(page);
    m_errorFilter->addItems({QStringLiteral("All diagnostics"), QStringLiteral("Errors only"),
                             QStringLiteral("Warnings only")});
    auto* analyze = new QPushButton(QStringLiteral("Analyze build output"), page);
    auto* collect = new QPushButton(QStringLiteral("Collect last build"), page);
    auto* resolve = new QPushButton(QStringLiteral("Toggle resolved"), page);
    auto* openSource = new QPushButton(QStringLiteral("Open source"), page);
    toolbar->addWidget(m_errorFilter);
    toolbar->addWidget(analyze);
    toolbar->addWidget(collect);
    toolbar->addWidget(resolve);
    toolbar->addWidget(openSource);
    toolbar->addStretch(1);
    layout->addLayout(toolbar);

    m_errorSummary = new QLabel(
        QStringLiteral("Run a build or paste compiler output to group diagnostics."), page);
    m_errorSummary->setObjectName(QStringLiteral("hintLabel"));
    m_errorSummary->setWordWrap(true);
    layout->addWidget(m_errorSummary);

    auto* splitter = new QSplitter(Qt::Vertical, page);
    m_errorTree = new QTreeWidget(splitter);
    m_errorTree->setColumnCount(5);
    m_errorTree->setRootIsDecorated(false);
    m_errorTree->setUniformRowHeights(true);
    m_errorTree->setHeaderLabels({QStringLiteral("Severity"), QStringLiteral("Count"),
                                  QStringLiteral("Location"), QStringLiteral("Message"),
                                  QStringLiteral("State")});
    m_errorTree->header()->setStretchLastSection(false);
    m_errorTree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    splitter->addWidget(m_errorTree);

    m_errorDetails = new QTextBrowser(splitter);
    m_errorDetails->setOpenExternalLinks(false);
    splitter->addWidget(m_errorDetails);
    splitter->setSizes({260, 180});
    layout->addWidget(splitter, 1);

    auto* note = new QLabel(
        QStringLiteral("Explanations are generated locally from the diagnostic text. Suggestions "
                       "are possibilities to review; DevFlow never edits code for you."),
        page);
    note->setObjectName(QStringLiteral("hintLabel"));
    note->setWordWrap(true);
    layout->addWidget(note);

    connect(m_errorFilter, &QComboBox::currentIndexChanged, this, [this](int) {
        refreshErrorTree();
    });
    connect(m_errorTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem*, QTreeWidgetItem*) { showSelectedError(); });
    connect(m_errorTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem*, int) { openSelectedErrorSource(); });
    connect(openSource, &QPushButton::clicked, this, &DevFlowPanel::openSelectedErrorSource);
    connect(resolve, &QPushButton::clicked, this, &DevFlowPanel::toggleResolvedError);
    connect(collect, &QPushButton::clicked, this, &DevFlowPanel::collectErrorsFromLastBuild);
    connect(analyze, &QPushButton::clicked, this, [this]() {
        const QString text = QApplication::clipboard()->text();
        if (text.trimmed().isEmpty()) {
            QMessageBox::information(
                this, QStringLiteral("Nothing to analyze"),
                QStringLiteral("Copy compiler output to the clipboard first, or run a build."));
            return;
        }
        analyzeText(text, QStringLiteral("clipboard"));
    });
    return page;
}

void DevFlowPanel::analyzeText(const QString& text, const QString& source)
{
    m_lastErrorReport = devflow::parseBuildOutput(text);
    m_errorGroups = devflow::groupDiagnostics(m_lastErrorReport.diagnostics);
    refreshErrorTree();
    m_errorSummary->setText(QStringLiteral("%1 error(s), %2 warning(s) from %3%4")
                                .arg(m_lastErrorReport.errorCount())
                                .arg(m_lastErrorReport.warningCount())
                                .arg(source,
                                     m_lastErrorReport.truncated ? QStringLiteral(" (truncated)")
                                                                 : QString()));
    addActivity(QStringLiteral("Analyzed %1: %2 error(s), %3 warning(s)")
                    .arg(source)
                    .arg(m_lastErrorReport.errorCount())
                    .arg(m_lastErrorReport.warningCount()));
    refreshDashboard();
}

void DevFlowPanel::collectErrorsFromLastBuild()
{
    const QString text = m_buildOutput->toPlainText();
    if (text.trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("No build output"),
                                 QStringLiteral("Run a build first; its output is kept here."));
        return;
    }
    analyzeText(text, QStringLiteral("last build"));
}

void DevFlowPanel::refreshErrorTree()
{
    if (m_errorTree == nullptr)
        return;
    const QString previous = m_errorTree->currentItem() != nullptr
        ? m_errorTree->currentItem()->data(0, Qt::UserRole).toString()
        : QString();
    m_errorTree->clear();
    const int filter = m_errorFilter != nullptr ? m_errorFilter->currentIndex() : 0;
    const QString project = projectPath();
    for (const devflow::DiagnosticGroup& group : m_errorGroups) {
        const bool isError = group.severity == devflow::DiagnosticSeverity::Error ||
                             group.severity == devflow::DiagnosticSeverity::Fatal;
        if (filter == 1 && !isError)
            continue;
        if (filter == 2 && group.severity != devflow::DiagnosticSeverity::Warning)
            continue;
        auto* item = new QTreeWidgetItem(m_errorTree);
        item->setText(0, devflow::severityName(group.severity));
        item->setText(1, QString::number(group.count));
        item->setText(2, group.file.isEmpty() ? QStringLiteral("\u2014")
                                              : QStringLiteral("%1:%2").arg(group.file).arg(group.line));
        item->setText(3, group.summary());
        const bool resolved = m_store.isErrorResolved(project, group.fingerprint);
        item->setText(4, resolved ? QStringLiteral("Resolved") : QStringLiteral("Open"));
        item->setData(0, Qt::UserRole, group.fingerprint);
        item->setForeground(0, QBrush(ThemeManager::instance().current().color(
                               severityColorKey(group.severity))));
        item->setToolTip(3, group.message);
        if (group.fingerprint == previous)
            m_errorTree->setCurrentItem(item);
    }
    if (m_errorTree->topLevelItemCount() == 0) {
        auto* item = new QTreeWidgetItem(m_errorTree);
        item->setText(0, QStringLiteral("\u2014"));
        item->setText(3, m_errorGroups.isEmpty()
                             ? QStringLiteral("No diagnostics analyzed yet.")
                             : QStringLiteral("No diagnostics match this filter."));
        item->setDisabled(true);
    }
    if (m_errorSummary != nullptr && m_errorGroups.isEmpty()) {
        m_errorSummary->setText(
            QStringLiteral("Run a build or analyze compiler output to group diagnostics."));
    }
}

void DevFlowPanel::showSelectedError()
{
    const QTreeWidgetItem* item = m_errorTree->currentItem();
    if (item == nullptr)
        return;
    const QString fingerprint = item->data(0, Qt::UserRole).toString();
    if (fingerprint.isEmpty()) {
        m_errorDetails->clear();
        return;
    }
    for (const devflow::DiagnosticGroup& group : m_errorGroups) {
        if (group.fingerprint != fingerprint)
            continue;
        const bool resolved = m_store.isErrorResolved(projectPath(), group.fingerprint);
        QString html;
        html += QStringLiteral("<p><b>%1</b> \u00B7 %2 occurrence(s)</p>")
                    .arg(group.message.toHtmlEscaped())
                    .arg(group.count);
        if (!group.file.isEmpty()) {
            html += QStringLiteral("<p>Location: <code>%1:%2</code></p>")
                        .arg(group.file.toHtmlEscaped())
                        .arg(group.line);
        }
        if (!group.code.isEmpty())
            html += QStringLiteral("<p>Code: %1</p>").arg(group.code.toHtmlEscaped());
        html += QStringLiteral("<p><b>State:</b> %1</p>")
                    .arg(resolved ? QStringLiteral("resolved") : QStringLiteral("open"));
        if (!group.explanation.isEmpty())
            html += QStringLiteral("<p><b>What this means:</b> %1</p>")
                        .arg(group.explanation.toHtmlEscaped());
        if (!group.suggestion.isEmpty())
            html += QStringLiteral("<p><b>Possible next step:</b> %1</p>")
                        .arg(group.suggestion.toHtmlEscaped());
        if (group.explanation.isEmpty() && group.suggestion.isEmpty()) {
            html += QStringLiteral(
                "<p>No local explanation rule matched this diagnostic. Read the raw compiler "
                "message above before changing code.</p>");
        }
        m_errorDetails->setHtml(html);
        return;
    }
}

void DevFlowPanel::toggleResolvedError()
{
    const QTreeWidgetItem* item = m_errorTree->currentItem();
    if (item == nullptr)
        return;
    const QString fingerprint = item->data(0, Qt::UserRole).toString();
    if (fingerprint.isEmpty())
        return;
    const bool resolved = m_store.isErrorResolved(projectPath(), fingerprint);
    m_store.setErrorResolved(projectPath(), fingerprint, !resolved);
    refreshErrorTree();
}

void DevFlowPanel::openSelectedErrorSource()
{
    const QTreeWidgetItem* item = m_errorTree->currentItem();
    if (item == nullptr)
        return;
    const QString fingerprint = item->data(0, Qt::UserRole).toString();
    for (const devflow::DiagnosticGroup& group : m_errorGroups) {
        if (group.fingerprint != fingerprint || group.file.isEmpty())
            continue;
        if (m_hooks.openFile)
            m_hooks.openFile(group.file);
        if (m_hooks.statusMessage)
            m_hooks.statusMessage(
                QStringLiteral("Opened %1 (reported at line %2)").arg(group.file).arg(group.line));
        return;
    }
}

QWidget* DevFlowPanel::buildTasksTab()
{
    auto* page = new QWidget(m_tabs);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* toolbar = new QHBoxLayout();
    m_taskFilter = new QComboBox(page);
    m_taskFilter->addItems({QStringLiteral("All tasks"), QStringLiteral("To do"),
                            QStringLiteral("In progress"), QStringLiteral("Done")});
    m_taskSearch = new QLineEdit(page);
    m_taskSearch->setPlaceholderText(QStringLiteral("Search tasks\u2026"));
    m_taskSearch->setClearButtonEnabled(true);
    m_taskSearch->setMaximumWidth(220);
    auto* add = new QPushButton(QStringLiteral("New task"), page);
    auto* edit = new QPushButton(QStringLiteral("Edit"), page);
    auto* remove = new QPushButton(QStringLiteral("Delete"), page);
    auto* openFile = new QPushButton(QStringLiteral("Open linked file"), page);
    toolbar->addWidget(m_taskFilter);
    toolbar->addWidget(m_taskSearch);
    toolbar->addWidget(add);
    toolbar->addWidget(edit);
    toolbar->addWidget(remove);
    toolbar->addWidget(openFile);
    toolbar->addStretch(1);
    layout->addLayout(toolbar);

    m_taskTable = new QTableWidget(page);
    m_taskTable->setColumnCount(5);
    m_taskTable->setHorizontalHeaderLabels({QStringLiteral("Status"), QStringLiteral("Priority"),
                                            QStringLiteral("Task"), QStringLiteral("Linked file"),
                                            QStringLiteral("Updated")});
    m_taskTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_taskTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_taskTable->setAlternatingRowColors(true);
    m_taskTable->verticalHeader()->setVisible(false);
    m_taskTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(m_taskTable, 1);

    auto* note = new QLabel(
        QStringLiteral("Tasks are stored in the local SQLite database and can link to a project "
                       "file or a tracked error. Completing one only changes its state."),
        page);
    note->setObjectName(QStringLiteral("hintLabel"));
    note->setWordWrap(true);
    layout->addWidget(note);

    connect(add, &QPushButton::clicked, this, [this]() {
        QMenu menu(this);
        QAction* blank = menu.addAction(QStringLiteral("Blank task\u2026"));
        menu.addSeparator();
        QAction* review = menu.addAction(QStringLiteral("Review build warnings"));
        QAction* document = menu.addAction(QStringLiteral("Document the build steps"));
        QAction* dependency = menu.addAction(QStringLiteral("Resolve an unresolved dependency"));
        QAction* plan = menu.addAction(QStringLiteral("Plan the next milestone"));
        QAction* chosen = menu.exec(QCursor::pos());
        if (chosen == nullptr)
            return;
        if (chosen == blank)
            createTaskInteractive();
        else if (chosen == review)
            newTaskWithTemplate(
                QStringLiteral("Review build warnings"),
                QStringLiteral("Open the Errors tab, review warnings that are not yet resolved "
                               "and decide which ones matter for this build."));
        else if (chosen == document)
            newTaskWithTemplate(
                QStringLiteral("Document the build steps"),
                QStringLiteral("Write down the exact configure and build commands so another "
                               "person can reproduce the build."));
        else if (chosen == dependency)
            newTaskWithTemplate(
                QStringLiteral("Resolve an unresolved dependency"),
                QStringLiteral("Check the dependency graph's unresolved list and either declare "
                               "the dependency in the build file or remove the reference."));
        else if (chosen == plan)
            newTaskWithTemplate(QStringLiteral("Plan the next milestone"),
                                QStringLiteral("List the next concrete changes, then split them "
                                               "into reviewable tasks."));
    });
    connect(edit, &QPushButton::clicked, this, [this]() {
        const auto items = m_taskTable->selectedItems();
        if (!items.isEmpty()) {
            const auto* idItem = m_taskTable->item(items.first()->row(), 0);
            if (idItem != nullptr)
                editTask(idItem->data(Qt::UserRole).toString());
        }
    });
    connect(remove, &QPushButton::clicked, this, &DevFlowPanel::deleteSelectedTask);
    connect(openFile, &QPushButton::clicked, this, &DevFlowPanel::openSelectedTaskFile);
    connect(m_taskTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const auto* idItem = m_taskTable->item(row, 0);
        if (idItem != nullptr)
            editTask(idItem->data(Qt::UserRole).toString());
    });
    connect(m_taskFilter, &QComboBox::currentIndexChanged, this, [this](int) { refreshTasks(); });
    connect(m_taskSearch, &QLineEdit::textChanged, this, [this](const QString&) { refreshTasks(); });
    return page;
}

void DevFlowPanel::refreshTasks()
{
    if (m_taskTable == nullptr)
        return;
    m_tasks = m_store.tasks(projectPath());
    const QString needle = m_taskSearch->text().trimmed();
    const int filter = m_taskFilter->currentIndex();
    m_taskTable->setRowCount(0);
    m_taskTable->clearSpans();
    for (const devflow::ProjectTask& task : m_tasks) {
        if (filter == 1 && task.status != devflow::TaskStatus::ToDo)
            continue;
        if (filter == 2 && task.status != devflow::TaskStatus::InProgress)
            continue;
        if (filter == 3 && task.status != devflow::TaskStatus::Done)
            continue;
        if (!needle.isEmpty() && !task.title.contains(needle, Qt::CaseInsensitive) &&
            !task.description.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        const int row = m_taskTable->rowCount();
        m_taskTable->insertRow(row);
        auto* status = new QTableWidgetItem(devflow::taskStatusName(task.status));
        status->setData(Qt::UserRole, task.id);
        m_taskTable->setItem(row, 0, status);
        m_taskTable->setItem(row, 1, new QTableWidgetItem(devflow::taskPriorityName(task.priority)));
        m_taskTable->setItem(row, 2, new QTableWidgetItem(task.title));
        m_taskTable->setItem(row, 3, new QTableWidgetItem(QFileInfo(task.linkedFile).fileName()));
        m_taskTable->setItem(row, 4,
                             new QTableWidgetItem(task.updatedAt.isValid()
                                                      ? task.updatedAt.toString(
                                                            QStringLiteral("MM-dd HH:mm"))
                                                      : QString()));
        if (task.status == devflow::TaskStatus::Done) {
            m_taskTable->item(row, 2)->setForeground(
                QBrush(ThemeManager::instance().current().color(QStringLiteral("gutter"))));
        }
    }
    if (m_taskTable->rowCount() == 0) {
        m_taskTable->insertRow(0);
        auto* empty = new QTableWidgetItem(
            m_tasks.isEmpty() ? QStringLiteral("No tasks yet. Create one with New task.")
                              : QStringLiteral("No tasks match this filter."));
        empty->setFlags(Qt::NoItemFlags);
        m_taskTable->setItem(0, 0, empty);
        m_taskTable->setSpan(0, 0, 1, 5);
    }
    refreshDashboard();
}

namespace {

class TaskDialog : public QDialog {
public:
    TaskDialog(const devflow::ProjectTask& task,
               const QVector<devflow::DiagnosticGroup>& errors, QWidget* parent)
        : QDialog(parent)
    {
        setWindowTitle(task.id.isEmpty() ? QStringLiteral("New task") : QStringLiteral("Edit task"));
        setModal(true);
        resize(500, 400);
        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout();
        title = new QLineEdit(task.title, this);
        form->addRow(QStringLiteral("Title"), title);
        description = new QPlainTextEdit(task.description, this);
        description->setMaximumHeight(110);
        form->addRow(QStringLiteral("Description"), description);
        priority = new QComboBox(this);
        for (const auto value : {devflow::TaskPriority::Low, devflow::TaskPriority::Normal,
                                 devflow::TaskPriority::High, devflow::TaskPriority::Urgent}) {
            priority->addItem(devflow::taskPriorityName(value), static_cast<int>(value));
        }
        priority->setCurrentIndex(std::max(0, priority->findData(static_cast<int>(task.priority))));
        form->addRow(QStringLiteral("Priority"), priority);
        status = new QComboBox(this);
        for (const auto value : {devflow::TaskStatus::ToDo, devflow::TaskStatus::InProgress,
                                 devflow::TaskStatus::Done}) {
            status->addItem(devflow::taskStatusName(value), static_cast<int>(value));
        }
        status->setCurrentIndex(std::max(0, status->findData(static_cast<int>(task.status))));
        form->addRow(QStringLiteral("Status"), status);
        linkedFile = new QLineEdit(task.linkedFile, this);
        linkedFile->setPlaceholderText(QStringLiteral("Optional file path"));
        form->addRow(QStringLiteral("Linked file"), linkedFile);
        linkedError = new QComboBox(this);
        linkedError->addItem(QStringLiteral("Not linked"), QString());
        for (const devflow::DiagnosticGroup& group : errors)
            linkedError->addItem(group.summary().left(80), group.fingerprint);
        linkedError->setCurrentIndex(std::max(0, linkedError->findData(task.linkedError)));
        form->addRow(QStringLiteral("Linked error"), linkedError);
        layout->addLayout(form);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            if (title->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Task needs a title"),
                                     QStringLiteral("Enter a short title first."));
                return;
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    QLineEdit* title = nullptr;
    QPlainTextEdit* description = nullptr;
    QComboBox* priority = nullptr;
    QComboBox* status = nullptr;
    QLineEdit* linkedFile = nullptr;
    QComboBox* linkedError = nullptr;
};

}  // namespace

QString DevFlowPanel::createTaskInteractive()
{
    devflow::ProjectTask task;
    task.projectPath = projectPath();
    TaskDialog dialog(task, m_errorGroups, this);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    task.id = devflow::makeId(QStringLiteral("task"));
    task.title = dialog.title->text().trimmed();
    task.description = dialog.description->toPlainText().trimmed();
    task.priority = static_cast<devflow::TaskPriority>(dialog.priority->currentData().toInt());
    task.status = static_cast<devflow::TaskStatus>(dialog.status->currentData().toInt());
    task.linkedFile = dialog.linkedFile->text().trimmed();
    task.linkedError = dialog.linkedError->currentData().toString();
    task.createdAt = QDateTime::currentDateTime();
    task.updatedAt = task.createdAt;
    QString error;
    if (!m_store.saveTask(task, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot save task"), error);
        return {};
    }
    refreshTasks();
    addActivity(QStringLiteral("Created task \"%1\"").arg(task.title));
    return task.id;
}

void DevFlowPanel::newTaskWithTemplate(const QString& title, const QString& description)
{
    devflow::ProjectTask task;
    task.projectPath = projectPath();
    task.title = title;
    task.description = description;
    TaskDialog dialog(task, m_errorGroups, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    task.id = devflow::makeId(QStringLiteral("task"));
    task.title = dialog.title->text().trimmed();
    task.description = dialog.description->toPlainText().trimmed();
    task.priority = static_cast<devflow::TaskPriority>(dialog.priority->currentData().toInt());
    task.status = static_cast<devflow::TaskStatus>(dialog.status->currentData().toInt());
    task.linkedFile = dialog.linkedFile->text().trimmed();
    task.linkedError = dialog.linkedError->currentData().toString();
    task.createdAt = QDateTime::currentDateTime();
    task.updatedAt = task.createdAt;
    QString error;
    if (!m_store.saveTask(task, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot save task"), error);
        return;
    }
    refreshTasks();
    addActivity(QStringLiteral("Created task \"%1\"").arg(task.title));
}

void DevFlowPanel::editTask(const QString& id)
{
    if (id.isEmpty())
        return;
    for (const devflow::ProjectTask& existing : m_store.tasks(projectPath())) {
        if (existing.id != id)
            continue;
        devflow::ProjectTask task = existing;
        TaskDialog dialog(task, m_errorGroups, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        task.title = dialog.title->text().trimmed();
        task.description = dialog.description->toPlainText().trimmed();
        task.priority = static_cast<devflow::TaskPriority>(dialog.priority->currentData().toInt());
        task.status = static_cast<devflow::TaskStatus>(dialog.status->currentData().toInt());
        task.linkedFile = dialog.linkedFile->text().trimmed();
        task.linkedError = dialog.linkedError->currentData().toString();
        task.updatedAt = QDateTime::currentDateTime();
        QString error;
        if (!m_store.saveTask(task, &error)) {
            QMessageBox::warning(this, QStringLiteral("Cannot save task"), error);
            return;
        }
        refreshTasks();
        addActivity(QStringLiteral("Updated task \"%1\"").arg(task.title));
        return;
    }
}

void DevFlowPanel::deleteSelectedTask()
{
    const auto items = m_taskTable->selectedItems();
    if (items.isEmpty())
        return;
    const QTableWidgetItem* idItem = m_taskTable->item(items.first()->row(), 0);
    if (idItem == nullptr)
        return;
    const QString id = idItem->data(Qt::UserRole).toString();
    if (id.isEmpty())
        return;
    if (QMessageBox::question(this, QStringLiteral("Delete task"),
                              QStringLiteral("Delete this task? This cannot be undone."),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
        QMessageBox::Yes) {
        return;
    }
    m_store.removeTask(id);
    refreshTasks();
}

void DevFlowPanel::openSelectedTaskFile()
{
    const auto items = m_taskTable->selectedItems();
    if (items.isEmpty())
        return;
    const QTableWidgetItem* idItem = m_taskTable->item(items.first()->row(), 0);
    if (idItem == nullptr)
        return;
    const QString id = idItem->data(Qt::UserRole).toString();
    for (const devflow::ProjectTask& task : m_store.tasks(projectPath())) {
        if (task.id != id || task.linkedFile.isEmpty())
            continue;
        if (m_hooks.openFile)
            m_hooks.openFile(task.linkedFile);
        return;
    }
}

QWidget* DevFlowPanel::buildSnapshotsTab()
{
    auto* page = new QWidget(m_tabs);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* toolbar = new QHBoxLayout();
    auto* save = new QPushButton(QStringLiteral("Save current workspace"), page);
    auto* restore = new QPushButton(QStringLiteral("Restore"), page);
    auto* remove = new QPushButton(QStringLiteral("Delete"), page);
    toolbar->addWidget(save);
    toolbar->addWidget(restore);
    toolbar->addWidget(remove);
    toolbar->addStretch(1);
    layout->addLayout(toolbar);

    m_snapshotList = new QListWidget(page);
    m_snapshotList->setObjectName(QStringLiteral("devflowList"));
    layout->addWidget(m_snapshotList, 1);
    m_snapshotSummary = new QLabel(page);
    m_snapshotSummary->setObjectName(QStringLiteral("hintLabel"));
    m_snapshotSummary->setWordWrap(true);
    layout->addWidget(m_snapshotSummary);

    auto* note = new QLabel(
        QStringLiteral("Snapshots store tab paths, layout and cursor positions, never project "
                       "file contents. Restoring reopens the files that still exist and reports "
                       "the ones that do not."),
        page);
    note->setObjectName(QStringLiteral("hintLabel"));
    note->setWordWrap(true);
    layout->addWidget(note);

    connect(save, &QPushButton::clicked, this, &DevFlowPanel::saveSnapshotInteractive);
    connect(restore, &QPushButton::clicked, this, &DevFlowPanel::restoreSelectedSnapshot);
    connect(remove, &QPushButton::clicked, this, &DevFlowPanel::deleteSelectedSnapshot);
    connect(m_snapshotList, &QListWidget::itemSelectionChanged, this, [this]() {
        const auto items = m_snapshotList->selectedItems();
        m_snapshotSummary->setText(items.isEmpty() ? QString() : items.first()->toolTip());
    });
    return page;
}

void DevFlowPanel::refreshSnapshots()
{
    if (m_snapshotList == nullptr)
        return;
    m_snapshotList->clear();
    for (const devflow::WorkspaceSnapshot& snapshot : m_store.snapshots()) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1\n%2").arg(snapshot.name, snapshot.summary()), m_snapshotList);
        item->setData(Qt::UserRole, snapshot.id);
        item->setToolTip(QStringLiteral("Project: %1\nFiles:\n%2")
                             .arg(snapshot.projectPath.isEmpty() ? QStringLiteral("none")
                                                                 : snapshot.projectPath,
                                  snapshot.openFiles.join(QLatin1Char('\n'))));
    }
    if (m_snapshotList->count() == 0) {
        auto* item = new QListWidgetItem(QStringLiteral("No snapshots saved yet."), m_snapshotList);
        item->setFlags(Qt::NoItemFlags);
    } else {
        m_snapshotList->setCurrentRow(0);
    }
}

void DevFlowPanel::saveSnapshotInteractive()
{
    if (!m_hooks.captureSnapshot) {
        QMessageBox::information(this, QStringLiteral("Snapshots unavailable"),
                                 QStringLiteral("The workspace cannot be captured right now."));
        return;
    }
    devflow::WorkspaceSnapshot snapshot = m_hooks.captureSnapshot();
    if (snapshot.openFiles.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("Nothing to capture"),
            QStringLiteral("Open at least one file before saving a workspace snapshot."));
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Save snapshot"), QStringLiteral("Snapshot name"),
        QLineEdit::Normal, QStringLiteral("%1 workspace").arg(projectName()), &accepted);
    if (!accepted)
        return;
    snapshot.id = devflow::makeId(QStringLiteral("snapshot"));
    snapshot.name = name.trimmed();
    snapshot.createdAt = QDateTime::currentDateTime();
    QString error;
    if (!m_store.saveSnapshot(snapshot, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot save snapshot"), error);
        return;
    }
    refreshSnapshots();
    refreshDashboard();
    addActivity(QStringLiteral("Saved workspace snapshot \"%1\"").arg(snapshot.name));
}

void DevFlowPanel::restoreSelectedSnapshot()
{
    const QListWidgetItem* item = m_snapshotList->currentItem();
    if (item == nullptr)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    for (const devflow::WorkspaceSnapshot& snapshot : m_store.snapshots()) {
        if (snapshot.id != id)
            continue;
        if (QMessageBox::question(
                this, QStringLiteral("Restore snapshot"),
                QStringLiteral("Open %1 tab(s) from \"%2\"? Currently open tabs stay open.")
                    .arg(snapshot.openFiles.size())
                    .arg(snapshot.name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        if (m_hooks.restoreSnapshot)
            m_hooks.restoreSnapshot(snapshot);
        addActivity(QStringLiteral("Restored workspace snapshot \"%1\"").arg(snapshot.name));
        return;
    }
}

void DevFlowPanel::deleteSelectedSnapshot()
{
    const QListWidgetItem* item = m_snapshotList->currentItem();
    if (item == nullptr)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty())
        return;
    if (QMessageBox::question(this, QStringLiteral("Delete snapshot"),
                              QStringLiteral("Delete this workspace snapshot?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
        QMessageBox::Yes) {
        return;
    }
    m_store.removeSnapshot(id);
    refreshSnapshots();
    refreshDashboard();
}

QWidget* DevFlowPanel::buildHealthTab()
{
    auto* page = new QWidget(m_tabs);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* toolbar = new QHBoxLayout();
    m_healthRunButton = new QPushButton(QStringLiteral("Run health check"), page);
    auto* note = new QLabel(
        QStringLiteral("Checks structure, build files, dependencies, missing sources, large "
                       "files and portability signals."),
        page);
    note->setObjectName(QStringLiteral("hintLabel"));
    note->setWordWrap(true);
    toolbar->addWidget(m_healthRunButton);
    toolbar->addWidget(note, 1);
    layout->addLayout(toolbar);

    m_healthSummary = new QLabel(QStringLiteral("No health check has run yet."), page);
    m_healthSummary->setObjectName(QStringLiteral("hintLabel"));
    m_healthSummary->setWordWrap(true);
    layout->addWidget(m_healthSummary);

    auto* splitter = new QSplitter(Qt::Vertical, page);
    m_healthTable = new QTableWidget(splitter);
    m_healthTable->setColumnCount(4);
    m_healthTable->setHorizontalHeaderLabels({QStringLiteral("Severity"),
                                              QStringLiteral("Confidence"),
                                              QStringLiteral("Finding"),
                                              QStringLiteral("Evidence")});
    m_healthTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_healthTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_healthTable->setAlternatingRowColors(true);
    m_healthTable->verticalHeader()->setVisible(false);
    m_healthTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    splitter->addWidget(m_healthTable);
    m_healthDetails = new QTextBrowser(splitter);
    splitter->addWidget(m_healthDetails);
    splitter->setSizes({250, 160});
    layout->addWidget(splitter, 1);

    connect(m_healthRunButton, &QPushButton::clicked, this, &DevFlowPanel::runHealthCheck);
    connect(m_healthTable, &QTableWidget::itemSelectionChanged, this,
            &DevFlowPanel::showSelectedHealthFinding);
    return page;
}

bool DevFlowPanel::runHealthCheck()
{
    if (m_healthRunning)
        return false;
    const QString root = projectPath();
    if (root.isEmpty() || !QFileInfo(root).isDir()) {
        m_healthSummary->setText(
            QStringLiteral("Open a project folder first; there is nothing to check."));
        return false;
    }
    m_healthRunning = true;
    m_healthToken = CancelToken();
    m_healthRunButton->setEnabled(false);
    m_healthSummary->setText(QStringLiteral("Checking the project on a worker thread\u2026"));
    if (!m_healthWatcher) {
        m_healthWatcher = std::make_shared<QFutureWatcher<HealthResult>>();
        connect(m_healthWatcher.get(), &QFutureWatcher<HealthResult>::finished, this,
                &DevFlowPanel::onHealthFinished);
    }
    const CancelToken token = m_healthToken;
    m_healthWatcher->setFuture(QtConcurrent::run([root, token]() {
        HealthResult result;
        result.scan = gameforge::scanProject(root, token);
        result.config = gameforge::analyzeBuildConfig(result.scan, token);
        result.graph = gameforge::buildDependencyGraph(result.config, result.scan);
        const gameforge::PortingReport porting = gameforge::analyzePortability(result.scan, token);
        result.health =
            devflow::checkProjectHealth(root, result.scan, result.config, result.graph,
                                        porting, token);
        return result;
    }));
    return true;
}

void DevFlowPanel::onHealthFinished()
{
    m_healthRunning = false;
    m_healthRunButton->setEnabled(true);
    const HealthResult result = m_healthWatcher->result();
    m_lastHealth = result.health;

    m_healthTable->setRowCount(0);
    for (const devflow::HealthFinding& finding : result.health.findings) {
        const int row = m_healthTable->rowCount();
        m_healthTable->insertRow(row);
        auto* severity = new QTableWidgetItem(devflow::healthSeverityName(finding.severity));
        severity->setData(Qt::UserRole, row);
        m_healthTable->setItem(row, 0, severity);
        m_healthTable->setItem(row, 1, new QTableWidgetItem(finding.confidence));
        m_healthTable->setItem(row, 2, new QTableWidgetItem(finding.title));
        m_healthTable->setItem(row, 3, new QTableWidgetItem(finding.evidence));
    }

    QString summary = QStringLiteral("%1 finding(s): %2 critical, %3 warning(s). ")
                          .arg(result.health.findings.size())
                          .arg(result.health.criticalCount())
                          .arg(result.health.warningCount());
    summary += QStringLiteral("Checked %1 file(s), %2 target(s). ")
                   .arg(result.health.checkedFiles)
                   .arg(result.health.checkedTargets);
    if (result.health.scanTruncated)
        summary += QStringLiteral("The scan hit its limit, so results are partial. ");
    if (result.health.noFindings())
        summary += QStringLiteral("No findings from the checks that ran; this is not a health "
                                  "score and does not prove the project is problem-free.");
    m_healthSummary->setText(summary);
    addActivity(QStringLiteral("Health check: %1 finding(s)").arg(result.health.findings.size()));
    refreshDashboard();
}

void DevFlowPanel::showSelectedHealthFinding()
{
    const auto items = m_healthTable->selectedItems();
    if (items.isEmpty()) {
        m_healthDetails->clear();
        return;
    }
    const int row = items.first()->row();
    const QTableWidgetItem* idItem = m_healthTable->item(row, 0);
    const int index = idItem != nullptr ? idItem->data(Qt::UserRole).toInt() : -1;
    if (index < 0 || index >= m_lastHealth.findings.size())
        return;
    const devflow::HealthFinding& finding = m_lastHealth.findings.at(index);
    QString html;
    html += QStringLiteral("<p><b>%1</b></p>").arg(finding.title.toHtmlEscaped());
    html += QStringLiteral("<p>%1</p>").arg(finding.detail.toHtmlEscaped());
    html += QStringLiteral("<p><b>Evidence:</b> <code>%1</code></p>")
                .arg(finding.evidence.toHtmlEscaped());
    html += QStringLiteral("<p><b>Confidence:</b> %1 \u00B7 <b>Severity:</b> %2</p>")
                .arg(finding.confidence.toHtmlEscaped(),
                     devflow::healthSeverityName(finding.severity));
    m_healthDetails->setHtml(html);
}

QWidget* DevFlowPanel::buildWorkflowsTab()
{
    auto* page = new QWidget(m_tabs);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* toolbar = new QHBoxLayout();
    auto* add = new QPushButton(QStringLiteral("New workflow"), page);
    m_workflowRunButton = new QPushButton(QStringLiteral("Run"), page);
    m_workflowCancelButton = new QPushButton(QStringLiteral("Cancel"), page);
    m_workflowCancelButton->setEnabled(false);
    auto* remove = new QPushButton(QStringLiteral("Delete"), page);
    auto* openReport = new QPushButton(QStringLiteral("Open report folder"), page);
    toolbar->addWidget(add);
    toolbar->addWidget(m_workflowRunButton);
    toolbar->addWidget(m_workflowCancelButton);
    toolbar->addWidget(remove);
    toolbar->addWidget(openReport);
    toolbar->addStretch(1);
    layout->addLayout(toolbar);

    auto* splitter = new QSplitter(Qt::Horizontal, page);
    m_workflowList = new QListWidget(splitter);
    m_workflowList->setObjectName(QStringLiteral("devflowList"));
    splitter->addWidget(m_workflowList);
    m_workflowLog = new QPlainTextEdit(splitter);
    m_workflowLog->setReadOnly(true);
    m_workflowLog->setMaximumBlockCount(3000);
    m_workflowLog->setPlaceholderText(
        QStringLiteral("Workflow steps and their results appear here."));
    splitter->addWidget(m_workflowLog);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({220, 520});
    layout->addWidget(splitter, 1);

    m_workflowProgress = new QProgressBar(page);
    m_workflowProgress->setRange(0, 1);
    m_workflowProgress->setValue(0);
    layout->addWidget(m_workflowProgress);

    auto* note = new QLabel(
        QStringLiteral("Supported steps are fixed: scan, detect, dependencies, portability and "
                       "report. Workflows never run project scripts or build commands; the only "
                       "file written is the Markdown report."),
        page);
    note->setObjectName(QStringLiteral("hintLabel"));
    note->setWordWrap(true);
    layout->addWidget(note);

    connect(add, &QPushButton::clicked, this, &DevFlowPanel::newWorkflow);
    connect(m_workflowRunButton, &QPushButton::clicked, this, &DevFlowPanel::runWorkflow);
    connect(m_workflowCancelButton, &QPushButton::clicked, this,
            [this]() { m_workflowRunner->cancel(); });
    connect(remove, &QPushButton::clicked, this, &DevFlowPanel::deleteSelectedWorkflow);
    connect(openReport, &QPushButton::clicked, this, []() {
        const QString directory =
            QDir(Settings::dataDirectory()).filePath(QStringLiteral("reports"));
        QDir().mkpath(directory);
        QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    });
    return page;
}

void DevFlowPanel::refreshWorkflowList()
{
    if (m_workflowList == nullptr)
        return;
    m_workflowList->clear();
    auto* builtin = new QListWidgetItem(
        QStringLiteral("Project review (built-in)\nScan, detect, dependencies, portability, report"),
        m_workflowList);
    builtin->setData(Qt::UserRole, QStringLiteral("builtin"));
    for (const devflow::WorkflowDefinition& workflow : m_store.workflows()) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1\n%2 step(s)").arg(workflow.name).arg(workflow.steps.size()),
            m_workflowList);
        item->setData(Qt::UserRole, workflow.id);
    }
    m_workflowList->setCurrentRow(0);
}

void DevFlowPanel::newWorkflow()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("New workflow"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* name = new QLineEdit(&dialog);
    name->setPlaceholderText(QStringLiteral("Workflow name"));
    layout->addWidget(name);
    auto* hint = new QLabel(
        QStringLiteral("Only these supported steps can run. They are read-only."), &dialog);
    hint->setObjectName(QStringLiteral("hintLabel"));
    layout->addWidget(hint);
    QMap<QString, QCheckBox*> boxes;
    for (const QString& step : devflow::WorkflowDefinition::supportedSteps()) {
        auto* box = new QCheckBox(
            QStringLiteral("%1 \u2014 %2")
                .arg(step, devflow::WorkflowDefinition::stepDescription(step)),
            &dialog);
        box->setChecked(step != QLatin1String("report"));
        boxes.insert(step, box);
        layout->addWidget(box);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    devflow::WorkflowDefinition workflow;
    workflow.id = devflow::makeId(QStringLiteral("workflow"));
    workflow.name = name->text().trimmed();
    workflow.projectPath = projectPath();
    workflow.createdAt = QDateTime::currentDateTime();
    for (auto it = boxes.constBegin(); it != boxes.constEnd(); ++it) {
        if (it.value()->isChecked())
            workflow.steps.append(it.key());
    }
    QString error;
    if (!m_store.saveWorkflow(workflow, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot save workflow"), error);
        return;
    }
    refreshWorkflowList();
    addActivity(QStringLiteral("Saved workflow \"%1\"").arg(workflow.name));
}

void DevFlowPanel::runWorkflow()
{
    if (m_workflowRunner->isRunning())
        return;
    const QListWidgetItem* item = m_workflowList->currentItem();
    if (item == nullptr) {
        QMessageBox::information(this, QStringLiteral("Choose a workflow"),
                                 QStringLiteral("Select a workflow in the list first."));
        return;
    }
    const QString id = item->data(Qt::UserRole).toString();
    devflow::WorkflowDefinition workflow;
    if (id == QLatin1String("builtin")) {
        workflow.id = QStringLiteral("builtin");
        workflow.name = QStringLiteral("Project review");
        workflow.steps = devflow::WorkflowDefinition::supportedSteps();
    } else {
        for (const devflow::WorkflowDefinition& stored : m_store.workflows()) {
            if (stored.id == id) {
                workflow = stored;
                break;
            }
        }
    }
    if (workflow.steps.isEmpty()) {
        m_workflowLog->appendPlainText(QStringLiteral("The selected workflow has no steps."));
        return;
    }
    if (workflow.steps.contains(QStringLiteral("report"))) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Run workflow"),
            QStringLiteral("Run %1 step(s) and write a Markdown report into PKG Studio's data "
                           "folder? No project file is modified.")
                .arg(workflow.steps.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    m_workflowLog->clear();
    m_workflowReportPath.clear();
    m_workflowProgress->setRange(0, static_cast<int>(workflow.steps.size()));
    m_workflowProgress->setValue(0);
    m_workflowRunButton->setEnabled(false);
    m_workflowCancelButton->setEnabled(true);
    m_workflowLog->appendPlainText(
        QStringLiteral("Starting \"%1\" in %2").arg(workflow.name, projectPath()));
    const QString reports = QDir(Settings::dataDirectory()).filePath(QStringLiteral("reports"));
    addActivity(QStringLiteral("Started workflow \"%1\"").arg(workflow.name));
    m_workflowRunner->start(workflow, projectPath(), reports);
}

void DevFlowPanel::deleteSelectedWorkflow()
{
    const QListWidgetItem* item = m_workflowList->currentItem();
    if (item == nullptr)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty() || id == QLatin1String("builtin")) {
        QMessageBox::information(this, QStringLiteral("Built-in workflow"),
                                 QStringLiteral("The built-in workflow cannot be deleted."));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("Delete workflow"),
                              QStringLiteral("Delete this saved workflow?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
        QMessageBox::Yes) {
        return;
    }
    m_store.removeWorkflow(id);
    refreshWorkflowList();
}

void DevFlowPanel::onWorkflowStepStarted(const QString& step)
{
    m_workflowLog->appendPlainText(
        QStringLiteral("\u25B6 %1 \u2014 %2")
            .arg(step, devflow::WorkflowDefinition::stepDescription(step)));
}

void DevFlowPanel::onWorkflowStepFinished(const QString& step, bool ok, const QString& detail)
{
    m_workflowLog->appendPlainText(QStringLiteral("   %1 %2: %3")
                                       .arg(ok ? QStringLiteral("\u2713")
                                               : QStringLiteral("\u2717"),
                                            step, detail));
    m_workflowProgress->setValue(m_workflowProgress->value() + 1);
}

void DevFlowPanel::onWorkflowFinished(bool ok, const QString& error, const QString& reportPath)
{
    m_workflowRunButton->setEnabled(true);
    m_workflowCancelButton->setEnabled(false);
    if (!reportPath.isEmpty()) {
        m_workflowReportPath = reportPath;
        m_workflowLog->appendPlainText(QStringLiteral("Report: %1").arg(reportPath));
    }
    if (!ok) {
        m_workflowLog->appendPlainText(
            QStringLiteral("Workflow stopped: %1")
                .arg(error.isEmpty() ? QStringLiteral("unknown error") : error));
        addActivity(QStringLiteral("Workflow stopped: %1")
                        .arg(error.isEmpty() ? QStringLiteral("unknown error") : error));
    } else {
        m_workflowLog->appendPlainText(QStringLiteral("Workflow finished."));
        addActivity(QStringLiteral("Workflow finished"));
    }
    m_workflowProgress->setValue(m_workflowProgress->maximum());
}

}  // namespace pkg::ui
