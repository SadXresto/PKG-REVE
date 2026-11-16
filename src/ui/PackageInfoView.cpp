#include "ui/PackageInfoView.h"

#include "core/FormatUtil.h"
#include "data/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace pkg::ui {
namespace {

QString colorFor(const Theme& theme, const QString& role)
{
    return theme.color(role).name(QColor::HexRgb);
}

}  // namespace

PackageInfoView::PackageInfoView(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(10, 6, 8, 6);
    m_title = new QLabel(QStringLiteral("Package information"), header);
    m_title->setObjectName(QStringLiteral("sectionLabel"));
    headerLayout->addWidget(m_title, 1);

    m_copyButton = new QPushButton(QStringLiteral("Copy report"), header);
    m_exportButton = new QPushButton(QStringLiteral("Export report\u2026"), header);
    m_copyButton->setEnabled(false);
    m_exportButton->setEnabled(false);
    headerLayout->addWidget(m_copyButton);
    headerLayout->addWidget(m_exportButton);
    layout->addWidget(header);

    m_browser = new QTextBrowser(this);
    m_browser->setOpenExternalLinks(false);
    layout->addWidget(m_browser, 1);

    connect(m_copyButton, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_report);
    });
    connect(m_exportButton, &QPushButton::clicked, this, &PackageInfoView::exportRequested);

    showPlaceholder(QStringLiteral("Package information"),
                    QStringLiteral("Open a file to see its format analysis."));
}

void PackageInfoView::showPlaceholder(const QString& title, const QString& message)
{
    m_title->setText(title);
    m_browser->setHtml(QStringLiteral("<p>%1</p>").arg(message.toHtmlEscaped()));
    m_report.clear();
    m_copyButton->setEnabled(false);
    m_exportButton->setEnabled(false);
}

void PackageInfoView::setInfo(const pkg::PackageInfo& info, const QString& filePath,
                              quint64 fileSize)
{
    const Theme& theme = ThemeManager::instance().current();
    const QString bg = colorFor(theme, QStringLiteral("base"));
    const QString text = colorFor(theme, QStringLiteral("hexBytes"));
    const QString muted = colorFor(theme, QStringLiteral("panelText"));
    const QString accent = colorFor(theme, QStringLiteral("hexOffset"));
    const QString border = colorFor(theme, QStringLiteral("border"));
    const QString warning = theme.dark ? QStringLiteral("#e0af68") : QStringLiteral("#8a5a00");

    m_title->setText(QStringLiteral("%1 \u2014 %2").arg(info.formatName, QFileInfo(filePath).fileName()));

    QString html;
    html += QStringLiteral("<html><body style=\"background:%1;color:%2;\">").arg(bg, text);
    html += QStringLiteral("<h2 style=\"color:%1;margin-bottom:2px;\">%2</h2>")
                .arg(accent, info.formatName.toHtmlEscaped());
    if (!info.summary.isEmpty()) {
        html += QStringLiteral("<p style=\"color:%1;margin-top:0;\">%2</p>")
                    .arg(muted, info.summary.toHtmlEscaped());
    }
    html += QStringLiteral("<p style=\"color:%1;\">File: %2<br/>Size: %3 (%4 bytes)<br/>Parser: %5 %6</p>")
                .arg(muted, filePath.toHtmlEscaped(), fmt::humanBytes(fileSize))
                .arg(fileSize)
                .arg(info.parserId.toHtmlEscaped(), info.parserVersion.toHtmlEscaped());

    for (const InfoSection& section : info.sections) {
        html += QStringLiteral("<h3 style=\"color:%1;border-bottom:1px solid %2;padding-bottom:2px;\">%3</h3>")
                    .arg(accent, border, section.title.toHtmlEscaped());
        html += QStringLiteral("<table cellspacing=\"0\" cellpadding=\"3\" width=\"100%\">");
        for (const InfoField& field : section.fields) {
            html += QStringLiteral("<tr><td width=\"32%\" style=\"color:%1;\">%2</td>"
                                   "<td style=\"color:%3;font-family:monospace;\">%4%5</td></tr>")
                        .arg(muted, field.name.toHtmlEscaped(), text,
                             field.value.toHtmlEscaped(),
                             field.note.isEmpty()
                                 ? QString()
                                 : QStringLiteral(" <span style=\"color:%1;\">(%2)</span>")
                                       .arg(muted, field.note.toHtmlEscaped()));
        }
        html += QStringLiteral("</table>");
    }

    if (!info.warnings.isEmpty()) {
        html += QStringLiteral("<h3 style=\"color:%1;\">Warnings</h3><ul>").arg(warning);
        for (const QString& item : info.warnings)
            html += QStringLiteral("<li style=\"color:%1;\">%2</li>").arg(warning,
                                                                         item.toHtmlEscaped());
        html += QStringLiteral("</ul>");
    }

    if (info.partial) {
        html += QStringLiteral("<p style=\"color:%1;\">This analysis is partial; see the "
                               "warnings above.</p>")
                    .arg(warning);
    }

    html += QStringLiteral("</body></html>");
    m_browser->setHtml(html);
    m_browser->verticalScrollBar()->setValue(0);

    m_report = QStringLiteral("PKG Studio analysis report\nFile: %1\nSize: %2 bytes\n\n%3")
                   .arg(filePath)
                   .arg(fileSize)
                   .arg(info.toPlainText());
    m_copyButton->setEnabled(true);
    m_exportButton->setEnabled(true);
}

}  // namespace pkg::ui
