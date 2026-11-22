#include "ui/HexView.h"

#include "core/FormatUtil.h"
#include "data/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStringList>

#include <algorithm>
#include <limits>

namespace pkg::ui {
namespace {

constexpr quint64 kWindowBytes = 256U * 1024U;
constexpr int kRowPadding = 4;
constexpr int kLeftMargin = 8;

char printableChar(quint8 value)
{
    return (value >= 0x20U && value < 0x7FU) ? static_cast<char>(value) : '.';
}

int hexNibble(QChar ch)
{
    const ushort code = ch.unicode();
    if (code >= '0' && code <= '9')
        return code - '0';
    if (code >= 'a' && code <= 'f')
        return code - 'a' + 10;
    if (code >= 'A' && code <= 'F')
        return code - 'A' + 10;
    return -1;
}

}  // namespace

HexView::HexView(QWidget* parent)
    : QAbstractScrollArea(parent), m_fontMetrics(m_font)
{
    setFocusPolicy(Qt::StrongFocus);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent, true);
    viewport()->setMouseTracking(true);

    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_font = fixed;
    m_font.setPointSize(10);
    m_fontMetrics = QFontMetrics(m_font);
    recomputeMetrics();

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (m_updatingScrollBars)
            return;
        m_topRow = static_cast<quint64>(std::max(0, value));
        viewport()->update();
    });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (m_updatingScrollBars)
            return;
        m_xOffset = value;
        viewport()->update();
    });
}

HexView::~HexView() = default;

void HexView::setDocument(std::shared_ptr<HexDocument> document)
{
    m_document = std::move(document);
    m_topRow = 0;
    m_xOffset = 0;
    m_cursor = 0;
    m_anchor = 0;
    m_windowValid = false;
    m_window.clear();
    m_searchHits.clear();
    m_bookmarks.clear();
    m_currentHit = -1;
    m_signatures.clear();
    m_pendingHighNibble = -1;
    m_cursorInAscii = false;
    recomputeMetrics();
    updateScrollBars();
    viewport()->update();
    if (m_document && m_document->isValid()) {
        emit cursorMoved(0);
        updateSelectionState();
    }
}

void HexView::closeDocument()
{
    m_document.reset();
    m_searchHits.clear();
    m_window.clear();
    m_windowValid = false;
    updateScrollBars();
    viewport()->update();
}

void HexView::setLayout(const HexLayout& layout)
{
    m_layout = layout.normalized();
    m_windowValid = false;
    recomputeMetrics();
    updateScrollBars();
    viewport()->update();
}

void HexView::setEditorFont(const QFont& font)
{
    m_font = font;
    m_fontMetrics = QFontMetrics(m_font);
    m_windowValid = false;
    recomputeMetrics();
    updateScrollBars();
    viewport()->update();
}

void HexView::setEditingEnabled(bool enabled)
{
    if (m_editing == enabled)
        return;
    m_editing = enabled;
    m_pendingHighNibble = -1;
    if (!enabled)
        m_cursorInAscii = false;
    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->update();
}

void HexView::notifyDocumentChanged()
{
    m_windowValid = false;
    m_window.clear();
    viewport()->update();
}

void HexView::recomputeMetrics()
{
    m_fontMetrics = QFontMetrics(m_font);
    m_metrics.charWidth = std::max(6, m_fontMetrics.horizontalAdvance(QLatin1Char('0')));
    m_metrics.rowHeight = std::max(12, m_fontMetrics.height() + kRowPadding);

    const quint64 size = m_document ? m_document->size() : 0;
    quint64 probe = size > 0 ? size - 1 : 0;
    int digits = 1;
    while (probe >= 16) {
        probe >>= 4;
        ++digits;
    }
    m_metrics.offsetDigits = std::max(8, digits);
    m_metrics.gutterWidth = kLeftMargin + m_metrics.offsetDigits * m_metrics.charWidth + 12;
    m_metrics.hexStart = m_metrics.gutterWidth + 8;

    const int bpr = m_layout.normalized().bytesPerRow;
    const int groupSize = m_layout.normalized().groupSize;
    const int lastColumn = std::max(0, bpr - 1);
    const int lastColumnX = m_metrics.hexStart + lastColumn * 3 * m_metrics.charWidth +
                            (lastColumn / groupSize) * m_metrics.charWidth;
    m_metrics.asciiStart = lastColumnX + 3 * m_metrics.charWidth + 2 * m_metrics.charWidth;
    m_metrics.contentWidth = m_metrics.asciiStart + bpr * m_metrics.charWidth + 8;
}

int HexView::visibleRowCount() const
{
    const int height = viewport()->height();
    if (height <= 0)
        return 1;
    return std::max(1, height / m_metrics.rowHeight);
}

quint64 HexView::rowCount() const
{
    if (!m_document || !m_document->isValid())
        return 0;
    return m_document->rowCount(m_layout);
}

int HexView::columnX(int column) const
{
    const int groupSize = m_layout.normalized().groupSize;
    return m_metrics.hexStart + column * 3 * m_metrics.charWidth +
           (column / groupSize) * m_metrics.charWidth;
}

void HexView::updateScrollBars()
{
    m_updatingScrollBars = true;

    const quint64 rows = rowCount();
    const int visible = visibleRowCount();
    const quint64 maxTop = rows > static_cast<quint64>(visible)
                               ? rows - static_cast<quint64>(visible)
                               : 0;
    const int cappedTop = static_cast<int>(
        std::min<quint64>(maxTop, static_cast<quint64>(std::numeric_limits<int>::max())));

    verticalScrollBar()->setRange(0, cappedTop);
    verticalScrollBar()->setPageStep(visible);
    verticalScrollBar()->setSingleStep(1);
    if (m_topRow > static_cast<quint64>(cappedTop))
        m_topRow = static_cast<quint64>(cappedTop);
    verticalScrollBar()->setValue(static_cast<int>(m_topRow));

    const int overflow = std::max(0, m_metrics.contentWidth - viewport()->width());
    horizontalScrollBar()->setRange(0, overflow);
    horizontalScrollBar()->setPageStep(std::max(1, viewport()->width()));
    horizontalScrollBar()->setSingleStep(m_metrics.charWidth * 3);
    m_xOffset = std::clamp(m_xOffset, 0, overflow);
    horizontalScrollBar()->setValue(m_xOffset);

    m_updatingScrollBars = false;
}

void HexView::ensureWindow(quint64 neededOffset)
{
    if (!m_document || !m_document->isValid())
        return;
    const quint64 size = m_document->size();
    if (neededOffset >= size)
        return;

    const bool covered = m_windowValid && neededOffset >= m_windowStart &&
                         neededOffset < m_windowStart + static_cast<quint64>(m_window.size());
    if (covered)
        return;

    const quint64 start = (neededOffset / kWindowBytes) * kWindowBytes;
    const quint64 length = std::min<quint64>(kWindowBytes + 512, size - start);
    auto bytes = m_document->readMerged(start, length);
    if (!bytes)
        return;
    m_window = bytes.value();
    m_windowStart = start;
    m_windowValid = true;
}

QByteArray HexView::rowSlice(quint64 rowStart, quint64 length)
{
    if (!m_document || !m_document->isValid() || length == 0)
        return {};
    ensureWindow(rowStart);
    const quint64 windowEnd = m_windowStart + static_cast<quint64>(m_window.size());
    if (rowStart >= m_windowStart && rowStart + length <= windowEnd) {
        const auto offset = static_cast<qsizetype>(rowStart - m_windowStart);
        return m_window.mid(offset, static_cast<qsizetype>(length));
    }
    auto bytes = m_document->readMerged(rowStart, length);
    return bytes ? bytes.value() : QByteArray();
}

void HexView::paintEvent(QPaintEvent*)
{
    const Theme& theme = ThemeManager::instance().current();
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), theme.color(QStringLiteral("base")));

    if (!m_document || !m_document->isValid()) {
        painter.setPen(theme.color(QStringLiteral("hexBytes")));
        painter.drawText(viewport()->rect(), Qt::AlignCenter,
                         QStringLiteral("No file open"));
        return;
    }

    const quint64 size = m_document->size();
    if (size == 0) {
        painter.setPen(theme.color(QStringLiteral("hexBytes")));
        painter.drawText(viewport()->rect(), Qt::AlignCenter,
                         QStringLiteral("Empty file (0 bytes)"));
        return;
    }

    const int bpr = m_layout.normalized().bytesPerRow;
    const int charWidth = m_metrics.charWidth;
    const int rowHeight = m_metrics.rowHeight;
    const int rows = visibleRowCount() + 1;
    const quint64 rowsTotal = rowCount();

    const quint64 selectionStartValue = selectionStart();
    const quint64 selectionEnd = selectionStartValue + selectionLength();

    ensureWindow(m_topRow * static_cast<quint64>(bpr));

    const QColor gutterColor = theme.color(QStringLiteral("gutter"));
    const QColor offsetColor = theme.color(QStringLiteral("hexOffset"));
    const QColor byteColor = theme.color(QStringLiteral("hexBytes"));
    const QColor asciiColor = theme.color(QStringLiteral("hexAscii"));
    const QColor selectionColor = theme.color(QStringLiteral("hexSelection"));
    const QColor searchColor = theme.color(QStringLiteral("hexSearch"));
    const QColor accentColor = theme.color(QStringLiteral("accent"));
    QColor signatureTint = accentColor;
    signatureTint.setAlpha(28);
    QColor signatureLine = accentColor;
    signatureLine.setAlpha(150);

    painter.setFont(m_font);

    for (int rowIndex = 0; rowIndex < rows; ++rowIndex) {
        const quint64 row = m_topRow + static_cast<quint64>(rowIndex);
        if (row >= rowsTotal)
            break;

        const quint64 rowStart = row * static_cast<quint64>(bpr);
        const quint64 available = std::min<quint64>(static_cast<quint64>(bpr), size - rowStart);
        const QByteArray rowData = rowSlice(rowStart, available);
        if (rowData.isEmpty())
            break;

        const int y = rowIndex * rowHeight;
        if ((rowIndex % 2) == 1)
            painter.fillRect(QRect(0, y, viewport()->width(), rowHeight),
                             theme.color(QStringLiteral("base")));

        // Offset gutter.
        painter.setPen(offsetColor);
        const QRect gutterRect(kLeftMargin - m_xOffset, y,
                               m_metrics.gutterWidth - kLeftMargin - 8, rowHeight);
        painter.drawText(gutterRect, Qt::AlignRight | Qt::AlignVCenter,
                         fmt::hex(rowStart, m_metrics.offsetDigits));
        painter.setPen(gutterColor);
        painter.drawLine(m_metrics.gutterWidth - 4 - m_xOffset, y,
                         m_metrics.gutterWidth - 4 - m_xOffset, y + rowHeight);

        // Bookmark marker: a solid accent bar on the row holding a bookmark.
        for (const quint64 bookmark : m_bookmarks) {
            if (bookmark >= rowStart && bookmark < rowStart + available) {
                painter.fillRect(QRect(2 - m_xOffset, y + 2, 3, rowHeight - 4), accentColor);
                break;
            }
        }

        for (qsizetype column = 0; column < rowData.size(); ++column) {
            const quint64 offset = rowStart + static_cast<quint64>(column);
            const int x = columnX(static_cast<int>(column)) - m_xOffset;
            const auto value = static_cast<quint8>(rowData.at(column));

            const bool selected =
                offset >= selectionStartValue && offset < selectionEnd && selectionLength() > 0;
            if (selected)
                painter.fillRect(QRect(x - 1, y, 2 * charWidth + 2, rowHeight), selectionColor);

            for (int hitIndex = 0; hitIndex < m_searchHits.size(); ++hitIndex) {
                const quint64 hit = m_searchHits.at(hitIndex);
                if (offset >= hit &&
                    offset < hit + static_cast<quint64>(std::max(1, m_patternLength))) {
                    painter.fillRect(QRect(x - 1, y, 2 * charWidth + 2, rowHeight), searchColor);
                    break;
                }
            }

            const bool pendingNibble =
                m_editing && m_pendingHighNibble >= 0 && offset == m_cursor;
            quint64 displayed = value;
            if (pendingNibble)
                displayed = static_cast<quint64>((m_pendingHighNibble << 4) | (value & 0x0F));
            const bool modified = m_document->hasOverride(offset);

            bool signedRange = false;
            for (const SignatureHit& hit : m_signatures) {
                const quint64 end = hit.offset + hit.length;
                if (offset >= hit.offset && offset < end) {
                    signedRange = true;
                    break;
                }
            }
            if (signedRange) {
                painter.fillRect(QRect(x - 1, y, 2 * charWidth + 2, rowHeight), signatureTint);
                painter.fillRect(QRect(x - 1, y + rowHeight - 2, 2 * charWidth + 2, 2),
                                 signatureLine);
            }

            painter.setPen((modified || pendingNibble) ? accentColor : byteColor);
            painter.drawText(QRect(x, y, 2 * charWidth, rowHeight),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QStringLiteral("%1")
                                 .arg(static_cast<quint8>(displayed), 2, 16, QLatin1Char('0'))
                                 .toUpper());

            const int asciiX = m_metrics.asciiStart + static_cast<int>(column) * charWidth -
                               m_xOffset;
            painter.setPen(modified ? accentColor : asciiColor);
            painter.drawText(QRect(asciiX, y, charWidth, rowHeight),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QString(QChar::fromLatin1(printableChar(static_cast<quint8>(displayed)))));
        }

        // Cursor cell outline (hex cell, or the ASCII cell when editing there).
        if (m_cursor >= rowStart && m_cursor < rowStart + available) {
            const int column = static_cast<int>(m_cursor - rowStart);
            QRect cursorRect;
            if (m_editing && m_cursorInAscii) {
                const int asciiX =
                    m_metrics.asciiStart + column * charWidth - m_xOffset;
                cursorRect = QRect(asciiX, y, charWidth, rowHeight - 1);
            } else {
                const int x = columnX(column) - m_xOffset;
                cursorRect = QRect(x - 1, y, 2 * charWidth + 1, rowHeight - 1);
            }
            painter.setPen(QPen(accentColor, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(cursorRect);
        }
    }
}

void HexView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
    emit viewResized();
}

void HexView::scrollContentsBy(int dx, int dy)
{
    if (!m_updatingScrollBars) {
        m_topRow = static_cast<quint64>(std::max(0, verticalScrollBar()->value()));
        m_xOffset = horizontalScrollBar()->value();
    }
    QAbstractScrollArea::scrollContentsBy(dx, dy);
    viewport()->update();
}

std::optional<quint64> HexView::offsetAt(const QPoint& position) const
{
    if (!m_document || !m_document->isValid() || m_document->size() == 0)
        return std::nullopt;
    if (position.y() < 0)
        return std::nullopt;

    const quint64 rowIndex =
        m_topRow + static_cast<quint64>(std::max(0, position.y() / m_metrics.rowHeight));
    const int bpr = m_layout.normalized().bytesPerRow;
    if (rowIndex >= rowCount())
        return std::nullopt;

    const quint64 rowStart = rowIndex * static_cast<quint64>(bpr);
    const int available =
        static_cast<int>(std::min<quint64>(static_cast<quint64>(bpr),
                                           m_document->size() - rowStart));
    const int x = position.x() + m_xOffset;

    int column = -1;
    for (int candidate = 0; candidate < available; ++candidate) {
        const int cellX = columnX(candidate);
        if (x >= cellX - m_metrics.charWidth / 2 &&
            x < cellX + 2 * m_metrics.charWidth + m_metrics.charWidth / 2) {
            column = candidate;
            break;
        }
    }
    if (column < 0 && x >= m_metrics.asciiStart) {
        const int candidate = (x - m_metrics.asciiStart) / m_metrics.charWidth;
        if (candidate >= 0 && candidate < available)
            column = candidate;
    }
    if (column < 0)
        return std::nullopt;
    return rowStart + static_cast<quint64>(column);
}

void HexView::setCursor(quint64 offset, bool extendSelection)
{
    if (!m_document)
        return;
    const quint64 clamped = m_document->clampOffset(offset);
    if (!extendSelection)
        m_anchor = clamped;
    m_pendingHighNibble = -1;
    m_cursor = clamped;
    ensureVisible(clamped);
    emit cursorMoved(m_cursor);
    updateSelectionState();
    viewport()->update();
}

void HexView::ensureVisible(quint64 offset)
{
    if (!m_document)
        return;
    const int bpr = m_layout.normalized().bytesPerRow;
    const quint64 row = offset / static_cast<quint64>(bpr);
    const int visible = visibleRowCount();
    if (row < m_topRow) {
        m_topRow = row;
        updateScrollBars();
    } else if (row >= m_topRow + static_cast<quint64>(visible)) {
        m_topRow = row - static_cast<quint64>(visible) + 1;
        updateScrollBars();
    }
}

void HexView::updateSelectionState()
{
    emit selectionChanged(selectionStart(), selectionLength());
}

quint64 HexView::selectionStart() const
{
    return std::min(m_anchor, m_cursor);
}

quint64 HexView::selectionLength() const
{
    if (!m_document)
        return 0;
    return std::max(m_anchor, m_cursor) - selectionStart() + 1;
}

void HexView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    const auto offset = offsetAt(event->pos());
    if (!offset) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    m_dragging = true;
    if (m_editing) {
        const int x = event->pos().x() + m_xOffset;
        m_cursorInAscii = x >= m_metrics.asciiStart;
    }
    setCursor(*offset, false);
}

void HexView::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging) {
        const auto offset = offsetAt(event->pos());
        if (offset)
            emit hovered(*offset);
        else
            emit hoverCleared();
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }
    const auto offset = offsetAt(event->pos());
    if (offset && *offset != m_cursor)
        setCursor(*offset, true);
}

void HexView::leaveEvent(QEvent* event)
{
    emit hoverCleared();
    QAbstractScrollArea::leaveEvent(event);
}

void HexView::mouseReleaseEvent(QMouseEvent* event)
{
    m_dragging = false;
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void HexView::keyPressEvent(QKeyEvent* event)
{
    if (!m_document || !m_document->isValid()) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    const int bpr = m_layout.normalized().bytesPerRow;
    const bool shift = (event->modifiers() & Qt::ShiftModifier) != 0;
    const bool control = (event->modifiers() & Qt::ControlModifier) != 0;
    const quint64 fileSize = m_document->size();
    quint64 target = m_cursor;

    // Direct byte entry: hex digits in the hex column, printable characters in
    // the ASCII column. Only active after the user enables editing for the file.
    const bool noModifiers = (event->modifiers() & ~Qt::ShiftModifier) == Qt::NoModifier;
    if (m_editing && noModifiers && fileSize > 0) {
        const QString text = event->text();
        if (m_cursorInAscii && text.size() == 1) {
            const ushort code = text.at(0).unicode();
            if (code >= 0x20U && code < 0x7FU) {
                m_pendingHighNibble = -1;
                if (m_document->setByte(m_cursor, static_cast<quint8>(code))) {
                    emit documentEdited();
                    if (m_cursor + 1 < fileSize)
                        setCursor(m_cursor + 1, false);
                    else
                        viewport()->update();
                }
                return;
            }
        } else if (!m_cursorInAscii && text.size() == 1) {
            const int nibble = hexNibble(text.at(0));
            if (nibble >= 0) {
                if (m_pendingHighNibble < 0) {
                    m_pendingHighNibble = nibble;
                    viewport()->update();
                } else {
                    const auto value =
                        static_cast<quint8>((m_pendingHighNibble << 4) | nibble);
                    m_pendingHighNibble = -1;
                    if (m_document->setByte(m_cursor, value)) {
                        emit documentEdited();
                        if (m_cursor + 1 < fileSize)
                            setCursor(m_cursor + 1, false);
                        else
                            viewport()->update();
                    } else {
                        viewport()->update();
                    }
                }
                return;
            }
        }
        if (event->key() == Qt::Key_Escape && m_pendingHighNibble >= 0) {
            m_pendingHighNibble = -1;
            viewport()->update();
            return;
        }
    }

    switch (event->key()) {
    case Qt::Key_Left:
        target = m_cursor > 0 ? m_cursor - 1 : 0;
        break;
    case Qt::Key_Right:
        target = std::min(m_cursor + 1, fileSize == 0 ? 0 : fileSize - 1);
        break;
    case Qt::Key_Up:
        target = m_cursor >= static_cast<quint64>(bpr) ? m_cursor - static_cast<quint64>(bpr)
                                                      : m_cursor % static_cast<quint64>(bpr);
        break;
    case Qt::Key_Down: {
        const quint64 candidate = m_cursor + static_cast<quint64>(bpr);
        target = candidate < fileSize ? candidate : m_cursor;
        break;
    }
    case Qt::Key_PageUp: {
        const quint64 step = static_cast<quint64>(bpr) * static_cast<quint64>(visibleRowCount());
        target = m_cursor > step ? m_cursor - step : 0;
        break;
    }
    case Qt::Key_PageDown: {
        const quint64 step = static_cast<quint64>(bpr) * static_cast<quint64>(visibleRowCount());
        target = std::min(m_cursor + step, fileSize == 0 ? 0 : fileSize - 1);
        break;
    }
    case Qt::Key_Home:
        target = control ? 0 : (m_cursor / static_cast<quint64>(bpr)) * static_cast<quint64>(bpr);
        break;
    case Qt::Key_End: {
        const quint64 rowStart =
            (m_cursor / static_cast<quint64>(bpr)) * static_cast<quint64>(bpr);
        target = control ? (fileSize == 0 ? 0 : fileSize - 1)
                         : std::min(rowStart + static_cast<quint64>(bpr) - 1,
                                    fileSize == 0 ? 0 : fileSize - 1);
        break;
    }
    case Qt::Key_G:
        if (control) {
            emit gotoRequested();
            return;
        }
        QAbstractScrollArea::keyPressEvent(event);
        return;
    case Qt::Key_F:
        if (control) {
            emit findRequested();
            return;
        }
        QAbstractScrollArea::keyPressEvent(event);
        return;
    case Qt::Key_C:
        if (control) {
            emit copyRequested();
            return;
        }
        QAbstractScrollArea::keyPressEvent(event);
        return;
    case Qt::Key_A:
        if (control) {
            selectAll();
            return;
        }
        QAbstractScrollArea::keyPressEvent(event);
        return;
    case Qt::Key_Escape:
        clearSelection();
        return;
    default:
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    setCursor(target, shift);
}

void HexView::contextMenuEvent(QContextMenuEvent* event)
{
    if (!m_document || !m_document->isValid()) {
        QAbstractScrollArea::contextMenuEvent(event);
        return;
    }
    const std::optional<quint64> clicked = offsetAt(event->pos());
    const quint64 target = clicked.value_or(m_cursor);
    if (clicked)
        setCursor(target, false);

    QMenu menu(this);
    QAction* copyHex = menu.addAction(QStringLiteral("Copy selection as hex"));
    QAction* copyAscii = menu.addAction(QStringLiteral("Copy selection as ASCII"));
    QAction* copyOffset = menu.addAction(QStringLiteral("Copy offset"));
    menu.addSeparator();
    QAction* find = menu.addAction(QStringLiteral("Find\u2026"));
    QAction* goTo = menu.addAction(QStringLiteral("Go to offset\u2026"));
    menu.addSeparator();
    QAction* toggleBookmark = menu.addAction(QStringLiteral("Toggle bookmark here"));
    QAction* annotateBookmark = menu.addAction(QStringLiteral("Bookmark with note\u2026"));
    QAction* compare = menu.addAction(QStringLiteral("Compare with another file\u2026"));
    menu.addSeparator();
    QAction* selectAllAction = menu.addAction(QStringLiteral("Select all"));
    const bool hasSelection = selectionLength() > 1;
    copyHex->setEnabled(hasSelection);
    copyAscii->setEnabled(hasSelection);

    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == copyHex || chosen == copyAscii)
        emit copyRequested();
    else if (chosen == copyOffset) {
        QApplication::clipboard()->setText(
            QStringLiteral("0x%1").arg(target, 0, 16));
    } else if (chosen == find)
        emit findRequested();
    else if (chosen == goTo)
        emit gotoRequested();
    else if (chosen == toggleBookmark)
        emit bookmarkToggleRequested(target);
    else if (chosen == annotateBookmark)
        emit bookmarkNoteRequested(target);
    else if (chosen == compare)
        emit compareRequested(target);
    else if (chosen == selectAllAction)
        selectAll();
}

void HexView::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
        recomputeMetrics();
        updateScrollBars();
    }
    QAbstractScrollArea::changeEvent(event);
    viewport()->update();
}

void HexView::goToOffset(quint64 offset)
{
    if (!m_document)
        return;
    const quint64 clamped = m_document->clampOffset(offset);
    m_anchor = clamped;
    m_cursor = clamped;
    ensureVisible(clamped);
    emit cursorMoved(m_cursor);
    updateSelectionState();
    viewport()->update();
}

void HexView::setSelection(quint64 start, quint64 length)
{
    if (!m_document || length == 0)
        return;
    m_anchor = m_document->clampOffset(start);
    m_cursor = m_document->clampOffset(start + length - 1);
    ensureVisible(m_cursor);
    emit cursorMoved(m_cursor);
    updateSelectionState();
    viewport()->update();
}

void HexView::selectAll()
{
    if (!m_document || m_document->size() == 0)
        return;
    m_anchor = 0;
    m_cursor = m_document->size() - 1;
    updateSelectionState();
    viewport()->update();
}

void HexView::clearSelection()
{
    m_anchor = m_cursor;
    updateSelectionState();
    viewport()->update();
}

QByteArray HexView::selectedBytes() const
{
    if (!m_document || !m_document->isValid())
        return {};
    const quint64 start = selectionStart();
    const quint64 length = selectionLength();
    if (length == 0)
        return {};
    auto bytes = m_document->readMerged(start, length);
    return bytes ? bytes.value() : QByteArray();
}

QString HexView::selectionAsHex() const
{
    const QByteArray bytes = selectedBytes();
    QStringList parts;
    parts.reserve(bytes.size());
    for (const char c : bytes) {
        parts.append(QStringLiteral("%1")
                         .arg(static_cast<quint8>(c), 2, 16, QLatin1Char('0'))
                         .toUpper());
    }
    return parts.join(QLatin1Char(' '));
}

QString HexView::selectionAsAscii() const
{
    const QByteArray bytes = selectedBytes();
    QString out;
    out.reserve(bytes.size());
    for (const char c : bytes)
        out.append(QChar::fromLatin1(printableChar(static_cast<quint8>(c))));
    return out;
}

void HexView::setSearchHits(const QVector<quint64>& offsets, int patternLength)
{
    m_searchHits = offsets;
    std::sort(m_searchHits.begin(), m_searchHits.end());
    m_patternLength = std::max(1, patternLength);
    m_currentHit = -1;
    viewport()->update();
}

void HexView::clearSearchHits()
{
    m_searchHits.clear();
    m_currentHit = -1;
    viewport()->update();
}

void HexView::setBookmarks(const QVector<quint64>& offsets)
{
    m_bookmarks = offsets;
    viewport()->update();
}

void HexView::setSignatures(const QVector<SignatureHit>& signatures)
{
    m_signatures = signatures;
    viewport()->update();
}

void HexView::clearSignatures()
{
    m_signatures.clear();
    viewport()->update();
}

bool HexView::findNextHit(bool forward)
{
    if (m_searchHits.isEmpty() || !m_document)
        return false;
    quint64 target = 0;
    if (forward) {
        const auto it = std::upper_bound(m_searchHits.begin(), m_searchHits.end(), m_cursor);
        target = it != m_searchHits.end() ? *it : m_searchHits.first();
    } else {
        const auto it = std::lower_bound(m_searchHits.begin(), m_searchHits.end(), m_cursor);
        target = it != m_searchHits.begin() ? *(it - 1) : m_searchHits.last();
    }
    goToOffset(target);
    return true;
}

QSize HexView::sizeHint() const
{
    return QSize(760, 420);
}

}  // namespace pkg::ui
