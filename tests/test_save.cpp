// Cancellable streaming save and backup.
//
// The UI runs these on a worker thread behind a progress dialog; these tests
// pin the guarantees that make that safe: cancellation leaves the destination
// untouched, the source is never modified, and progress is monotonic.
#include "core/AtomicFile.h"
#include "core/FileSource.h"
#include "core/HexDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace {

QByteArray readWholeFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

void writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(data), static_cast<qint64>(data.size()));
}

std::unique_ptr<pkg::HexDocument> openDocument(const QString& directory, const QString& name,
                                               const QByteArray& content)
{
    const QString path = QDir(directory).filePath(name);
    writeFile(path, content);
    auto source = pkg::FileSource::open(path);
    if (!source)
        return nullptr;
    return std::make_unique<pkg::HexDocument>(std::move(source.value()));
}

}  // namespace

TEST(SaveCancellation, PreCancelledSaveCreatesNothing)
{
    QTemporaryDir dir;
    const QByteArray original(6 * 1024 * 1024, 'A');
    auto document = openDocument(dir.path(), QStringLiteral("source.bin"), original);
    ASSERT_TRUE(document);
    ASSERT_TRUE(document->setByte(0, 'Z'));

    pkg::CancelToken token;
    token.cancel();
    const QString target = dir.filePath(QStringLiteral("out.bin"));
    const auto written = document->saveTo(target, token);

    EXPECT_FALSE(written);
    EXPECT_FALSE(QFileInfo::exists(target));
    EXPECT_EQ(readWholeFile(dir.filePath(QStringLiteral("source.bin"))), original);
    EXPECT_TRUE(document->isModified());
    EXPECT_EQ(document->byteAt(0).value(), static_cast<quint8>('Z'));
}

TEST(SaveCancellation, CancellingMidSaveLeavesTheDestinationUntouched)
{
    QTemporaryDir dir;
    const QByteArray original(9 * 1024 * 1024, 'B');
    auto document = openDocument(dir.path(), QStringLiteral("source.bin"), original);
    ASSERT_TRUE(document);
    ASSERT_TRUE(document->setByte(1024, 'X'));

    pkg::CancelToken token;
    quint64 chunks = 0;
    const QString target = dir.filePath(QStringLiteral("out.bin"));
    const auto written = document->saveTo(
        target, token, [token, &chunks](quint64 done, quint64) mutable {
            if (done > 0) {
                ++chunks;
                token.cancel();
            }
        });

    EXPECT_FALSE(written);
    EXPECT_EQ(chunks, 1U);
    EXPECT_FALSE(QFileInfo::exists(target));
    // No QSaveFile temporary remains behind either.
    const QStringList leftovers =
        QDir(dir.path()).entryList(QStringList{QStringLiteral("out.bin.*")}, QDir::Files);
    EXPECT_TRUE(leftovers.isEmpty()) << leftovers.join(QStringLiteral(", ")).toStdString();
    EXPECT_EQ(readWholeFile(dir.filePath(QStringLiteral("source.bin"))), original);
    EXPECT_TRUE(document->isModified());
}

TEST(SaveProgress, ReportsMonotonicChunkProgressAndCompletes)
{
    QTemporaryDir dir;
    const QByteArray original(10 * 1024 * 1024, 'C');
    auto document = openDocument(dir.path(), QStringLiteral("source.bin"), original);
    ASSERT_TRUE(document);
    ASSERT_TRUE(document->setByte(0, 'Q'));

    std::vector<std::pair<quint64, quint64>> reports;
    const QString target = dir.filePath(QStringLiteral("out.bin"));
    const auto written =
        document->saveTo(target, pkg::CancelToken(),
                         [&reports](quint64 done, quint64 total) {
                             reports.emplace_back(done, total);
                         });
    ASSERT_TRUE(written) << written.error().message.toStdString();
    EXPECT_EQ(written.value(), static_cast<quint64>(original.size()));

    ASSERT_GE(reports.size(), 4U);
    EXPECT_EQ(reports.front().first, 0U);
    EXPECT_EQ(reports.back().first, static_cast<quint64>(original.size()));
    for (const auto& [done, total] : reports)
        EXPECT_EQ(total, static_cast<quint64>(original.size()));
    for (size_t i = 1; i < reports.size(); ++i)
        EXPECT_GE(reports[i].first, reports[i - 1].first);

    QByteArray expected = original;
    expected[0] = 'Q';
    EXPECT_EQ(readWholeFile(target), expected);
}

TEST(SaveCancellation, CancelledBackupLeavesSourceAndDestination)
{
    QTemporaryDir dir;
    const QByteArray original(8 * 1024 * 1024, 'D');
    const QString source = dir.filePath(QStringLiteral("source.bin"));
    writeFile(source, original);
    const QString backup = dir.filePath(QStringLiteral("source.bin.bak"));

    pkg::CancelToken token;
    token.cancel();
    const auto copied = pkg::atomic::backupFile(source, backup, token);

    EXPECT_FALSE(copied);
    EXPECT_FALSE(QFileInfo::exists(backup));
    EXPECT_EQ(readWholeFile(source), original);
}

TEST(SaveProgress, BackupReportsProgressAndCopiesExactBytes)
{
    QTemporaryDir dir;
    QByteArray original(5 * 1024 * 1024, 0);
    for (qsizetype i = 0; i < original.size(); i += 8192)
        original[i] = static_cast<char>(i % 251);
    const QString source = dir.filePath(QStringLiteral("source.bin"));
    writeFile(source, original);
    const QString backup = dir.filePath(QStringLiteral("source.bin.bak"));

    quint64 lastDone = 0;
    quint64 lastTotal = 0;
    const auto copied = pkg::atomic::backupFile(
        source, backup, pkg::CancelToken(),
        [&lastDone, &lastTotal](quint64 done, quint64 total) {
            EXPECT_GE(done, lastDone);
            lastDone = done;
            lastTotal = total;
        });

    ASSERT_TRUE(copied) << copied.error().message.toStdString();
    EXPECT_EQ(copied.value(), static_cast<quint64>(original.size()));
    EXPECT_EQ(lastDone, static_cast<quint64>(original.size()));
    EXPECT_EQ(lastTotal, static_cast<quint64>(original.size()));
    EXPECT_EQ(readWholeFile(backup), original);
}
