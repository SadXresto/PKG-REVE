// Widget-level checks for the editing path: key events go through the real
// HexView, not a test hook. Runs headless with QT_QPA_PLATFORM=offscreen.
#include "core/HexDocument.h"
#include "ui/HexView.h"

#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>

using namespace pkg;

namespace {

std::shared_ptr<HexDocument> openDocument(const QTemporaryDir& dir, const QByteArray& content)
{
    const QString path = dir.filePath(QStringLiteral("ui.bin"));
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

void typeKey(ui::HexView& view, Qt::Key key, const QString& text)
{
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, text);
    QApplication::sendEvent(&view, &event);
}

}  // namespace

TEST(HexViewEditing, TypingHexDigitsEditsBytesAndAdvances)
{
    QTemporaryDir dir;
    auto document = openDocument(dir, QByteArray("ABCD"));
    ASSERT_TRUE(document);

    ui::HexView view;
    view.setDocument(document);

    typeKey(view, Qt::Key_A, QStringLiteral("a"));
    EXPECT_FALSE(document->isModified()) << "editing must be opt-in";

    view.setEditingEnabled(true);
    typeKey(view, Qt::Key_A, QStringLiteral("A"));
    typeKey(view, Qt::Key_5, QStringLiteral("5"));
    ASSERT_TRUE(document->isModified());
    EXPECT_EQ(document->byteAt(0).value(), 0xA5);
    EXPECT_EQ(view.cursorOffset(), 1U);

    typeKey(view, Qt::Key_9, QStringLiteral("9"));
    typeKey(view, Qt::Key_9, QStringLiteral("9"));
    EXPECT_EQ(document->byteAt(1).value(), 0x99);
    EXPECT_EQ(view.cursorOffset(), 2U);

    EXPECT_TRUE(document->undo());
    EXPECT_EQ(document->byteAt(1).value(), static_cast<quint8>('B'));

    view.setEditingEnabled(false);
    typeKey(view, Qt::Key_F, QStringLiteral("f"));
    typeKey(view, Qt::Key_F, QStringLiteral("f"));
    EXPECT_EQ(document->byteAt(1).value(), static_cast<quint8>('B'));
}
