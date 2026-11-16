#include "TestData.h"

#include "core/CancelToken.h"
#include "core/FileSource.h"
#include "core/FormatInfo.h"
#include "core/IFormatParser.h"
#include "core/SignatureScanner.h"

#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace pkg;

namespace {

QVector<SignatureHit> scanBytes(const QTemporaryDir& dir, const QByteArray& content)
{
    const QString path = testdata::writeTempFile(dir.path(), QStringLiteral("sig.bin"), content);
    if (path.isEmpty())
        return {};
    auto source = FileSource::open(path);
    if (!source)
        return {};
    return SignatureScanner::scan(source.value());
}

const SignatureHit* findHit(const QVector<SignatureHit>& hits, const QString& namePart)
{
    for (const SignatureHit& hit : hits) {
        if (hit.name.contains(namePart, Qt::CaseInsensitive))
            return &hit;
    }
    return nullptr;
}

}  // namespace

TEST(SignatureScanner, ValidatesElfIdentification)
{
    QTemporaryDir dir;
    const QVector<SignatureHit> hits = scanBytes(dir, testdata::makeElf64());
    const SignatureHit* elf = findHit(hits, QStringLiteral("ELF"));
    ASSERT_NE(elf, nullptr);
    EXPECT_EQ(elf->offset, 0U);
    EXPECT_GE(elf->confidence, 95);
    EXPECT_NE(elf->note.indexOf(QStringLiteral("validated")), -1);
}

TEST(SignatureScanner, DistinguishesValidatedZipFromMagicOnly)
{
    QTemporaryDir dir;
    const QByteArray zip = testdata::makeZip({
        {QStringLiteral("a.txt"), QByteArray("payload")},
    });
    const QVector<SignatureHit> hits = scanBytes(dir, zip);
    const SignatureHit* hit = findHit(hits, QStringLiteral("ZIP"));
    ASSERT_NE(hit, nullptr);
    EXPECT_GE(hit->confidence, 95);
    EXPECT_NE(hit->note.indexOf(QStringLiteral("end-of-central-directory")), -1);
}

TEST(SignatureScanner, RecognizesConsoleAndImageSignatures)
{
    QTemporaryDir dir;
    auto ps4 = scanBytes(dir, testdata::makePs4Pkg());
    ASSERT_NE(findHit(ps4, QStringLiteral("PS4 PKG")), nullptr);
    EXPECT_EQ(findHit(ps4, QStringLiteral("PS4 PKG"))->confidence, 99);

    QTemporaryDir vitaDir;
    auto vita = scanBytes(vitaDir, testdata::makeVitaPkg());
    ASSERT_NE(findHit(vita, QStringLiteral("PS Vita")), nullptr);

    QTemporaryDir pngDir;
    QByteArray png("\x89PNG\r\n\x1a\n", 8);
    png.append(QByteArray(64, '\0'));
    auto pngHits = scanBytes(pngDir, png);
    const SignatureHit* pngHit = findHit(pngHits, QStringLiteral("PNG"));
    ASSERT_NE(pngHit, nullptr);
    EXPECT_EQ(pngHit->confidence, 99);
}

TEST(SignatureScanner, ReportsEmbeddedMatchesWithLowerConfidence)
{
    QTemporaryDir dir;
    const QByteArray zip = testdata::makeZip({{QStringLiteral("inner.txt"), QByteArray("x")}});
    QByteArray container(512, '\x11');
    container.append(zip);
    container.append(QByteArray(64, '\0'));

    const QVector<SignatureHit> hits = scanBytes(dir, container);
    const SignatureHit* embedded = findHit(hits, QStringLiteral("ZIP"));
    ASSERT_NE(embedded, nullptr);
    EXPECT_EQ(embedded->offset, 512U);
    EXPECT_LT(embedded->confidence, 85);
    EXPECT_NE(embedded->note.indexOf(QStringLiteral("embedded")), -1);
}

TEST(SignatureScanner, DoesNotInventSignatures)
{
    QTemporaryDir dir;
    EXPECT_TRUE(scanBytes(dir, QByteArray(4096, '\0')).isEmpty());
    EXPECT_TRUE(scanBytes(dir, QByteArray("plain text without a documented header")).isEmpty());
}

TEST(SignatureScanner, MzStubAloneStaysLowConfidence)
{
    QTemporaryDir dir;
    QByteArray stub("MZ", 2);
    stub.append(QByteArray(62, '\0'));
    const QVector<SignatureHit> hits = scanBytes(dir, stub);
    const SignatureHit* pe = findHit(hits, QStringLiteral("PE"));
    ASSERT_NE(pe, nullptr);
    EXPECT_LE(pe->confidence, 45);
}

TEST(SignatureScanner, AnalysisCarriesSignaturesAndSection)
{
    QTemporaryDir dir;
    const QString path =
        testdata::writeTempFile(dir.path(), QStringLiteral("sample.elf"), testdata::makeElf64());
    auto source = FileSource::open(path);
    ASSERT_TRUE(source);
    ParserRegistry::registerBuiltins();
    CancelToken token;
    const auto info = ParserRegistry::instance().analyze(source.value(), token);
    ASSERT_TRUE(info);
    ASSERT_FALSE(info.value().signatures.isEmpty());

    bool hasSection = false;
    for (const InfoSection& section : info.value().sections) {
        if (section.title == QLatin1String("Detected signatures"))
            hasSection = true;
    }
    EXPECT_TRUE(hasSection);

    const auto restored = PackageInfo::fromJson(info.value().toJson());
    ASSERT_TRUE(restored);
    ASSERT_EQ(restored.value().signatures.size(), info.value().signatures.size());
    EXPECT_EQ(restored.value().signatures.first().name, info.value().signatures.first().name);
    EXPECT_EQ(restored.value().signatures.first().offset,
              info.value().signatures.first().offset);
}
