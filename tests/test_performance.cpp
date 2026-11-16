// Bounded performance checks for large files. Thresholds are deliberately
// generous so that slow CI machines do not fail, while still catching
// accidental "load the whole file" regressions.
#include "core/ByteSearch.h"
#include "core/FileSource.h"
#include "core/HexDocument.h"
#include "core/ProcessInfo.h"

#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace pkg;

TEST(Performance, LargeSparseFileStaysStreamed)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("huge.bin"));
    constexpr quint64 kSize = 8ULL * 1024ULL * 1024ULL * 1024ULL;  // 8 GiB sparse

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadWrite));
    ASSERT_TRUE(file.resize(static_cast<qint64>(kSize)));
    ASSERT_TRUE(file.seek(static_cast<qint64>(kSize - 32)));
    ASSERT_GT(file.write("TAILMARKER", 10), 0);
    file.close();

    const auto residentBefore = sysinfo::currentResidentBytes();

    QElapsedTimer timer;
    timer.start();
    auto source = FileSource::open(path);
    ASSERT_TRUE(source);
    EXPECT_EQ(source.value().size(), kSize);

    auto tail = source.value().read(kSize - 32, 10);
    ASSERT_TRUE(tail);
    EXPECT_EQ(tail.value(), QByteArray("TAILMARKER"));

    auto document = std::make_shared<HexDocument>(std::move(source.value()));
    HexLayout layout;
    layout.bytesPerRow = 16;
    layout.groupSize = 1;
    const quint64 rows = document->rowCount(layout);
    EXPECT_EQ(rows, (kSize + 15) / 16);

    auto lastRow = document->rowBytes(rows - 1, layout);
    ASSERT_TRUE(lastRow);
    EXPECT_EQ(lastRow.value().size(), 16);

    SearchOptions options;
    options.startOffset = kSize - 64 * 1024;
    CancelToken token;
    const auto hits = searchBytes(document->source(), QByteArray("TAILMARKER"), options, token);
    ASSERT_TRUE(hits);
    EXPECT_EQ(hits.value().offsets.size(), 1);

    const qint64 elapsedMs = timer.elapsed();
    EXPECT_LT(elapsedMs, 2000) << "streaming access took " << elapsedMs << " ms";

    const auto residentAfter = sysinfo::currentResidentBytes();
    if (residentBefore && residentAfter) {
        const qint64 growth =
            static_cast<qint64>(residentAfter.value()) - static_cast<qint64>(residentBefore.value());
        EXPECT_LT(growth, 128LL * 1024 * 1024)
            << "resident memory grew by " << growth << " bytes";
        if (growth > 0) {
            RecordProperty("residentGrowthBytes", static_cast<int>(growth));
        }
    }
    RecordProperty("elapsedMs", static_cast<int>(elapsedMs));
}

TEST(Performance, HexRowAccessIsConstantTime)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("medium.bin"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadWrite));
    ASSERT_TRUE(file.resize(64 * 1024 * 1024));
    file.close();

    auto source = FileSource::open(path);
    ASSERT_TRUE(source);
    HexDocument document(std::move(source.value()));
    HexLayout layout;
    layout.bytesPerRow = 16;

    QElapsedTimer timer;
    timer.start();
    quint64 checksum = 0;
    for (quint64 row = 0; row < 3000; ++row) {
        const auto bytes = document.rowBytes(row * 1000, layout);
        ASSERT_TRUE(bytes);
        checksum += static_cast<quint64>(bytes.value().size());
    }
    EXPECT_EQ(checksum, 3000U * 16U);
    EXPECT_LT(timer.elapsed(), 1000);
}
