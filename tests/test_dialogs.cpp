// The About and Feature status dialogs: content, and proof that they render
// to real pixels instead of only constructing widgets.
#include "ui/AboutDialog.h"
#include "ui/DialogCapture.h"
#include "ui/HelpDialog.h"

#include <QApplication>
#include <QDialog>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTextBrowser>

#include <gtest/gtest.h>

#include <memory>

using namespace pkg::ui;

TEST(AboutDialog, ReportsVersionBuildAndLocations)
{
    const AboutInfo info = collectAboutInfo();
    EXPECT_FALSE(info.version.isEmpty());
    EXPECT_FALSE(info.buildType.isEmpty());
    EXPECT_FALSE(info.qtVersion.isEmpty());
    EXPECT_FALSE(info.platform.isEmpty());
    EXPECT_FALSE(info.dataDirectory.isEmpty());

    EXPECT_NE(info.summaryHtml().indexOf(QStringLiteral("PKG Studio")), -1);
    EXPECT_NE(info.summaryHtml().indexOf(QStringLiteral("Development build")), -1);
    EXPECT_NE(info.summaryHtml().indexOf(QStringLiteral("PKG Studio %1").arg(info.version)), -1);

    const QString details = info.detailsText();
    EXPECT_NE(details.indexOf(QStringLiteral("Build type: %1").arg(info.buildType)), -1);
    EXPECT_NE(details.indexOf(QStringLiteral("Data directory: ")), -1);
    EXPECT_NE(details.indexOf(QStringLiteral("No telemetry")), -1);
}

TEST(AboutDialog, BuildsExplainedBoxWithSampleLocation)
{
    std::unique_ptr<QMessageBox> box(
        createAboutDialog(nullptr, QStringLiteral("/tmp/samples")));
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->objectName(), QStringLiteral("aboutDialog"));
    EXPECT_NE(box->text().indexOf(QStringLiteral("Development build")), -1);
    EXPECT_FALSE(box->informativeText().isEmpty());
    EXPECT_NE(box->detailedText().indexOf(QStringLiteral("/tmp/samples")), -1);

    std::unique_ptr<QMessageBox> noSamples(createAboutDialog(nullptr));
    ASSERT_NE(noSamples, nullptr);
    EXPECT_NE(noSamples->detailedText().indexOf(QStringLiteral("Sample files: not installed")), -1);
}

TEST(FeatureStatusDialog, ListsImplementedPartialAndPlannedFeatures)
{
    std::unique_ptr<QDialog> dialog(
        HelpDialog::createTopicDialog(nullptr, QStringLiteral("help.featureStatus")));
    ASSERT_NE(dialog, nullptr);
    EXPECT_NE(dialog->windowTitle().indexOf(QStringLiteral("Feature status")), -1);

    auto* browser = dialog->findChild<QTextBrowser*>(QStringLiteral("helpTopicBrowser"));
    ASSERT_NE(browser, nullptr);
    const QString text = browser->toPlainText();
    EXPECT_NE(text.indexOf(QStringLiteral("PKG Studio is a development build")), -1);
    EXPECT_NE(text.indexOf(QStringLiteral("Working and tested")), -1);
    EXPECT_NE(text.indexOf(QStringLiteral("Partial or experimental")), -1);
    EXPECT_NE(text.indexOf(QStringLiteral("Planned, not present")), -1);
    EXPECT_FALSE(browser->toHtml().isEmpty());
}

TEST(DialogCapture, UnknownTopicIsRejected)
{
    EXPECT_EQ(HelpDialog::createTopicDialog(nullptr, QStringLiteral("help.missing")), nullptr);
}

TEST(DialogCapture, SavesReadableAboutAndFeatureStatusScreenshots)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    std::unique_ptr<QMessageBox> about(createAboutDialog(nullptr, QStringLiteral("/tmp/samples")));
    ASSERT_NE(about, nullptr);
    QString error;
    const QString aboutPath = dir.filePath(QStringLiteral("about.png"));
    ASSERT_TRUE(captureDialog(about.get(), aboutPath, &error)) << error.toStdString();

    const QImage aboutImage(aboutPath);
    ASSERT_FALSE(aboutImage.isNull());
    EXPECT_GE(aboutImage.width(), 300);
    EXPECT_GE(aboutImage.height(), 120);
    EXPECT_GT(QFileInfo(aboutPath).size(), 4096);

    std::unique_ptr<QDialog> status(
        HelpDialog::createTopicDialog(nullptr, QStringLiteral("help.featureStatus")));
    ASSERT_NE(status, nullptr);
    const QString statusPath = dir.filePath(QStringLiteral("feature-status.png"));
    error.clear();
    ASSERT_TRUE(captureDialog(status.get(), statusPath, &error)) << error.toStdString();

    const QImage statusImage(statusPath);
    ASSERT_FALSE(statusImage.isNull());
    EXPECT_GE(statusImage.width(), 300);
    EXPECT_GE(statusImage.height(), 120);
    EXPECT_GT(QFileInfo(statusPath).size(), 4096);
}

TEST(DialogCapture, RejectsMissingDialogAndEmptyPath)
{
    QString error;
    EXPECT_FALSE(captureDialog(nullptr, QStringLiteral("x.png"), &error));
    EXPECT_FALSE(error.isEmpty());

    std::unique_ptr<QDialog> dialog(
        HelpDialog::createTopicDialog(nullptr, QStringLiteral("help.featureStatus")));
    ASSERT_NE(dialog, nullptr);
    error.clear();
    EXPECT_FALSE(captureDialog(dialog.get(), QString(), &error));
    EXPECT_FALSE(error.isEmpty());
}
