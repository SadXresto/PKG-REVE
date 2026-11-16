#include <QApplication>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    // QApplication (not QCoreApplication) so widget-level tests can send real
    // key events. Headless runs use QT_QPA_PLATFORM=offscreen.
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PKGStudioTests"));
    QApplication::setOrganizationName(QStringLiteral("PKG Studio"));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
