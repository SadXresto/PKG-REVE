#include "ui/ByteInspector.h"

#include <gtest/gtest.h>

using namespace pkg;

namespace {

QString rowValue(const QList<QPair<QString, QString>>& rows, const QString& key)
{
    for (const auto& row : rows) {
        if (row.first == key)
            return row.second;
    }
    return {};
}

}  // namespace

TEST(ByteInspector, InterpretsLittleAndBigEndian)
{
    const QByteArray bytes = QByteArray::fromHex("0102");
    const auto rows = ui::ByteInspector::interpretations(bytes);

    EXPECT_EQ(rowValue(rows, QStringLiteral("Hex")), QStringLiteral("01 02"));
    EXPECT_EQ(rowValue(rows, QStringLiteral("Decimal")), QStringLiteral("1"));
    EXPECT_EQ(rowValue(rows, QStringLiteral("Binary")), QStringLiteral("0000 0001"));
    EXPECT_EQ(rowValue(rows, QStringLiteral("Unsigned 8")), QStringLiteral("1"));
    EXPECT_EQ(rowValue(rows, QStringLiteral("UInt16 LE")), QStringLiteral("513"));
    EXPECT_EQ(rowValue(rows, QStringLiteral("UInt16 BE")), QStringLiteral("258"));
    EXPECT_EQ(rowValue(rows, QStringLiteral("UInt32 LE")), QStringLiteral("\u2014"));
}

TEST(ByteInspector, InterpretsSignedValuesAndFloats)
{
    const auto negative = ui::ByteInspector::interpretations(QByteArray::fromHex("ff"));
    EXPECT_EQ(rowValue(negative, QStringLiteral("Signed 8")), QStringLiteral("-1"));

    const auto floatRows = ui::ByteInspector::interpretations(QByteArray::fromHex("0000803f"));
    EXPECT_EQ(rowValue(floatRows, QStringLiteral("Float32 LE")), QStringLiteral("1"));
    EXPECT_TRUE(rowValue(floatRows, QStringLiteral("Float32 BE"))
                    .startsWith(QStringLiteral("4.6006")));
}

TEST(ByteInspector, EmptyInputProducesNoRows)
{
    EXPECT_TRUE(ui::ByteInspector::interpretations(QByteArray()).isEmpty());
}
