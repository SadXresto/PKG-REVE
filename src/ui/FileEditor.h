#pragma once

#include "core/ByteSearch.h"
#include "core/CodeListing.h"
#include "core/FormatInfo.h"
#include "core/HexBookmarks.h"
#include "core/HexDocument.h"
#include "ui/HexView.h"

#include <QByteArray>
#include <QFutureWatcher>
#include <QVector>
#include <QString>
#include <QWidget>

#include <memory>

class QCheckBox;
class QLabel;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSplitter;
class QTabWidget;
class QToolButton;

namespace pkg::ui {

class ByteInspector;
class PackageInfoView;
class StructurePanel;

/// One open file: header strip, hex view, analysis page and an inline find bar
/// with streaming search running on a worker thread.
class FileEditor : public QWidget {
    Q_OBJECT

public:
    explicit FileEditor(std::shared_ptr<HexDocument> document, QWidget* parent = nullptr);

    [[nodiscard]] HexView* hexView() const { return m_hex; }
    [[nodiscard]] QString path() const;
    [[nodiscard]] quint64 fileSize() const;
    [[nodiscard]] std::shared_ptr<HexDocument> document() const { return m_document; }

    void setAnalysis(const pkg::PackageInfo& info);
    void setAnalysisError(const QString& message);
    void showAnalysisTab();
    /// Diagnostic/screenshot helper: reveals the live inspector at `offset`.
    void showByteInspectorAt(quint64 offset);
    [[nodiscard]] QString analysisReport() const;
    [[nodiscard]] const pkg::PackageInfo& analysisInfo() const { return m_info; }
    [[nodiscard]] bool hasAnalysis() const { return m_hasAnalysis; }

    /// Editing is opt-in per file. Edits stay in memory until saveTo().
    void setEditingEnabled(bool enabled);
    [[nodiscard]] bool isEditingEnabled() const;
    [[nodiscard]] bool isModified() const;
    [[nodiscard]] bool undoEdit();
    [[nodiscard]] bool redoEdit();
    /// Writes the document (edits applied) to `path` and reloads from it.
    [[nodiscard]] Result<quint64> saveTo(const QString& path);
    /// Opens `path` as the new backing file and drops edit state.
    [[nodiscard]] Status reloadFrom(const QString& path);

    void setEditorFont(const QFont& font);
    void setHexLayout(const HexLayout& layout);

    void showFindBar(bool visible);
    void focusFindBar();

    /// Bookmarks are navigation aids kept outside the file bytes.
    void toggleBookmark(quint64 offset);
    void addBookmarkWithNote(quint64 offset);
    [[nodiscard]] bool nextBookmark();
    [[nodiscard]] bool previousBookmark();
    void showBookmarkList();
    [[nodiscard]] QList<pkg::HexBookmark> bookmarks() const { return m_bookmarks.all(); }
    /// Opens the binary comparison view against another file.
    void compareWithFile();

    /// Structure builder lives beside the hex view so tree selections can
    /// highlight bytes without hiding either side.
    void showStructurePanel(bool visible);
    void toggleStructurePanel();
    void openStructureTemplate();
    void editStructureTemplate();
    [[nodiscard]] StructurePanel* structurePanel() const { return m_structurePanel; }
    void goToOffsetInteractive();
    void goToOffset(quint64 offset);

    /// Human-readable listing of the file (or its first `maxBytes`), used by the
    /// Code tab and as bounded context for the AI assistant.
    [[nodiscard]] QString readableListing(quint64 maxBytes) const;
    /// True when more than one byte is selected in the hex view.
    [[nodiscard]] bool hasSelection() const;
    /// Offset/hex/ASCII listing of the current selection, bounded so a huge
    /// selection cannot flood a prompt. Empty when nothing is selected.
    [[nodiscard]] QString selectionContext(quint64 maxBytes) const;
    /// Applies parsed listing runs as undoable in-memory edits. Enables editing
    /// for this file first, because the change must stay reviewable.
    [[nodiscard]] Result<int> applyByteEdits(const QVector<pkg::ListingEntry>& entries);
    void showCodeTab();
    void refreshCodeListing();

signals:
    void cursorMoved(quint64 offset);
    void selectionChanged(quint64 start, quint64 length);
    void analysisExportRequested();
    void modificationChanged(bool modified);
    void pathChanged(const QString& path);

private:
    void startSearch();
    void cancelSearch();
    void onSearchFinished();
    void refreshEditState();
    void updateTitleLabel();
    void positionInspector();
    void replaceAll();
    void updateHexInfoStrip();
    void refreshBookmarkMarkers();

    std::shared_ptr<HexDocument> m_document;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_formatLabel = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QToolButton* m_undoButton = nullptr;
    QToolButton* m_redoButton = nullptr;
    QToolButton* m_editToggle = nullptr;
    QToolButton* m_structureToggle = nullptr;
    QTabWidget* m_tabs = nullptr;
    QSplitter* m_hexSplitter = nullptr;
    QPlainTextEdit* m_listingEdit = nullptr;
    QLabel* m_listingStatus = nullptr;
    StructurePanel* m_structurePanel = nullptr;
    HexView* m_hex = nullptr;
    ByteInspector* m_inspector = nullptr;
    PackageInfoView* m_infoView = nullptr;

    QWidget* m_findBar = nullptr;
    QLineEdit* m_findInput = nullptr;
    QLineEdit* m_replaceInput = nullptr;
    QLabel* m_replaceStatus = nullptr;
    QToolButton* m_replaceAllButton = nullptr;
    QCheckBox* m_caseBox = nullptr;
    QLabel* m_findStatus = nullptr;
    QToolButton* m_previousButton = nullptr;
    QToolButton* m_nextButton = nullptr;

    QLabel* m_infoName = nullptr;
    QLabel* m_infoSize = nullptr;
    QLabel* m_infoOffset = nullptr;
    QLabel* m_infoSelection = nullptr;
    QLabel* m_infoMode = nullptr;
    QToolButton* m_bookmarkButton = nullptr;
    pkg::HexBookmarks m_bookmarks;

    PackageInfo m_info;
    bool m_hasAnalysis = false;

    std::shared_ptr<QFutureWatcher<Result<SearchResult>>> m_watcher;
    CancelToken m_token;
    QByteArray m_pattern;
    QVector<quint64> m_hits;
    bool m_searchRunning = false;
};

}  // namespace pkg::ui
