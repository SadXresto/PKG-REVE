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

QByteArray readWholeFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

}  // namespace

TEST(HexEditing, EditsAreMergedIntoReads)
{
    QTemporaryDir dir;
    auto document = openDocument(dir, QStringLiteral("edit.bin"), QByteArray("ABCDEFGH"));
    ASSERT_TRUE(document);
    ASSERT_EQ(document->size(), 8U);

    EXPECT_FALSE(document->isModified());
    EXPECT_TRUE(document->setByte(2, 'x'));
    EXPECT_TRUE(document->isModified());
    EXPECT_EQ(document->byteAt(2).value(), static_cast<quint8>('x'));
    EXPECT_TRUE(document->hasOverride(2));
    EXPECT_EQ(document->modifiedByteCount(), 1);

    HexLayout layout;
    layout.bytesPerRow = 8;
    const auto row = document->rowBytes(0, layout);
    ASSERT_TRUE(row);
    EXPECT_EQ(row.value(), QByteArray("ABxDEFGH"));

    const auto merged = document->readMerged(1, 3);
    ASSERT_TRUE(merged);
    EXPECT_EQ(merged.value(), QByteArray("BxD"));

    EXPECT_TRUE(document->canUndo());
    EXPECT_FALSE(document->canRedo());
}

TEST(HexEditing, UndoRedoRestoresBytesAndTracksModification)
{
    QTemporaryDir dir;
    auto document = openDocument(dir, QStringLiteral("undo.bin"), QByteArray("ABCDEFGH"));
    ASSERT_TRUE(document);

    ASSERT_TRUE(document->setByte(0, '1'));
    ASSERT_TRUE(document->setByte(7, '9'));
    EXPECT_EQ(document->modifiedByteCount(), 2);

    EXPECT_TRUE(document->undo());
    EXPECT_EQ(document->byteAt(7).value(), static_cast<quint8>('H'));
    EXPECT_EQ(document->modifiedByteCount(), 1);
    EXPECT_TRUE(document->canRedo());

    EXPECT_TRUE(document->undo());
    EXPECT_EQ(document->byteAt(0).value(), static_cast<quint8>('A'));
    EXPECT_FALSE(document->isModified());
    EXPECT_EQ(document->modifiedByteCount(), 0);
    EXPECT_FALSE(document->undo());

    EXPECT_TRUE(document->redo());
    EXPECT_EQ(document->byteAt(0).value(), static_cast<quint8>('1'));
    EXPECT_TRUE(document->redo());
    EXPECT_EQ(document->byteAt(7).value(), static_cast<quint8>('9'));
    EXPECT_FALSE(document->redo());
}

TEST(HexEditing, NewEditAfterUndoDiscardsRedoBranch)
{
    QTemporaryDir dir;
    auto document = openDocument(dir, QStringLiteral("branch.bin"), QByteArray("ABCDEFGH"));
    ASSERT_TRUE(document);

    ASSERT_TRUE(document->setByte(1, 'x'));
    ASSERT_TRUE(document->undo());
    EXPECT_TRUE(document->canRedo());

    ASSERT_TRUE(document->setByte(1, 'y'));
    EXPECT_FALSE(document->canRedo());
    EXPECT_EQ(document->byteAt(1).value(), static_cast<quint8>('y'));
    EXPECT_TRUE(document->undo());
    EXPECT_EQ(document->byteAt(1).value(), static_cast<quint8>('B'));
}

TEST(HexEditing, NoOpAndOutOfRangeEditsAreRejected)
{
    QTemporaryDir dir;
    auto document = openDocument(dir, QStringLiteral("noop.bin"), QByteArray("ABCD"));
    ASSERT_TRUE(document);

    EXPECT_FALSE(document->setByte(0, 'A'));
    EXPECT_FALSE(document->isModified());
    EXPECT_FALSE(document->setByte(4, 'Z'));
    EXPECT_FALSE(document->canUndo());
    EXPECT_FALSE(document->readMerged(3, 2));
    EXPECT_FALSE(document->readMerged(2, 10));
}

TEST(HexEditing, SaveWritesEditsAndPreservesEverythingElse)
{
    QTemporaryDir dir;
    const QByteArray original("0123456789");
    auto document = openDocument(dir, QStringLiteral("save.bin"), original);
    ASSERT_TRUE(document);

    ASSERT_TRUE(document->setByte(0, 'X'));
    ASSERT_TRUE(document->setByte(9, 'Y'));
    const QString target = dir.filePath(QStringLiteral("saved.bin"));
    const auto written = document->saveTo(target);
    ASSERT_TRUE(written) << written.error().message.toStdString();
    EXPECT_EQ(written.value(), 10U);

    EXPECT_EQ(readWholeFile(target), QByteArray("X12345678Y"));
    EXPECT_EQ(readWholeFile(dir.filePath(QStringLiteral("save.bin"))), original);

    const auto reloaded = document->reloadFrom(target);
    ASSERT_TRUE(reloaded) << reloaded.error().message.toStdString();
    EXPECT_FALSE(document->isModified());
    EXPECT_FALSE(document->canUndo());
    EXPECT_EQ(document->byteAt(0).value(), static_cast<quint8>('X'));
    EXPECT_EQ(document->byteAt(9).value(), static_cast<quint8>('Y'));
    EXPECT_EQ(document->path(), target);
}

TEST(HexEditing, SaveStreamsAcrossChunkBoundaries)
{
    QTemporaryDir dir;
    QByteArray content(5 * 1024 * 1024, '\0');
    for (qsizetype i = 0; i < content.size(); i += 4096)
        content[i] = static_cast<char>(i % 251);
    constexpr quint64 kChunk = 4U * 1024U * 1024U;

    auto document = openDocument(dir, QStringLiteral("large.bin"), content);
    ASSERT_TRUE(document);
    ASSERT_TRUE(document->setByte(kChunk - 1, 0xAA));
    ASSERT_TRUE(document->setByte(kChunk, 0xBB));

    const QString target = dir.filePath(QStringLiteral("large-saved.bin"));
    const auto written = document->saveTo(target);
    ASSERT_TRUE(written);
    EXPECT_EQ(written.value(), static_cast<quint64>(content.size()));

    const QByteArray saved = readWholeFile(target);
    ASSERT_EQ(saved.size(), content.size());
    EXPECT_EQ(static_cast<quint8>(saved.at(static_cast<qsizetype>(kChunk - 1))), 0xAA);
    EXPECT_EQ(static_cast<quint8>(saved.at(static_cast<qsizetype>(kChunk))), 0xBB);
    EXPECT_TRUE(saved == content.left(static_cast<qsizetype>(kChunk - 1)) +
                            QByteArray(1, static_cast<char>(0xAA)) +
                            QByteArray(1, static_cast<char>(0xBB)) +
                            content.mid(static_cast<qsizetype>(kChunk + 1)));
}

TEST(HexEditing, InvalidDestinationsFailWithoutTouchingAnything)
{
    QTemporaryDir dir;
    auto document = openDocument(dir, QStringLiteral("dest.bin"), QByteArray("DATA"));
    ASSERT_TRUE(document);

    EXPECT_FALSE(document->saveTo(QString()));
    EXPECT_FALSE(document->reloadFrom(dir.filePath(QStringLiteral("missing.bin"))));
    EXPECT_TRUE(document->isValid());
    EXPECT_EQ(document->size(), 4U);
}
