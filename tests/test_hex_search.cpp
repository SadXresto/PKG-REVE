#include "core/ByteSearch.h"
#include "core/FileSource.h"
#include "core/HexDocument.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstring>

using namespace pkg;

namespace {

struct OpenedDocument {
    QTemporaryDir dir;
    std::shared_ptr<HexDocument> document;
};

std::shared_ptr<HexDocument> openDocument(const QTemporaryDir& dir, const QByteArray& content)
{
    const QString path = dir.filePath(QStringLiteral("doc.bin"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(content);
    file.close();
    auto source = FileSource::open(path);
    if (!source)
        return {};
    return std::make_shared<HexDocument>(std::move(source.value()));
}

}  // namespace

TEST(HexDocument, ComputesRowsAndShortFinalRow)
{
    QTemporaryDir dir;
    auto document = openDocument(dir, QByteArray(33, 'A'));
    ASSERT_TRUE(document);

    HexLayout layout;
    layout.bytesPerRow = 16;
    layout.groupSize = 1;
    EXPECT_EQ(document->rowCount(layout), 3U);
    EXPECT_EQ(document->rowBytes(0, layout).value().size(), 16);
    EXPECT_EQ(document->rowBytes(2, layout).value().size(), 1);
    EXPECT_TRUE(document->rowBytes(3, layout).value().isEmpty());
    EXPECT_FALSE(document->byteAt(33));
    EXPECT_EQ(document->byteAt(32).value(), static_cast<quint8>('A'));
    EXPECT_EQ(document->clampOffset(999), 32U);
}

TEST(HexDocument, NormalizesLayout)
{
    HexLayout layout;
    layout.bytesPerRow = 15;
    layout.groupSize = 4;
    const HexLayout normalized = layout.normalized();
    EXPECT_EQ(normalized.bytesPerRow, 12);
    EXPECT_EQ(normalized.groupSize, 4);

    layout.bytesPerRow = 2;
    layout.groupSize = 1;
    EXPECT_EQ(layout.normalized().bytesPerRow, 8);
}

TEST(ByteSearch, FindsPatternsAcrossChunkBoundary)
{
    constexpr int kSize = 3 * 1024 * 1024;
    constexpr int kChunk = 1024 * 1024;
    QByteArray content(kSize, '\0');
    const QByteArray needle("NEEDLE");
    const int firstOffset = kChunk - 3;  // straddles the first chunk boundary
    memcpy(content.data() + firstOffset, needle.constData(), needle.size());
    const int secondOffset = 2 * kChunk + 10;
    memcpy(content.data() + secondOffset, needle.constData(), needle.size());

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("big.bin"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(content), kSize);
    file.close();

    auto source = FileSource::open(path);
    ASSERT_TRUE(source);
    CancelToken token;
    SearchOptions options;
    const auto result = searchBytes(source.value(), needle, options, token);
    ASSERT_TRUE(result);
    ASSERT_EQ(result.value().offsets.size(), 2);
    EXPECT_EQ(result.value().offsets.at(0), static_cast<quint64>(firstOffset));
    EXPECT_EQ(result.value().offsets.at(1), static_cast<quint64>(secondOffset));
    EXPECT_FALSE(result.value().truncated);
}

TEST(ByteSearch, SupportsCaseInsensitiveAscii)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("case.bin"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QByteArray("xxAbCxxabcxx"));
    file.close();

    auto source = FileSource::open(path);
    ASSERT_TRUE(source);
    SearchOptions options;
    options.caseInsensitive = true;
    CancelToken token;
    const auto result = searchBytes(source.value(), QByteArray("abc"), options, token);
    ASSERT_TRUE(result);
    ASSERT_EQ(result.value().offsets.size(), 2);
    EXPECT_EQ(result.value().offsets.at(0), 2U);
    EXPECT_EQ(result.value().offsets.at(1), 7U);
}

TEST(ByteSearch, RespectsHitLimitAndCancellation)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("many.bin"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QByteArray("aaaaaaaaaaaaaaaaaaaa"));  // 20 'a'
    file.close();

    auto source = FileSource::open(path);
    ASSERT_TRUE(source);
    SearchOptions options;
    options.maxHits = 4;
    CancelToken token;
    const auto limited = searchBytes(source.value(), QByteArray("a"), options, token);
    ASSERT_TRUE(limited);
    EXPECT_EQ(limited.value().offsets.size(), 4);
    EXPECT_TRUE(limited.value().truncated);

    CancelToken cancelled;
    cancelled.cancel();
    const auto aborted = searchBytes(source.value(), QByteArray("a"), options, cancelled);
    EXPECT_FALSE(aborted);

    SearchOptions invalid;
    EXPECT_FALSE(searchBytes(source.value(), QByteArray(), invalid, token));
}
