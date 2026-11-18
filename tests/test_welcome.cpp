// First-launch welcome page: build identity, sample actions and their
// availability state.
#include "ui/WelcomePage.h"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>

#include <gtest/gtest.h>

using namespace pkg::ui;

TEST(WelcomePageWidget, ShowsDevelopmentBuildAndSampleActions)
{
    WelcomePage page;
    page.setDevelopmentBuild(QStringLiteral("0.6.0"));

    auto* build = page.findChild<QLabel*>(QStringLiteral("buildIdentityLabel"));
    ASSERT_NE(build, nullptr);
    EXPECT_NE(build->text().indexOf(QStringLiteral("Development build 0.6.0")), -1);

    auto* elf = page.findChild<QPushButton*>(QStringLiteral("demoElfButton"));
    auto* apk = page.findChild<QPushButton*>(QStringLiteral("demoApkButton"));
    auto* project = page.findChild<QPushButton*>(QStringLiteral("demoProjectButton"));
    auto* featureStatus = page.findChild<QPushButton*>(QStringLiteral("featureStatusButton"));
    ASSERT_NE(elf, nullptr);
    ASSERT_NE(apk, nullptr);
    ASSERT_NE(project, nullptr);
    ASSERT_NE(featureStatus, nullptr);

    // Samples missing: the actions stay visible but explain why they are off.
    page.setDemoSamples(false, QString());
    EXPECT_FALSE(elf->isEnabled());
    EXPECT_FALSE(apk->isEnabled());
    EXPECT_FALSE(project->isEnabled());
    EXPECT_NE(elf->toolTip().indexOf(QStringLiteral("not installed")), -1);

    page.setDemoSamples(true, QStringLiteral("/tmp/samples"));
    EXPECT_TRUE(elf->isEnabled());
    EXPECT_TRUE(apk->isEnabled());
    EXPECT_TRUE(project->isEnabled());
    EXPECT_NE(project->toolTip().indexOf(QStringLiteral("/tmp/samples")), -1);

    QStringList requested;
    QObject::connect(&page, &WelcomePage::sampleFileRequested,
                     [&requested](const QString& name) { requested << name; });
    bool projectRequested = false;
    bool statusRequested = false;
    QObject::connect(&page, &WelcomePage::sampleProjectRequested,
                     [&projectRequested]() { projectRequested = true; });
    QObject::connect(&page, &WelcomePage::featureStatusRequested,
                     [&statusRequested]() { statusRequested = true; });

    elf->click();
    apk->click();
    project->click();
    featureStatus->click();

    ASSERT_EQ(requested.size(), 2);
    EXPECT_EQ(requested.at(0), QStringLiteral("sample.elf"));
    EXPECT_EQ(requested.at(1), QStringLiteral("sample.apk"));
    EXPECT_TRUE(projectRequested);
    EXPECT_TRUE(statusRequested);
}

TEST(WelcomePageWidget, ExplainsTheEmptyRecentList)
{
    WelcomePage page;
    page.setRecent({});
    auto* recent = page.findChild<QListWidget*>();
    ASSERT_NE(recent, nullptr);
    ASSERT_EQ(recent->count(), 1);
    EXPECT_NE(recent->item(0)->text().indexOf(QStringLiteral("sample buttons")), -1);
    EXPECT_FALSE(recent->item(0)->flags() & Qt::ItemIsEnabled);
}
