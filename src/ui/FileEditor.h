#pragma once

#include "core/ByteSearch.h"
#include "core/FormatInfo.h"
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
class QLineEdit;
class QTabWidget;
class QToolButton;

namespace pkg::ui {

class ByteInspector;
class PackageInfoView;

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
    void goToOffsetInteractive();
    void goToOffset(quint64 offset);

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

    std::shared_ptr<HexDocument> m_document;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_formatLabel = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QToolButton* m_editToggle = nullptr;
    QTabWidget* m_tabs = nullptr;
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

    PackageInfo m_info;
    bool m_hasAnalysis = false;

    std::shared_ptr<QFutureWatcher<Result<SearchResult>>> m_watcher;
    CancelToken m_token;
    QByteArray m_pattern;
    QVector<quint64> m_hits;
    bool m_searchRunning = false;
};

}  // namespace pkg::ui
