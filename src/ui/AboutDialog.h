#pragma once

#include <QString>

class QMessageBox;
class QWidget;

namespace pkg::ui {

/// Everything Help → About reports. Collected in one place so the dialog, the
/// automated tests and the --screenshot-dialog diagnostics agree.
struct AboutInfo {
    QString version;
    QString buildType;
    QString qtVersion;
    QString platform;
    QString dataDirectory;
    QString samplesPath;
    QString logFilePath;

    [[nodiscard]] QString summaryHtml() const;
    [[nodiscard]] QString detailsText() const;
};

/// Reads version, build type, Qt version, platform and data locations.
/// `samplesPath` is filled in by the caller, which knows where the bundled
/// samples were found.
[[nodiscard]] AboutInfo collectAboutInfo();

/// Builds the About box without showing it, so tests and the screenshot
/// diagnostics can render it. The caller owns the result; it is parented to
/// `parent` when one is given.
[[nodiscard]] QMessageBox* createAboutDialog(QWidget* parent,
                                             const QString& samplesPath = QString());

}  // namespace pkg::ui
