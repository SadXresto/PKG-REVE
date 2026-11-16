#include "TestData.h"

#include "core/CancelToken.h"
#include "core/FileSource.h"
#include "core/IFormatParser.h"

#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace pkg;

namespace {

Result<PackageInfo> analyzeBytes(const QByteArray& content, const QString& name)
{
    QTemporaryDir dir;
    const QString path = testdata::writeTempFile(dir.path(), name, content);
    if (path.isEmpty())
        return makeError(QStringLiteral("fixture write failed"));
    auto source = FileSource::open(path);
    if (!source)
        return source.error();
    CancelToken token;
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

class PkgParserTest : public ::testing::Test {
protected:
    void SetUp() override { ParserRegistry::registerBuiltins(); }
};

TEST_F(PkgParserTest, ReadsVitaPkgHeader)
{
    auto analyzed = analyzeBytes(testdata::makeVitaPkg(), QStringLiteral("game.pkg"));
    ASSERT_TRUE(analyzed);
    const PackageInfo& info = analyzed.value();
    EXPECT_EQ(info.kind, FormatKind::VitaPkg);
    EXPECT_EQ(fieldValue(info, QStringLiteral("Content ID")),
              QStringLiteral("UP0000-PCSG00001_00-0000000000000001"));
    EXPECT_EQ(fieldValue(info, QStringLiteral("File items")), QStringLiteral("3"));
    EXPECT_NE(info.summary.indexOf(QStringLiteral("PSP / PS Vita")), -1);

    bool mentionsEncryption = false;
    for (const QString& warning : info.warnings) {
        if (warning.contains(QStringLiteral("encrypted")))
            mentionsEncryption = true;
    }
    EXPECT_TRUE(mentionsEncryption);
}

TEST_F(PkgParserTest, RejectsTruncatedVitaHeader)
{
    auto analyzed = analyzeBytes(testdata::makeVitaPkg().left(0x20),
                                 QStringLiteral("cut.pkg"));
    EXPECT_FALSE(analyzed);
}

TEST_F(PkgParserTest, ReadsPs4PkgHeaderAndWarnsAboutEncryption)
{
    auto analyzed = analyzeBytes(testdata::makePs4Pkg(), QStringLiteral("game.pkg"));
    ASSERT_TRUE(analyzed);
    const PackageInfo& info = analyzed.value();
    EXPECT_EQ(info.kind, FormatKind::Ps4Pkg);
    EXPECT_EQ(fieldValue(info, QStringLiteral("Content ID")),
              QStringLiteral("UP0000-CUSA00001_00-0000000000000001"));
    EXPECT_EQ(fieldValue(info, QStringLiteral("Entry count")), QStringLiteral("5"));
    EXPECT_EQ(fieldValue(info, QStringLiteral("Image count")), QStringLiteral("1"));

    bool mentionsEncryption = false;
    for (const QString& warning : info.warnings) {
        if (warning.contains(QStringLiteral("encrypted")))
            mentionsEncryption = true;
    }
    EXPECT_TRUE(mentionsEncryption);
}

TEST_F(PkgParserTest, Ps4SizeMismatchIsReported)
{
    QByteArray pkg = testdata::makePs4Pkg();
    testdata::putBE64(pkg, 0x430, 0xFFFFFF);  // declared size larger than the file
    auto analyzed = analyzeBytes(pkg, QStringLiteral("badsize.pkg"));
    ASSERT_TRUE(analyzed);
    EXPECT_TRUE(analyzed.value().partial);
    bool mentionsSize = false;
    for (const QString& warning : analyzed.value().warnings) {
        if (warning.contains(QStringLiteral("Declared package size")))
            mentionsSize = true;
    }
    EXPECT_TRUE(mentionsSize);
}

TEST_F(PkgParserTest, ImplausibleEntryCountIsRejectedSafely)
{
    QByteArray pkg = testdata::makePs4Pkg();
    testdata::putBE32(pkg, 0x10, 0xFFFFFFF0U);
    auto analyzed = analyzeBytes(pkg, QStringLiteral("huge.pkg"));
    ASSERT_TRUE(analyzed);
    EXPECT_TRUE(analyzed.value().partial);
    EXPECT_FALSE(analyzed.value().warnings.isEmpty());
}

TEST_F(PkgParserTest, Ps4TruncatedHeaderIsRejected)
{
    auto analyzed = analyzeBytes(testdata::makePs4Pkg().left(0x40), QStringLiteral("cut.pkg"));
    EXPECT_FALSE(analyzed);
}
