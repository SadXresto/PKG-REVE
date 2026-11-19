#include "ui/AboutDialog.h"

#include "core/Logging.h"
#include "data/Settings.h"

#include <QApplication>
#include <QMessageBox>
#include <QSysInfo>

#ifndef PKGSTUDIO_BUILD_TYPE
#define PKGSTUDIO_BUILD_TYPE "unknown"
#endif

namespace pkg::ui {

QString AboutInfo::summaryHtml() const
{
    return QStringLiteral("<h3>PKG Studio %1</h3>"
                          "<p><b>Development build</b> \u00b7 %2 \u00b7 Qt %3</p>"
                          "<p>Package analysis, metadata inspection and authorized binary "
                          "development. This is a demonstration/evaluation build, not a signed "
                          "release. Features listed as partial or planned under "
                          "<i>Help \u2192 Feature status</i> are not complete.</p>"
                          "<p>Working today: ELF/PE/ZIP/APK and PSP/Vita/PS4 header analysis, the "
                          "hex view with live inspector and signature overlay, same-size byte "
                          "editing with undo and safe saving, the structure builder (form and "
                          "JSON), the command center and offline help.</p>")
        .arg(version, buildType, qtVersion);
}

QString AboutInfo::detailsText() const
{
    return QStringLiteral("Build type: %1\n"
                          "Qt: %2\n"
                          "Platform: %3\n"
                          "Data directory: %4\n"
                          "Sample files: %5\n"
                          "Log file: %6\n"
                          "\n"
                          "No telemetry. Nothing is uploaded.")
        .arg(buildType, qtVersion, platform, dataDirectory, samplesPath, logFilePath);
}

AboutInfo collectAboutInfo()
{
    AboutInfo info;
    info.version = QApplication::applicationVersion();
    if (info.version.isEmpty())
        info.version = QStringLiteral("unknown");
    info.buildType = QString::fromLatin1(PKGSTUDIO_BUILD_TYPE).trimmed();
    if (info.buildType.isEmpty())
        info.buildType = QStringLiteral("custom");
    info.qtVersion = QString::fromLatin1(qVersion());
    info.platform = QSysInfo::prettyProductName();
    info.dataDirectory = Settings::dataDirectory();
    info.logFilePath = Logger::instance().logFilePath();
    return info;
}

QMessageBox* createAboutDialog(QWidget* parent, const QString& samplesPath)
{
    AboutInfo info = collectAboutInfo();
    info.samplesPath = samplesPath.isEmpty() ? QStringLiteral("not installed") : samplesPath;

    auto* box = new QMessageBox(QMessageBox::Information,
                                QStringLiteral("About PKG Studio"), info.summaryHtml(),
                                QMessageBox::Ok, parent);
    box->setObjectName(QStringLiteral("aboutDialog"));
    box->setTextFormat(Qt::RichText);
    box->setInformativeText(QStringLiteral("Platform: %1").arg(info.platform));
    box->setDetailedText(info.detailsText());
    return box;
}

}  // namespace pkg::ui
