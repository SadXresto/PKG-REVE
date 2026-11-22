#include "core/BinaryCompare.h"
#include "core/CancelToken.h"
#include "core/HexBookmarks.h"
#include "core/Logging.h"
#include "data/Database.h"
#include "data/Settings.h"
#include "ui/OutputPanel.h"
#include "ui/SettingsDialog.h"

#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace {

QString writeFile(const QTemporaryDir& dir, const QString& name, const QByteArray& bytes)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(bytes);
    file.close();
    return path;
}

}  // namespace

TEST(HexBookmarksTest, AddToggleAndAnnotation)
{
    pkg::HexBookmarks bookmarks;
    EXPECT_TRUE(bookmarks.isEmpty());
    EXPECT_TRUE(bookmarks.add(0x10, QStringLiteral("header")));
    EXPECT_TRUE(bookmarks.add(0x40));
    EXPECT_TRUE(bookmarks.add(0x10, QStringLiteral("header")) == false);

    EXPECT_TRUE(bookmarks.contains(0x10));
    EXPECT_EQ(bookmarks.note(0x10), QStringLiteral("header"));
    EXPECT_EQ(bookmarks.count(), 2);

    EXPECT_FALSE(bookmarks.toggle(0x40));
    EXPECT_FALSE(bookmarks.contains(0x40));
    EXPECT_TRUE(bookmarks.toggle(0x80, QStringLiteral("strings")));
    EXPECT_EQ(bookmarks.count(), 2);

    EXPECT_FALSE(bookmarks.remove(0x99));
    EXPECT_TRUE(bookmarks.remove(0x80));
}

TEST(HexBookmarksTest, NavigationWraps)
{
    pkg::HexBookmarks bookmarks;
    bookmarks.add(0x20);
    bookmarks.add(0x10);
    bookmarks.add(0x30);

    EXPECT_EQ(bookmarks.next(0), std::optional<quint64>(0x10));
    EXPECT_EQ(bookmarks.next(0x10), std::optional<quint64>(0x20));
    EXPECT_EQ(bookmarks.next(0x30), std::optional<quint64>(0x10));
    EXPECT_EQ(bookmarks.previous(0x30), std::optional<quint64>(0x20));
    EXPECT_EQ(bookmarks.previous(0x10), std::optional<quint64>(0x30));
    EXPECT_EQ(bookmarks.previous(0), std::optional<quint64>(0x30));

    pkg::HexBookmarks empty;
    EXPECT_FALSE(empty.next(0).has_value());
    EXPECT_FALSE(empty.previous(0).has_value());
}

TEST(HexBookmarksTest, JsonRoundTripIgnoresMalformedEntries)
{
    pkg::HexBookmarks bookmarks;
    bookmarks.add(0x1000, QStringLiteral("resource table"));
    bookmarks.add(0x2000);

    const pkg::HexBookmarks restored = pkg::HexBookmarks::fromJson(bookmarks.toJson());
    EXPECT_EQ(restored.count(), 2);
    EXPECT_EQ(restored.note(0x1000), QStringLiteral("resource table"));

    QJsonArray malformed;
    malformed.append(QJsonObject{{QStringLiteral("note"), QStringLiteral("no offset")}});
    malformed.append(QJsonObject{{QStringLiteral("offset"), -4}});
    malformed.append(QJsonValue(7));
    EXPECT_TRUE(pkg::HexBookmarks::fromJson(malformed).isEmpty());
}

TEST(BinaryCompareTest, IdenticalAndDifferingRuns)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QByteArray left("0123456789abcdef", 16);
    QByteArray right = left;
    right[4] = 'X';
    right[5] = 'Y';
    right[12] = 'Z';
    const QString a = writeFile(dir, QStringLiteral("a.bin"), left);
    const QString b = writeFile(dir, QStringLiteral("b.bin"), right);

    auto result = pkg::compareFiles(a, b);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().sizeA, 16u);
    EXPECT_EQ(result.value().sizeB, 16u);
    EXPECT_FALSE(result.value().identical());
    EXPECT_EQ(result.value().differingBytes, 3u);
    EXPECT_EQ(result.value().firstDifference, 4u);
    ASSERT_EQ(result.value().runs.size(), 2);
    EXPECT_EQ(result.value().runs.at(0).offset, 4u);
    EXPECT_EQ(result.value().runs.at(0).length, 2u);
    EXPECT_EQ(result.value().runs.at(1).offset, 12u);

    auto same = pkg::compareFiles(a, a);
    ASSERT_TRUE(same);
    EXPECT_TRUE(same.value().identical());
    EXPECT_TRUE(same.value().runs.isEmpty());
}

TEST(BinaryCompareTest, SizeDifferenceAndBudgetReportHonestly)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString a = writeFile(dir, QStringLiteral("a.bin"), QByteArray("abcd", 4));
    const QString b = writeFile(dir, QStringLiteral("b.bin"), QByteArray("ab", 2));

    auto result = pkg::compareFiles(a, b);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().differingBytes, 2u);
    ASSERT_EQ(result.value().runs.size(), 1);
    EXPECT_TRUE(result.value().runs.first().onlyInOne);
    EXPECT_EQ(result.value().runs.first().offset, 2u);

    const QString large = writeFile(dir, QStringLiteral("large.bin"), QByteArray(4096, 'a'));
    auto truncated = pkg::compareFiles(large, large, 512);
    ASSERT_TRUE(truncated);
    EXPECT_TRUE(truncated.value().truncated);
    EXPECT_EQ(truncated.value().comparedBytes, 512u);
}

TEST(OutputPanelTest, SecretsAreRedactedBeforeDisplay)
{
    EXPECT_EQ(pkg::ui::OutputPanel::redactSecrets(
                  QStringLiteral("request api_key=sk-live-1234 to the provider")),
              QStringLiteral("request api_key=***redacted*** to the provider"));
    EXPECT_EQ(pkg::ui::OutputPanel::redactSecrets(
                  QStringLiteral("Authorization: Bearer abc.def")),
              QStringLiteral("Authorization: ***redacted***"));
    EXPECT_EQ(pkg::ui::OutputPanel::redactSecrets(QStringLiteral("plain message")),
              QStringLiteral("plain message"));
}

TEST(OutputPanelTest, FormatRecordHonoursTimestampAndLevel)
{
    pkg::LogRecord record;
    record.timestamp = QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5, 6));
    record.level = pkg::LogLevel::Warning;
    record.category = QStringLiteral("build");
    record.message = QStringLiteral("token=abc123 unfinished");

    const QString withTime = pkg::ui::OutputPanel::formatRecord(record, true);
    EXPECT_TRUE(withTime.startsWith(QStringLiteral("03:04:05.006")));
    EXPECT_TRUE(withTime.contains(QStringLiteral("WARN")));
    EXPECT_TRUE(withTime.contains(QStringLiteral("[build]")));
    EXPECT_FALSE(withTime.contains(QStringLiteral("abc123")));

    const QString withoutTime = pkg::ui::OutputPanel::formatRecord(record, false);
    EXPECT_FALSE(withoutTime.startsWith(QStringLiteral("03:04")));
    EXPECT_TRUE(withoutTime.startsWith(QStringLiteral("[WARN")));
}

TEST(SettingsCenterTest, HasAllApexSectionsAndSearchablePages)
{
    pkg::AppSettings values;
    pkg::ui::SettingsDialog dialog(values, {});
    auto* categories = dialog.findChild<QListWidget*>(QStringLiteral("settingsCategories"));
    ASSERT_NE(categories, nullptr);
    EXPECT_EQ(categories->count(), 10);

    QStringList titles;
    for (int row = 0; row < categories->count(); ++row)
        titles << categories->item(row)->text();
    for (const QString& expected : {QStringLiteral("Appearance"), QStringLiteral("Editor"),
                                    QStringLiteral("Hex Editor"),
                                    QStringLiteral("GameForge"),
                                    QStringLiteral("AI Providers"),
                                    QStringLiteral("Performance"),
                                    QStringLiteral("Keyboard Shortcuts"),
                                    QStringLiteral("Plugins"),
                                    QStringLiteral("Privacy and Security"),
                                    QStringLiteral("Updates")}) {
        EXPECT_TRUE(titles.contains(expected)) << expected.toStdString();
    }

    auto* search = dialog.findChild<QLineEdit*>();
    ASSERT_NE(search, nullptr);
    search->setText(QStringLiteral("no-such-setting"));
    int visible = 0;
    for (int row = 0; row < categories->count(); ++row)
        visible += categories->item(row)->isHidden() ? 0 : 1;
    EXPECT_EQ(visible, 0);

    search->clear();
    visible = 0;
    for (int row = 0; row < categories->count(); ++row)
        visible += categories->item(row)->isHidden() ? 0 : 1;
    EXPECT_EQ(visible, categories->count());
}

TEST(SettingsCenterTest, ValuesRoundTripAndReset)
{
    pkg::AppSettings values;
    values.themeId = QStringLiteral("pkgstudio.cyber.blue");
    values.fontSize = 13;
    values.hexBytesPerRow = 32;
    values.confirmWrites = false;
    pkg::ui::SettingsDialog dialog(values, {});

    const pkg::AppSettings returned = dialog.values();
    EXPECT_EQ(returned.themeId, values.themeId);
    EXPECT_EQ(returned.fontSize, 13);
    EXPECT_EQ(returned.hexBytesPerRow, 32);
    EXPECT_FALSE(returned.confirmWrites);
}

TEST(SettingsCenterTest, ThemePreferenceSurvivesReload)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    pkg::Database database;
    ASSERT_TRUE(database.open(dir.filePath(QStringLiteral("settings.db"))));
    pkg::Settings::instance().attach(&database);

    pkg::AppSettings values = pkg::Settings::instance().values();
    values.themeId = QStringLiteral("pkgstudio.terminal.classic");
    pkg::Settings::instance().setValues(values);

    // Re-attaching reloads the persisted rows, as a restart would.
    pkg::Settings::instance().attach(&database);
    EXPECT_EQ(pkg::Settings::instance().values().themeId,
              QStringLiteral("pkgstudio.terminal.classic"));
    EXPECT_EQ(database.setting(QStringLiteral("ui.theme")),
              QStringLiteral("pkgstudio.terminal.classic"));
}

TEST(SettingsCenterTest, CategoryCanBeSelectedForSearchResults)
{
    pkg::AppSettings values;
    pkg::ui::SettingsDialog dialog(values, {});
    EXPECT_TRUE(dialog.selectCategory(QStringLiteral("Privacy and Security")));
    EXPECT_TRUE(dialog.selectCategory(QStringLiteral("hex editor")));
    EXPECT_FALSE(dialog.selectCategory(QStringLiteral("No such section")));
}

TEST(BinaryCompareTest, MissingFileAndCancellationFail)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString a = writeFile(dir, QStringLiteral("a.bin"), QByteArray("abc", 3));
    EXPECT_FALSE(pkg::compareFiles(a, dir.filePath(QStringLiteral("missing.bin"))));

    pkg::CancelToken token;
    token.cancel();
    EXPECT_FALSE(pkg::compareFiles(a, a, 64, token));
}
