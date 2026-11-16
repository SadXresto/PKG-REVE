#include "TestData.h"

#include "core/CancelToken.h"
#include "core/FileSource.h"
#include "core/IFormatParser.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace pkg;

namespace {

Result<PackageInfo> analyzeBytes(const QByteArray& content,
                                const QString& name = QStringLiteral("file.bin"))
{
    QTemporaryDir dir;
    const QString path = testdata::writeTempFile(dir.path(), name, content);
    if (path.isEmpty())
        return makeError(QStringLiteral("fixture write failed"));
    auto source = FileSource::open(path);
    if (!source)
        return source.error();
    CancelToken token;
    // The result holds only plain data, so the mapping may end with this call.
    return ParserRegistry::instance().analyze(source.value(), token);
}

QString fieldValue(const PackageInfo& info, const QString& name)
{
    for (const InfoSection& section : info.sections) {
        for (const InfoField& field : section.fields) {
            if (field.name == name)
                return field.value;
        }
    }
    return {};
}

}  // namespace

class ParserTest : public ::testing::Test {
protected:
    void SetUp() override { ParserRegistry::registerBuiltins(); }
};

TEST_F(ParserTest, DetectsElfHeaderAndInterpreter)
{
    auto analyzed = analyzeBytes(testdata::makeElf64(), QStringLiteral("sample.elf"));
    ASSERT_TRUE(analyzed);
    const PackageInfo& info = analyzed.value();
    EXPECT_EQ(info.kind, FormatKind::Elf);
    EXPECT_EQ(info.parserId, QStringLiteral("builtin.elf"));
    EXPECT_NE(fieldValue(info, QStringLiteral("Machine")).indexOf(QStringLiteral("AMD x86-64")),
              -1);
    EXPECT_NE(fieldValue(info, QStringLiteral("Entry point")), QString());

    bool foundInterpreter = false;
    for (const InfoSection& section : info.sections) {
        if (section.title == QLatin1String("Interpreter")) {
            foundInterpreter = true;
            EXPECT_EQ(section.fields.first().value,
                      QStringLiteral("/lib64/ld-linux-x86-64.so.2"));
        }
    }
    EXPECT_TRUE(foundInterpreter);
    EXPECT_TRUE(info.warnings.isEmpty());
}

TEST_F(ParserTest, ReportsTruncatedElfWithoutCrashing)
{
    auto analyzed = analyzeBytes(testdata::makeElf64().left(4), QStringLiteral("short.elf"));
    ASSERT_TRUE(analyzed);
    EXPECT_EQ(analyzed.value().kind, FormatKind::Elf);
    EXPECT_TRUE(analyzed.value().partial);
    EXPECT_FALSE(analyzed.value().warnings.isEmpty());
}

TEST_F(ParserTest, RejectsElfWithInvalidClass)
{
    QByteArray elf = testdata::makeElf64();
    elf[4] = 7;
    auto analyzed = analyzeBytes(elf, QStringLiteral("bad.elf"));
    EXPECT_FALSE(analyzed);
}

TEST_F(ParserTest, DetectsPe32PlusAndSections)
{
    auto analyzed = analyzeBytes(testdata::makePe64(), QStringLiteral("sample.exe"));
    ASSERT_TRUE(analyzed);
    const PackageInfo& info = analyzed.value();
    EXPECT_EQ(info.kind, FormatKind::Pe);
    EXPECT_NE(info.formatName.indexOf(QStringLiteral("PE32+")), -1);
    EXPECT_EQ(fieldValue(info, QStringLiteral("Machine")).left(3), QStringLiteral("x64"));
    EXPECT_EQ(fieldValue(info, QStringLiteral("Sections")), QStringLiteral("2"));

    bool foundTextSection = false;
    for (const InfoSection& section : info.sections) {
        for (const InfoField& field : section.fields) {
            if (field.name == QLatin1String(".text")) {
                foundTextSection = true;
                EXPECT_NE(field.value.indexOf(QStringLiteral("VA 0x00001000")), -1);
            }
        }
    }
    EXPECT_TRUE(foundTextSection);
}

TEST_F(ParserTest, MalformedPeFallsBackToRawBytes)
{
    QByteArray pe = testdata::makePe64();
    testdata::putLE32(pe, 0x3C, 0x7FFFFFF0U);  // e_lfanew far outside the file
    auto analyzed = analyzeBytes(pe, QStringLiteral("broken.exe"));
    ASSERT_TRUE(analyzed);
    EXPECT_EQ(analyzed.value().kind, FormatKind::RawBinary);
}

TEST_F(ParserTest, DetectsZipAndListsEntries)
{
    const QByteArray zip = testdata::makeZip({
        {QStringLiteral("readme.txt"), QByteArray("hello")},
        {QStringLiteral("assets/logo.bin"), QByteArray(32, 'x')},
    });
    auto analyzed = analyzeBytes(zip, QStringLiteral("archive.zip"));
    ASSERT_TRUE(analyzed);
    const PackageInfo& info = analyzed.value();
    EXPECT_EQ(info.kind, FormatKind::Zip);
    EXPECT_NE(info.summary.indexOf(QStringLiteral("2 entries")), -1);
    EXPECT_NE(fieldValue(info, QStringLiteral("readme.txt")), QString());
}

TEST_F(ParserTest, IdentifiesApkByManifest)
{
    auto analyzed = analyzeBytes(testdata::makeApk(), QStringLiteral("sample.apk"));
    ASSERT_TRUE(analyzed);
    EXPECT_EQ(analyzed.value().kind, FormatKind::Apk);
    EXPECT_NE(fieldValue(analyzed.value(), QStringLiteral("AndroidManifest.xml")), QString());
}

TEST_F(ParserTest, TruncatedZipReportsErrorInsteadOfGarbage)
{
    QByteArray apk = testdata::makeApk();
    apk.chop(12);  // remove the end of central directory record
    auto analyzed = analyzeBytes(apk, QStringLiteral("truncated.apk"));
    EXPECT_FALSE(analyzed);
}

TEST_F(ParserTest, UnrecognizedDataIsReportedHonestly)
{
    auto analyzed = analyzeBytes(QByteArray(1024, '\x42'), QStringLiteral("unknown.bin"));
    ASSERT_TRUE(analyzed);
    EXPECT_EQ(analyzed.value().kind, FormatKind::RawBinary);
    EXPECT_FALSE(analyzed.value().warnings.isEmpty());
}
