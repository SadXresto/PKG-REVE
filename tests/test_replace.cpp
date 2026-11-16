#include "core/ByteSearch.h"
#include "core/FileSource.h"
#include "core/HexDocument.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>

using namespace pkg;

namespace {

std::shared_ptr<HexDocument> openDocument(const QTemporaryDir& dir, const QByteArray& content)
{
    const QString path = dir.filePath(QStringLiteral("replace.bin"));
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

TEST(Replace, ReplacesEveryMatchAtTheSearchOffsets)
{
    QTemporaryDir dir;
    auto document = openDocument(dir, QByteArray("ABxxABABzz"));
    ASSERT_TRUE(document);

    CancelToken token;
    const auto found = searchBytes(document->source(), QByteArray("AB"), SearchOptions{}, token);
    ASSERT_TRUE(found);
    ASSERT_EQ(found.value().offsets.size(), 3);

    const auto replaced = replaceAllEqualLength(*document, found.value().offsets, "AB", "CD");
    ASSERT_TRUE(replaced);
    EXPECT_EQ(replaced.value(), 3);
    EXPECT_EQ(document->readMerged(0, 10).value(), QByteArray("CDxxCDCDzz"));
    EXPECT_TRUE(document->isModified());
    EXPECT_EQ(document->modifiedByteCount(), 6);

    EXPECT_TRUE(document->undo());
    EXPECT_EQ(document->byteAt(7).value(), static_cast<quint8>('B'));
}

TEST(Replace, RejectsLengthChangesAndEmptyPatterns)
{
    QTemporaryDir dir;
    auto document = openDocument(dir, QByteArray("ABCD"));
    ASSERT_TRUE(document);

    auto longer = replaceAllEqualLength(*document, {0}, "AB", "ABC");
    EXPECT_FALSE(longer);
    EXPECT_NE(longer.error().detail.indexOf(QStringLiteral("insert/delete")), -1);

    EXPECT_FALSE(replaceAllEqualLength(*document, {0}, QByteArray(), "X"));
    EXPECT_FALSE(document->isModified());
}

TEST(Replace, RejectsOutOfRangeOffsetsWithoutWriting)
{
    QTemporaryDir dir;
    auto document = openDocument(dir, QByteArray("ABCD"));
    ASSERT_TRUE(document);

    const auto bad = replaceAllEqualLength(*document, {3, 3}, "AB", "XY");
    EXPECT_FALSE(bad);
    EXPECT_FALSE(document->isModified());
}
