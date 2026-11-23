#include "ui/OnboardingDialog.h"

#include "data/Settings.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace pkg::ui {
namespace {

QString pageStyle()
{
    return QStringLiteral(
        "<style>h2 { margin: 0 0 8px 0; } p { margin: 4px 0; } li { margin: 3px 0; }</style>");
}

QLabel* pageLabel(const QString& html, QWidget* parent)
{
    auto* label = new QLabel(html, parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    label->setOpenExternalLinks(false);
    label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    return label;
}

}  // namespace

OnboardingDialog::OnboardingDialog(const QString& version, QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("onboardingDialog"));
    setWindowTitle(QStringLiteral("Welcome to PKG Studio"));
    resize(640, 500);
    buildUi(version);
}

void OnboardingDialog::buildUi(const QString& version)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 14);
    layout->setSpacing(10);

    m_step = new QLabel(this);
    m_step->setObjectName(QStringLiteral("hintLabel"));
    layout->addWidget(m_step);

    m_pages = new QStackedWidget(this);

    m_pages->addWidget(pageLabel(
        pageStyle()
            + QStringLiteral("<h2>PKG Studio %1</h2>").arg(version.toHtmlEscaped())
            + QStringLiteral(
                "<p>An offline desktop workspace for package analysis, metadata inspection, "
                "byte-accurate editing and authorized binary development.</p>"
                "<p><b>Privacy by default.</b> No telemetry, no relay, no upload. The optional AI "
                "assistant contacts only the endpoint you configure; a loopback model keeps "
                "everything on this computer.</p>"
                "<p><b>Honest scope.</b> The app never decrypts or extracts protected commercial "
                "content, and every feature states whether it is working, experimental or "
                "planned.</p>"),
        this));

    m_pages->addWidget(pageLabel(
        pageStyle()
            + QStringLiteral("<h2>Open the sample project</h2>")
            + QStringLiteral(
                "<p>The bundled <code>samples/gameforge-demo</code> folder is a small synthetic "
                "CMake project with real build files, declared dependencies and source "
                "references. It makes every workspace demonstrable without opening a real "
                "game.</p>"
                "<ul>"
                "<li><b>Project Navigator</b> indexes files, targets, dependencies, "
                "configuration, source references, portability findings and build errors.</li>"
                "<li><b>Test Center</b> lists only frameworks it actually detects.</li>"
                "<li><b>Smart diff</b> compares two folder states without touching either.</li>"
                "</ul>"
                "<p>Use the button below, or open your own project folder at any time.</p>"),
        this));

    m_pages->addWidget(pageLabel(
        pageStyle()
            + QStringLiteral("<h2>What is new in 0.11.0</h2>")
            + QStringLiteral(
                "<ul>"
                "<li><b>Project Navigator</b>: evidence-backed relationships with a confidence "
                "label on every link.</li>"
                "<li><b>AI code actions</b>: explain a selection or a build error, request a "
                "refactor, documentation or a patch, then review the diff before applying "
                "anything.</li>"
                "<li><b>Test &amp; Debug Center</b>: real runs, failure grouping, cancellation, "
                "regression history and report export.</li>"
                "<li><b>Smart project diff</b>: files, configuration, dependencies, binary "
                "differences and portability changes.</li>"
                "<li><b>Extension Studio</b>: validate and package declarative plugins; plugin "
                "code is not executed.</li>"
                "</ul>"
                "<p>Press <b>Ctrl+Shift+P</b> to search every command, setting and help "
                "page.</p>"),
        this));

    layout->addWidget(m_pages, 1);

    auto* row = new QHBoxLayout();
    m_showAgain = new QCheckBox(QStringLiteral("Show this guide again next launch"), this);
    m_showAgain->setChecked(false);
    row->addWidget(m_showAgain);
    row->addStretch(1);
    m_back = new QPushButton(QStringLiteral("Back"), this);
    m_back->setEnabled(false);
    row->addWidget(m_back);
    m_next = new QPushButton(QStringLiteral("Next"), this);
    m_next->setDefault(true);
    row->addWidget(m_next);
    layout->addLayout(row);

    auto* extras = new QHBoxLayout();
    auto* sample = new QPushButton(QStringLiteral("Open sample project"), this);
    auto* guide = new QPushButton(QStringLiteral("Getting started guide"), this);
    extras->addWidget(sample);
    extras->addWidget(guide);
    extras->addStretch(1);
    layout->addLayout(extras);

    connect(m_back, &QPushButton::clicked, this, [this]() {
        if (m_index > 0)
            --m_index;
        updatePage();
    });
    connect(m_next, &QPushButton::clicked, this, &OnboardingDialog::next);
    connect(sample, &QPushButton::clicked, this, [this]() {
        emit sampleProjectRequested();
        accept();
    });
    connect(guide, &QPushButton::clicked, this, &OnboardingDialog::gettingStartedRequested);

    updatePage();
}

bool OnboardingDialog::showOnFirstLaunch() const
{
    return m_showAgain->isChecked();
}

void OnboardingDialog::updatePage()
{
    m_pages->setCurrentIndex(m_index);
    m_step->setText(QStringLiteral("Step %1 of %2").arg(m_index + 1).arg(m_pages->count()));
    m_back->setEnabled(m_index > 0);
    m_next->setText(m_index == m_pages->count() - 1 ? QStringLiteral("Start working")
                                                    : QStringLiteral("Next"));
}

void OnboardingDialog::next()
{
    if (m_index < m_pages->count() - 1) {
        ++m_index;
        updatePage();
        return;
    }
    accept();
}

}  // namespace pkg::ui
