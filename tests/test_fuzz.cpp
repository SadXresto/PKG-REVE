// Deterministic fuzz-style tests for the parser layer. They are not a
// replacement for libFuzzer (see TESTING.md), but they exercise thousands of
// truncated and random inputs on every build and would have caught the classic
// "trust the length field" bugs.
#include "TestData.h"

#include "core/CancelToken.h"
#include "core/FileSource.h"
#include "core/IFormatParser.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstdint>

using namespace pkg;

namespace {

struct Rng {
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;

    std::uint32_t next()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<std::uint32_t>(state >> 32);
    }

    std::uint32_t below(std::uint32_t bound) { return bound == 0 ? 0 : next() % bound; }
};

Result<PackageInfo> analyze(const QByteArray& content)
{
    QTemporaryDir dir;
    const QString path = testdata::writeTempFile(dir.path(), QStringLiteral("fuzz.bin"), content);
    if (path.isEmpty())
        return makeError(QStringLiteral("fixture write failed"));
    auto source = FileSource::open(path);
    if (!source)
        return source.error();
    CancelToken token;
    return ParserRegistry::instance().analyze(source.value(), token);
}

void expectSaneResult(const Result<PackageInfo>& result)
{
    if (!result)
        return;
    EXPECT_FALSE(result.value().parserId.isEmpty());
    EXPECT_FALSE(result.value().formatName.isEmpty());
}

}  // namespace

class FuzzTest : public ::testing::Test {
protected:
    void SetUp() override { ParserRegistry::registerBuiltins(); }
};

TEST_F(FuzzTest, RandomBuffersNeverProduceUnsafeResults)
{
    Rng rng;
    for (int iteration = 0; iteration < 1500; ++iteration) {
        const int size = static_cast<int>(rng.below(2048));
        QByteArray content(size, '\0');
        for (int i = 0; i < size; ++i)
            content[i] = static_cast<char>(rng.next() & 0xFF);
        expectSaneResult(analyze(content));
    }
}

TEST_F(FuzzTest, EveryTruncationOfEveryFixtureIsHandled)
{
    const QList<QByteArray> fixtures = {
        testdata::makeElf64(),
        testdata::makePe64(),
        testdata::makeZip({{QStringLiteral("a.txt"), QByteArray("a")}}),
        testdata::makeApk(),
        testdata::makeVitaPkg(),
        testdata::makePs4Pkg(),
    };

    for (const QByteArray& fixture : fixtures) {
        const QList<int> cuts = {0, 1, 2, 3, 4, 8, 16, 24, 32, 48, 52, 64, 100,
                                 fixture.size() / 2, fixture.size() - 1, fixture.size()};
        for (const int cut : cuts) {
            if (cut < 0)
                continue;
            expectSaneResult(analyze(fixture.left(cut)));
        }
    }
}

TEST_F(FuzzTest, RandomPkgHeadersWithHugeCountsAreClamped)
{
    Rng rng;
    for (int iteration = 0; iteration < 300; ++iteration) {
        const bool vita = (rng.next() & 1U) != 0;
        QByteArray header(0x1000, '\0');
        if (vita) {
            testdata::putBE32(header, 0, 0x7F504B47);
            testdata::putBE16(header, 6, 2);
            testdata::putBE32(header, 0x14, rng.next());       // item count
            testdata::putBE64(header, 0x20, rng.next());       // data offset
            testdata::putBE64(header, 0x28, rng.next());       // data size
        } else {
            testdata::putBE32(header, 0, 0x7F434E54);
            testdata::putBE32(header, 0x10, rng.next());       // entry count
            testdata::putBE32(header, 0x18, rng.next());       // table offset
            testdata::putBE64(header, 0x20, rng.next());       // body offset
            testdata::putBE64(header, 0x28, rng.next());       // body size
        }
        const auto result = analyze(header);
        expectSaneResult(result);
    }
}

TEST_F(FuzzTest, ZipDirectoryFuzz)
{
    Rng rng;
    for (int iteration = 0; iteration < 200; ++iteration) {
        const int extra = static_cast<int>(rng.below(512));
        QByteArray content;
        content.append(QByteArray("PK\x03\x04", 4));
        for (int i = 0; i < extra; ++i)
            content.append(static_cast<char>(rng.next() & 0xFF));
        content.append(QByteArray("PK\x05\x06", 4));
        content.append(QByteArray(18, '\0'));
        for (int i = 0; i < 18; ++i)
            content[content.size() - 18 + i] = static_cast<char>(rng.next() & 0xFF);
        expectSaneResult(analyze(content));
    }
}
