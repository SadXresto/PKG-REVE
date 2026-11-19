#include "ui/DialogCapture.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QSet>
#include <QThread>
#include <QWidget>

#include <algorithm>

namespace pkg::ui {
namespace {

bool hasVisibleContent(const QImage& image)
{
    if (image.isNull() || image.width() < 80 || image.height() < 40)
        return false;
    QSet<QRgb> colors;
    const int stepX = std::max(1, image.width() / 120);
    const int stepY = std::max(1, image.height() / 120);
    for (int y = 0; y < image.height(); y += stepY) {
        for (int x = 0; x < image.width(); x += stepX)
            colors.insert(image.pixel(x, y));
    }
    // A blank grab (or a single flat colour) means the dialog was never laid
    // out; real dialogs have a background, text and at least one button.
    return colors.size() >= 8;
}

}  // namespace

bool captureDialog(QWidget* dialog, const QString& path, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error != nullptr)
            *error = message;
        return false;
    };
    if (dialog == nullptr)
        return fail(QStringLiteral("no dialog to capture"));
    if (path.isEmpty())
        return fail(QStringLiteral("no output path"));

    dialog->show();
    dialog->raise();

    // Wait until the size settles; --screenshot-dialog also runs on CI, where a
    // window manager may adjust the dialog after the first layout pass.
    QElapsedTimer timer;
    timer.start();
    QSize previous;
    while (timer.elapsed() < 3000) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        const QSize size = dialog->size();
        if (dialog->isVisible() && size == previous && size.width() > 80
            && size.height() > 40) {
            break;
        }
        previous = size;
        QThread::msleep(25);
    }
    if (!dialog->isVisible())
        return fail(QStringLiteral("dialog did not become visible"));

    const QPixmap shot = dialog->grab();
    const QImage image = shot.toImage();
    if (!hasVisibleContent(image)) {
        return fail(QStringLiteral("dialog rendered blank (%1x%2)")
                        .arg(image.width())
                        .arg(image.height()));
    }

    const QFileInfo target(path);
    if (!target.absoluteDir().exists() && !QDir().mkpath(target.absolutePath()))
        return fail(QStringLiteral("cannot create output folder: %1").arg(target.absolutePath()));
    if (!shot.save(path, "PNG"))
        return fail(QStringLiteral("cannot save screenshot: %1").arg(path));
    return true;
}

}  // namespace pkg::ui
