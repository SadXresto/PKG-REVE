#include "core/ByteReader.h"
#include "core/FormatInfo.h"
#include "core/FormatUtil.h"
#include "core/SafeMath.h"

#include <gtest/gtest.h>

#include <limits>

using namespace pkg;

TEST(SafeMath, AdditionOverflowIsDetected)
{
    EXPECT_EQ(safe::add(1, 2).value(), 3U);
    EXPECT_FALSE(safe::add(std::numeric_limits<quint64>::max(), 1).has_value());
    EXPECT_TRUE(safe::inRange(0, 0, 0));
    EXPECT_TRUE(safe::inRange(10, 5, 15));
    EXPECT_FALSE(safe::inRange(10, 6, 15));
    EXPECT_FALSE(safe::inRange(std::numeric_limits<quint64>::max(), 2, 100));
}

TEST(SafeMath, MultiplicationOverflowIsDetected)
{
    EXPECT_EQ(safe::mul(1024, 1024).value(), 1048576U);
    EXPECT_FALSE(safe::mul(std::numeric_limits<quint64>::max(), 2).has_value());
}

TEST(SafeMath, ParseOffsetAcceptsDecimalAndHex)
{
    EXPECT_EQ(safe::parseOffset(QStringLiteral("0x1F")).value(), 31U);
    EXPECT_EQ(safe::parseOffset(QStringLiteral("31")).value(), 31U);
    EXPECT_EQ(safe::parseOffset(QStringLiteral(" 1_024 ")).value(), 1024U);
    EXPECT_FALSE(safe::parseOffset(QStringLiteral("nonsense")).has_value());
    EXPECT_FALSE(safe::parseOffset(QStringLiteral("0x")).has_value());
}

TEST(ByteReader, ReadsBothEndiannesses)
{
    QByteArray bytes;
    bytes.append(char(0x12));
    bytes.append(char(0x34));
    bytes.append(char(0x56));
    bytes.append(char(0x78));

    ByteReader reader(bytes);
    EXPECT_EQ(reader.u16be().value(), 0x1234);
    EXPECT_EQ(reader.u16le().value(), 0x7856);
    EXPECT_EQ(reader.u32beAt(0).value(), 0x12345678U);
    EXPECT_EQ(reader.u32leAt(0).value(), 0x78563412U);
}

TEST(ByteReader, RejectsOverrunWithoutMoving)
{
    QByteArray bytes(4, '\0');
    ByteReader reader(bytes);
    EXPECT_TRUE(reader.skip(4));
    const auto position = reader.position();
    auto result = reader.u32be();
    EXPECT_FALSE(result);
    EXPECT_EQ(reader.position(), position);
    EXPECT_EQ(reader.remaining(), 0U);
    EXPECT_TRUE(reader.atEnd());
}

TEST(ByteReader, FixedStringStopsAtNul)
{
    QByteArray bytes(16, '\x7f');
    bytes[0] = 'a';
    bytes[1] = 'b';
    bytes[2] = 'c';
    bytes[3] = '\0';
    ByteReader reader(bytes);
    EXPECT_EQ(reader.fixedString(16).value(), QStringLiteral("abc"));
    EXPECT_EQ(reader.remaining(), 0U);
}

TEST(FormatUtil, HexAndSizes)
{
    EXPECT_EQ(fmt::hex(0x1f, 4), QStringLiteral("001F"));
    EXPECT_EQ(fmt::hexPrefixed(0x10, 4), QStringLiteral("0x0010"));
    EXPECT_EQ(fmt::humanBytes(0), QStringLiteral("0 B"));
    EXPECT_EQ(fmt::humanBytes(1536), QStringLiteral("1.50 KiB"));
}

TEST(FormatInfo, JsonRoundTripPreservesFields)
{
    PackageInfo info;
    info.kind = FormatKind::Pe;
    info.formatName = QStringLiteral("PE");
    info.parserId = QStringLiteral("builtin.pe");
    info.parserVersion = QStringLiteral("1.0");
    info.summary = QStringLiteral("summary");
    info.warnings << QStringLiteral("warning one");
    info.partial = true;
    InfoSection section;
    section.title = QStringLiteral("Header");
    section.fields << InfoField{QStringLiteral("Machine"), QStringLiteral("x64"),
                                QStringLiteral("note")};
    info.sections << section;

    auto restored = PackageInfo::fromJson(info.toJson());
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored.value().kind, FormatKind::Pe);
    EXPECT_EQ(restored.value().summary, QStringLiteral("summary"));
    EXPECT_EQ(restored.value().warnings.size(), 1);
    EXPECT_TRUE(restored.value().partial);
    ASSERT_EQ(restored.value().sections.size(), 1);
    ASSERT_EQ(restored.value().sections.first().fields.size(), 1);
    EXPECT_EQ(restored.value().sections.first().fields.first().note,
              QStringLiteral("note"));
}

TEST(FormatInfo, RejectsInvalidJson)
{
    EXPECT_FALSE(PackageInfo::fromJson(QJsonObject{}));
    QJsonObject object;
    object.insert(QStringLiteral("kind"), 999);
    EXPECT_FALSE(PackageInfo::fromJson(object));
}
