#include "ai/AiClient.h"
#include "core/CodeListing.h"
#include "core/HexDocument.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>

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

std::shared_ptr<HexDocument> openDocument(const QTemporaryDir& dir, const QString& name,
                                          const QByteArray& content)
{
    const QString path = writeFile(dir, name, content);
    if (path.isEmpty())
        return {};
    auto source = FileSource::open(path);
    if (!source)
        return {};
    return std::make_shared<HexDocument>(std::move(source.value()));
}

}  // namespace

TEST(CodeListing, BuildsOffsetHexAndAsciiColumns)
{
    QTemporaryDir dir;
    auto document = openDocument(dir, QStringLiteral("listing.bin"), QByteArray("Hello, world!"));
    ASSERT_TRUE(document);

    auto listing = buildListing(*document, 0, document->size(), 8);
    ASSERT_TRUE(listing) << listing.error().message.toUtf8().constData();
    const QString text = listing.value();
    EXPECT_TRUE(text.contains(QStringLiteral("00000000  48 65 6C 6C 6F 2C 20 77")));
    EXPECT_TRUE(text.contains(QStringLiteral("|Hello, w|")));
    EXPECT_TRUE(text.contains(QStringLiteral("00000008  6F 72 6C 64 21")));
}

TEST(CodeListing, ListingRoundTripsThroughParseAndApply)
{
    QTemporaryDir dir;
    const QByteArray original("PKGStudio-listing-round-trip");
    auto source = openDocument(dir, QStringLiteral("source.bin"), original);
    auto target = openDocument(dir, QStringLiteral("target.bin"), QByteArray(original.size(), '\0'));
    ASSERT_TRUE(source);
    ASSERT_TRUE(target);

    auto listing = buildListing(*source, 0, source->size(), 16);
    ASSERT_TRUE(listing);
    auto parsed = parseListing(listing.value(), target->size());
    ASSERT_TRUE(parsed) << parsed.error().message.toUtf8().constData();
    EXPECT_EQ(listingByteCount(parsed.value()), static_cast<quint64>(original.size()));

    auto applied = applyListing(*target, parsed.value());
    ASSERT_TRUE(applied) << applied.error().message.toUtf8().constData();
    EXPECT_EQ(applied.value(), original.size());
    auto whole = target->readMerged(0, target->size());
    ASSERT_TRUE(whole);
    EXPECT_EQ(whole.value(), original);
}

TEST(CodeListing, ParsesColonAndOxOffsets)
{
    const QString text = QStringLiteral(
        "# a comment line\n"
        "00000004: DE AD BE EF\n"
        "0x00000008  01 02\n"
        "0000000A  03\n");
    auto parsed = parseListing(text);
    ASSERT_TRUE(parsed) << parsed.error().message.toUtf8().constData();
    // The three lines are contiguous, so they merge into one run.
    ASSERT_EQ(parsed.value().size(), 1);
    EXPECT_EQ(parsed.value().first().offset, 4U);
    EXPECT_EQ(parsed.value().first().bytes,
              QByteArray::fromHex("DEADBEEF010203"));
}

TEST(CodeListing, RejectsGarbageAndOutOfBounds)
{
    EXPECT_FALSE(parseListing(QStringLiteral("this is not a listing")));
    EXPECT_FALSE(parseListing(QStringLiteral("00000000  90 90"), 1));
}

TEST(CodeListing, AppliedEditsAreUndoable)
{
    QTemporaryDir dir;
    auto document = openDocument(dir, QStringLiteral("undo.bin"), QByteArray("ABCDEFGH"));
    ASSERT_TRUE(document);

    auto parsed = parseListing(QStringLiteral("00000002  78 78"));
    ASSERT_TRUE(parsed);
    auto applied = applyListing(*document, parsed.value());
    ASSERT_TRUE(applied);
    EXPECT_EQ(applied.value(), 2);
    EXPECT_TRUE(document->isModified());

    EXPECT_TRUE(document->undo());
    EXPECT_TRUE(document->undo());
    EXPECT_FALSE(document->isModified());
    auto whole = document->readMerged(0, document->size());
    ASSERT_TRUE(whole);
    EXPECT_EQ(whole.value(), QByteArray("ABCDEFGH"));
}

TEST(CodeListing, ExtractsEditsFromAiReply)
{
    const QString reply = QStringLiteral(
        "The version field lives at 0x10. Change it to 2:\n"
        "```pkgstudio-edit\n"
        "00000010  02 00 00 00\n"
        "```\n"
        "That is all.");
    const QVector<ListingEntry> edits = extractEdits(reply);
    ASSERT_EQ(edits.size(), 1);
    EXPECT_EQ(edits.first().offset, 0x10U);
    EXPECT_EQ(edits.first().bytes, QByteArray::fromHex("02000000"));

    EXPECT_TRUE(extractEdits(QStringLiteral("No patch here.")).isEmpty());
}
