// Virtualised hex editor view.
//
// Only the visible rows are painted and the bytes behind them are read from
// the memory mapped file in a bounded sliding window, so opening a multi
// gigabyte package costs almost no RAM.
#pragma once

#include "core/HexDocument.h"
#include "core/Signature.h"

#include <QAbstractScrollArea>
#include <QByteArray>
#include <QFont>
#include <QFontMetrics>
#include <QVector>

#include <memory>
#include <optional>

namespace pkg::ui {

class HexView : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit HexView(QWidget* parent = nullptr);
    ~HexView() override;

    void setDocument(std::shared_ptr<HexDocument> document);
    void closeDocument();
    [[nodiscard]] bool hasDocument() const { return m_document != nullptr; }
    [[nodiscard]] const HexDocument* document() const { return m_document.get(); }
    [[nodiscard]] std::shared_ptr<HexDocument> sharedDocument() const { return m_document; }

    void setLayout(const HexLayout& layout);
    [[nodiscard]] HexLayout layout() const { return m_layout; }

    void setEditorFont(const QFont& font);
    [[nodiscard]] QFont editorFont() const { return m_font; }

    /// When enabled, hex digits / printable ASCII typed at the cursor modify the
    /// in-memory document (never the file). Edits are undoable until saved.
    void setEditingEnabled(bool enabled);
    [[nodiscard]] bool isEditingEnabled() const { return m_editing; }
    /// Call after an undo/redo or external edit so the cached window is reread.
    void notifyDocumentChanged();

    [[nodiscard]] quint64 cursorOffset() const { return m_cursor; }
    [[nodiscard]] quint64 selectionStart() const;
    [[nodiscard]] quint64 selectionLength() const;

    void goToOffset(quint64 offset);
    void setSelection(quint64 start, quint64 length);
    void selectAll();
    void clearSelection();

    void setSearchHits(const QVector<quint64>& offsets, int patternLength);
    void clearSearchHits();

    /// Underlines and tints byte ranges that matched documented signatures.
    void setSignatures(const QVector<SignatureHit>& signatures);
    void clearSignatures();
    [[nodiscard]] bool findNextHit(bool forward);
    [[nodiscard]] int searchHitCount() const
    {
        return static_cast<int>(m_searchHits.size());
    }

    [[nodiscard]] QByteArray selectedBytes() const;
    [[nodiscard]] QString selectionAsHex() const;
    [[nodiscard]] QString selectionAsAscii() const;

    [[nodiscard]] QSize sizeHint() const override;

signals:
    void cursorMoved(quint64 offset);
    void selectionChanged(quint64 start, quint64 length);
    void findRequested();
    void gotoRequested();
    void copyRequested();
    void documentEdited();
    void hovered(quint64 offset);
    void hoverCleared();
    void viewResized();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void changeEvent(QEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    struct Metrics {
        int charWidth = 8;
        int rowHeight = 18;
        int offsetDigits = 8;
        int gutterWidth = 80;
        int hexStart = 90;
        int asciiStart = 400;
        int contentWidth = 480;
    };

    [[nodiscard]] int visibleRowCount() const;
    [[nodiscard]] quint64 rowCount() const;
    [[nodiscard]] int columnX(int column) const;
    void recomputeMetrics();
    void updateScrollBars();
    void ensureWindow(quint64 neededOffset);
    [[nodiscard]] QByteArray rowSlice(quint64 rowStart, quint64 length);
    [[nodiscard]] std::optional<quint64> offsetAt(const QPoint& position) const;
    void setCursor(quint64 offset, bool extendSelection);
    void ensureVisible(quint64 offset);
    void updateSelectionState();

    std::shared_ptr<HexDocument> m_document;
    HexLayout m_layout;
    QFont m_font;
    QFontMetrics m_fontMetrics;
    Metrics m_metrics;

    quint64 m_topRow = 0;
    int m_xOffset = 0;
    quint64 m_cursor = 0;
    quint64 m_anchor = 0;
    bool m_dragging = false;
    bool m_updatingScrollBars = false;
    bool m_editing = false;
    bool m_cursorInAscii = false;
    int m_pendingHighNibble = -1;

    QByteArray m_window;
    quint64 m_windowStart = 0;
    bool m_windowValid = false;

    QVector<quint64> m_searchHits;
    int m_patternLength = 1;
    int m_currentHit = -1;
    QVector<SignatureHit> m_signatures;
};

}  // namespace pkg::ui
