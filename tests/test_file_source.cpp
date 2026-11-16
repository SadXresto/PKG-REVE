#include "core/FileSource.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <chrono>

using namespace pkg;

namespace {

QString writeFile(const QTemporaryDir& dir, const QString& name, const QByteArray& content)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(content);
    file.close();
    return path;
}

}  // namespace

TEST(FileSource, ReadsBytesAndRejectsOutOfRange)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeFile(dir, QStringLiteral("data.bin"), QByteArray("0123456789"));
    ASSERT_FALSE(path.isEmpty());

    auto source = FileSource::open(path);
    ASSERT_TRUE(source);
    EXPECT_EQ(source.value().size(), 10U);
    EXPECT_TRUE(source.value().isOpen());

    auto middle = source.value().read(4, 3);
    ASSERT_TRUE(middle);
    EXPECT_EQ(middle.value(), QByteArray("456"));

    EXPECT_FALSE(source.value().read(8, 3));
    EXPECT_FALSE(source.value().read(0, 11));
    EXPECT_FALSE(source.value().view(11, 0));
    EXPECT_TRUE(source.value().read(10, 0));
}

TEST(FileSource, HandlesEmptyFiles)
{
    QTemporaryDir dir;
    const QString path = writeFile(dir, QStringLiteral("empty.bin"), QByteArray());
    auto source = FileSource::open(path);
    ASSERT_TRUE(source);
    EXPECT_EQ(source.value().size(), 0U);
    auto bytes = source.value().read(0, 0);
    ASSERT_TRUE(bytes);
    EXPECT_TRUE(bytes.value().isEmpty());
    EXPECT_FALSE(source.value().read(0, 1));
}

TEST(FileSource, RejectsMissingFilesAndDirectories)
{
    QTemporaryDir dir;
    EXPECT_FALSE(FileSource::open(dir.filePath(QStringLiteral("missing"))));
    EXPECT_FALSE(FileSource::open(dir.path()));
}

TEST(FileSource, MapsLargeSparseFileWithoutLoadingIt)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("large.bin"));
    constexpr quint64 kSize = 4ULL * 1024ULL * 1024ULL * 1024ULL;  // 4 GiB

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadWrite));
    ASSERT_TRUE(file.resize(static_cast<qint64>(kSize)));
    ASSERT_TRUE(file.seek(static_cast<qint64>(kSize - 4)));
    ASSERT_EQ(file.write("TAIL", 4), 4);
    file.close();

    const auto start = std::chrono::steady_clock::now();
    auto source = FileSource::open(path);
    ASSERT_TRUE(source);
    EXPECT_EQ(source.value().size(), kSize);

    auto tail = source.value().read(kSize - 4, 4);
    ASSERT_TRUE(tail);
    EXPECT_EQ(tail.value(), QByteArray("TAIL"));

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    // Mapping is O(1); this is a generous bound even on slow CI machines.
    EXPECT_LT(elapsed.count(), 2000);

    const quint64 progress = 12345;
    Q_UNUSED(progress)
}
