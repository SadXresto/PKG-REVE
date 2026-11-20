#include "ui/FileEditor.h"

#include "core/FormatUtil.h"
#include "core/Logging.h"
#include "core/SafeMath.h"
#include "ui/ByteInspector.h"
#include "ui/PackageInfoView.h"
#include "ui/StructurePanel.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>

namespace pkg::ui {
namespace {
constexpr auto kLogCategory = "editor";

QToolButton* makeBarButton(const QString& text, const QString& tooltip, QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(tooltip);
    button->setAutoRaise(true);
    return button;
}

/// Accepts "AA BB CC" hex notation or plain UTF-8 text.
QByteArray parsePattern(const QString& raw)
{
    if (raw.isEmpty())
        return {};
    QByteArray pattern;
    static const QRegularExpression hexOnly(QStringLiteral("^[0-9A-Fa-f ]+$"));
    const bool looksHex = raw.contains(QLatin1Char(' ')) && hexOnly.match(raw).hasMatch();
    if (looksHex) {
        const QStringList parts = raw.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            bool ok = false;
            const uint value = part.toUInt(&ok, 16);
            if (!ok || value > 0xFFU) {
                pattern.clear();
                break;
            }
            pattern.append(static_cast<char>(value));
        }
    }
    if (pattern.isEmpty())
        pattern = raw.toUtf8();
    return pattern;
}

}  // namespace

FileEditor::FileEditor(std::shared_ptr<HexDocument> document, QWidget* parent)
    : QWidget(parent), m_document(std::move(document))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("fileEditorHeader"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 4, 8, 4);
    headerLayout->setSpacing(10);

    m_titleLabel = new QLabel(header);
    m_titleLabel->setObjectName(QStringLiteral("editorPath"));
    headerLayout->addWidget(m_titleLabel);

    m_formatLabel = new QLabel(header);
    m_formatLabel->setObjectName(QStringLiteral("hintLabel"));
    headerLayout->addWidget(m_formatLabel);

    m_summaryLabel = new QLabel(header);
    m_summaryLabel->setObjectName(QStringLiteral("hintLabel"));
    headerLayout->addWidget(m_summaryLabel, 1);

    m_editToggle = new QToolButton(header);
    m_editToggle->setObjectName(QStringLiteral("hintLabel"));
    m_editToggle->setCheckable(true);
    m_editToggle->setAutoRaise(true);
    m_editToggle->setText(QStringLiteral("Editing off"));
    m_editToggle->setToolTip(
        QStringLiteral("Enable byte editing for this file. Edits are kept in memory and can "
                       "be undone until you save. Saving asks before overwriting an existing "
                       "file and writes a timestamped backup next to it."));
    headerLayout->addWidget(m_editToggle);

    m_structureToggle = new QToolButton(header);
    m_structureToggle->setObjectName(QStringLiteral("hintLabel"));
    m_structureToggle->setCheckable(true);
    m_structureToggle->setAutoRaise(true);
    m_structureToggle->setText(QStringLiteral("Structure"));
    m_structureToggle->setToolTip(
        QStringLiteral("Show the binary structure builder. Templates describe a layout; "
                       "they are not a confirmed format."));
    headerLayout->addWidget(m_structureToggle);

    layout->addWidget(header);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    layout->addWidget(m_tabs, 1);

    auto* hexPage = new QWidget(m_tabs);
    auto* hexLayout = new QVBoxLayout(hexPage);
    hexLayout->setContentsMargins(0, 0, 0, 0);
    hexLayout->setSpacing(0);

    m_hexSplitter = new QSplitter(Qt::Horizontal, hexPage);
    m_hexSplitter->setChildrenCollapsible(false);
    m_hex = new HexView(m_hexSplitter);
    m_hexSplitter->addWidget(m_hex);

    m_structurePanel = new StructurePanel(m_hexSplitter);
    m_structurePanel->setDocument(m_document.get());
    m_structurePanel->setVisible(false);
    m_hexSplitter->addWidget(m_structurePanel);
    m_hexSplitter->setStretchFactor(0, 3);
    m_hexSplitter->setStretchFactor(1, 2);
    m_hexSplitter->setSizes({660, 570});
    hexLayout->addWidget(m_hexSplitter, 1);

    // Find bar -----------------------------------------------------------
    m_findBar = new QWidget(hexPage);
    m_findBar->setObjectName(QStringLiteral("fileEditorHeader"));
    auto* findOuter = new QVBoxLayout(m_findBar);
    findOuter->setContentsMargins(8, 4, 8, 4);
    findOuter->setSpacing(4);
    auto* findLayout = new QHBoxLayout();
    findLayout->setContentsMargins(0, 0, 0, 0);
    findLayout->setSpacing(6);
    findOuter->addLayout(findLayout);

    auto* findLabel = new QLabel(QStringLiteral("Find"), m_findBar);
    findLabel->setObjectName(QStringLiteral("hintLabel"));
    findLayout->addWidget(findLabel);

    m_findInput = new QLineEdit(m_findBar);
    m_findInput->setPlaceholderText(
        QStringLiteral("ASCII text, or hex bytes such as 7F 45 4C 46"));
    findLayout->addWidget(m_findInput, 1);

    m_caseBox = new QCheckBox(QStringLiteral("Aa"), m_findBar);
    m_caseBox->setToolTip(QStringLiteral("Case sensitive"));
    findLayout->addWidget(m_caseBox);

    m_previousButton =
        makeBarButton(QStringLiteral("\u25B2"), QStringLiteral("Previous match"), m_findBar);
    m_nextButton = makeBarButton(QStringLiteral("\u25BC"), QStringLiteral("Next match"), m_findBar);
    findLayout->addWidget(m_previousButton);
    findLayout->addWidget(m_nextButton);

    m_findStatus = new QLabel(m_findBar);
    m_findStatus->setObjectName(QStringLiteral("hintLabel"));
    findLayout->addWidget(m_findStatus);

    auto* closeFind =
        makeBarButton(QStringLiteral("\u2715"), QStringLiteral("Close find bar"), m_findBar);
    findLayout->addWidget(closeFind);

    auto* replaceLayout = new QHBoxLayout();
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replaceLayout->setSpacing(6);
    auto* replaceLabel = new QLabel(QStringLiteral("Replace"), m_findBar);
    replaceLabel->setObjectName(QStringLiteral("hintLabel"));
    replaceLayout->addWidget(replaceLabel);
    m_replaceInput = new QLineEdit(m_findBar);
    m_replaceInput->setPlaceholderText(
        QStringLiteral("Replacement with the same byte length (e.g. 90 90)"));
    replaceLayout->addWidget(m_replaceInput, 1);
    m_replaceAllButton = makeBarButton(QStringLiteral("Replace all"),
                                       QStringLiteral("Replace every search hit in memory"),
                                       m_findBar);
    m_replaceAllButton->setObjectName(QStringLiteral("editToggle"));
    replaceLayout->addWidget(m_replaceAllButton);
    m_replaceStatus = new QLabel(m_findBar);
    m_replaceStatus->setObjectName(QStringLiteral("hintLabel"));
    replaceLayout->addWidget(m_replaceStatus);
    findOuter->addLayout(replaceLayout);

    hexLayout->addWidget(m_findBar);
    m_findBar->setVisible(false);

    m_tabs->addTab(hexPage, QStringLiteral("Hex"));

    m_infoView = new PackageInfoView(m_tabs);
    m_tabs->addTab(m_infoView, QStringLiteral("Analysis"));
    connect(m_infoView, &PackageInfoView::exportRequested, this,
            &FileEditor::analysisExportRequested);

    // Code tab: a readable listing that can be edited and assembled back. It
    // works for any file, including PS4/PSP/Vita PKG headers and APK/ZIP
    // containers; it does not decode CPU instructions.
    auto* codePage = new QWidget(m_tabs);
    auto* codeLayout = new QVBoxLayout(codePage);
    codeLayout->setContentsMargins(0, 0, 0, 0);
    codeLayout->setSpacing(0);

    auto* codeBar = new QWidget(codePage);
    codeBar->setObjectName(QStringLiteral("fileEditorHeader"));
    auto* codeBarLayout = new QHBoxLayout(codeBar);
    codeBarLayout->setContentsMargins(8, 4, 8, 4);
    codeBarLayout->setSpacing(8);
    auto* codeHint =
        new QLabel(QStringLiteral("Readable listing \u2014 edit the hex column, then assemble."),
                   codeBar);
    codeHint->setObjectName(QStringLiteral("hintLabel"));
    codeBarLayout->addWidget(codeHint, 1);

    auto* reload = makeBarButton(QStringLiteral("Reload listing"),
                                 QStringLiteral("Rebuild the listing from the file"), codeBar);
    codeBarLayout->addWidget(reload);
    auto* assemble = makeBarButton(
        QStringLiteral("Assemble into file"),
        QStringLiteral("Apply the listing as undoable byte edits. Nothing is written to "
                       "disk until you save."),
        codeBar);
    assemble->setObjectName(QStringLiteral("editToggle"));
    codeBarLayout->addWidget(assemble);
    m_listingStatus = new QLabel(codeBar);
    m_listingStatus->setObjectName(QStringLiteral("hintLabel"));
    codeBarLayout->addWidget(m_listingStatus);
    codeLayout->addWidget(codeBar);

    m_listingEdit = new QPlainTextEdit(codePage);
    m_listingEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_listingEdit->setPlaceholderText(
        QStringLiteral("Reload the listing to disassemble the file, or paste lines such as "
                       "\"00000010  90 90\" and assemble them into the file."));
    codeLayout->addWidget(m_listingEdit, 1);
    m_tabs->addTab(codePage, QStringLiteral("Code"));

    connect(reload, &QToolButton::clicked, this, &FileEditor::refreshCodeListing);
    connect(assemble, &QToolButton::clicked, this, [this]() {
        if (m_listingEdit == nullptr || m_document == nullptr)
            return;
        auto parsed = parseListing(m_listingEdit->toPlainText(), m_document->size());
        if (!parsed) {
            m_listingStatus->setText(parsed.error().detail.isEmpty()
                                         ? parsed.error().message
                                         : parsed.error().detail);
            return;
        }
        auto applied = applyByteEdits(parsed.value());
        if (!applied) {
            m_listingStatus->setText(applied.error().detail.isEmpty()
                                         ? applied.error().message
                                         : applied.error().detail);
            return;
        }
        m_listingStatus->setText(QStringLiteral("%1 byte(s) changed \u2014 unsaved")
                                     .arg(applied.value()));
    });

    updateTitleLabel();
    m_titleLabel->setToolTip(m_document->path());
    m_formatLabel->setText(fmt::humanBytes(m_document->size()));

    m_hex->setDocument(m_document);

    m_inspector = new ByteInspector(m_hex);
    m_inspector->setDocument(m_document.get());
    connect(m_hex, &HexView::viewResized, this, &FileEditor::positionInspector);
    connect(m_hex, &HexView::hovered, this, [this](quint64 offset) {
        m_inspector->showOffset(offset);
        positionInspector();
    });
    connect(m_hex, &HexView::hoverCleared, this, [this]() {
        if (m_hex->selectionLength() <= 1)
            m_inspector->clear();
    });
    connect(m_inspector, &ByteInspector::closeRequested, m_inspector, &QWidget::hide);
    connect(m_hex, &HexView::cursorMoved, this, [this](quint64 offset) {
        if (m_hex->selectionLength() <= 1) {
            m_inspector->showOffset(offset);
            positionInspector();
        }
    });
    connect(m_hex, &HexView::selectionChanged, this, [this](quint64 start, quint64 length) {
        if (length > 1) {
            m_inspector->showSelection(start, length);
            positionInspector();
        }
    });

    connect(m_hex, &HexView::cursorMoved, this, &FileEditor::cursorMoved);
    connect(m_hex, &HexView::selectionChanged, this, &FileEditor::selectionChanged);
    connect(m_hex, &HexView::findRequested, this, [this]() { showFindBar(true); });
    connect(m_hex, &HexView::gotoRequested, this, &FileEditor::goToOffsetInteractive);
    connect(m_hex, &HexView::documentEdited, this, &FileEditor::refreshEditState);
    connect(m_editToggle, &QToolButton::toggled, this, [this](bool enabled) {
        setEditingEnabled(enabled);
    });
    connect(m_structureToggle, &QToolButton::toggled, this, &FileEditor::showStructurePanel);
    connect(m_structurePanel, &StructurePanel::fieldSelected, this,
            [this](quint64 offset, quint64 length) {
                m_tabs->setCurrentIndex(0);
                m_hex->goToOffset(offset);
                m_hex->setSelection(offset, length);
                m_hex->setFocus(Qt::OtherFocusReason);
            });
    connect(m_hex, &HexView::cursorMoved, this, [this](quint64 offset) {
        if (m_structurePanel->isVisible())
            m_structurePanel->highlightOffset(offset);
    });
    connect(m_findInput, &QLineEdit::returnPressed, this, [this]() { startSearch(); });
    connect(m_caseBox, &QCheckBox::toggled, this, [this](bool) {
        if (!m_pattern.isEmpty())
            startSearch();
    });
    connect(m_nextButton, &QToolButton::clicked, this, [this]() {
        if (m_hex->searchHitCount() == 0)
            startSearch();
        else if (!m_hex->findNextHit(true))
            m_findStatus->setText(QStringLiteral("no matches"));
    });
    connect(m_previousButton, &QToolButton::clicked, this, [this]() {
        if (m_hex->searchHitCount() == 0)
            startSearch();
        else if (!m_hex->findNextHit(false))
            m_findStatus->setText(QStringLiteral("no matches"));
    });
    connect(closeFind, &QToolButton::clicked, this, [this]() { showFindBar(false); });
    connect(m_replaceAllButton, &QToolButton::clicked, this, &FileEditor::replaceAll);
    connect(m_replaceInput, &QLineEdit::returnPressed, this, &FileEditor::replaceAll);

    m_watcher = std::make_shared<QFutureWatcher<Result<SearchResult>>>();
    connect(m_watcher.get(), &QFutureWatcher<Result<SearchResult>>::finished, this,
            &FileEditor::onSearchFinished);
}

QString FileEditor::path() const
{
    return m_document ? m_document->path() : QString();
}

void FileEditor::updateTitleLabel()
{
    const QString name = QFileInfo(path()).fileName();
    m_titleLabel->setText(isModified() ? name + QStringLiteral(" \u25CF") : name);
}

void FileEditor::positionInspector()
{
    if (m_inspector == nullptr || !m_inspector->isVisible())
        return;
    m_inspector->adjustSize();
    const int margin = 14;
    m_inspector->move(std::max(0, m_hex->width() - m_inspector->width() - margin),
                      std::max(0, m_hex->height() - m_inspector->height() - margin));
    m_inspector->raise();
}

void FileEditor::refreshEditState()
{
    m_hex->notifyDocumentChanged();
    if (m_structurePanel != nullptr)
        m_structurePanel->refresh();
    updateTitleLabel();
    emit modificationChanged(isModified());
}

void FileEditor::showStructurePanel(bool visible)
{
    if (m_structurePanel == nullptr)
        return;
    if (m_structureToggle->isChecked() != visible) {
        const QSignalBlocker blocker(m_structureToggle);
        m_structureToggle->setChecked(visible);
    }
    m_structurePanel->setVisible(visible);
    if (visible) {
        m_structurePanel->refresh();
        m_structurePanel->setFocus(Qt::OtherFocusReason);
    }
}

void FileEditor::toggleStructurePanel()
{
    showStructurePanel(!m_structurePanel->isVisible());
}

void FileEditor::openStructureTemplate()
{
    showStructurePanel(true);
    m_structurePanel->loadTemplateFile();
}

void FileEditor::editStructureTemplate()
{
    showStructurePanel(true);
    m_structurePanel->editTemplate();
}

void FileEditor::setEditingEnabled(bool enabled)
{
    if (m_editToggle->isChecked() != enabled) {
        QSignalBlocker blocker(m_editToggle);
        m_editToggle->setChecked(enabled);
    }
    m_hex->setEditingEnabled(enabled);
    m_editToggle->setText(enabled ? QStringLiteral("Editing on")
                                  : QStringLiteral("Editing off"));
    if (enabled && m_tabs->currentIndex() == 0)
        m_hex->setFocus(Qt::OtherFocusReason);
}

bool FileEditor::isEditingEnabled() const
{
    return m_editToggle != nullptr && m_editToggle->isChecked();
}

bool FileEditor::isModified() const
{
    return m_document != nullptr && m_document->isModified();
}

bool FileEditor::undoEdit()
{
    if (m_document == nullptr || !m_document->undo())
        return false;
    refreshEditState();
    return true;
}

bool FileEditor::redoEdit()
{
    if (m_document == nullptr || !m_document->redo())
        return false;
    refreshEditState();
    return true;
}

Result<quint64> FileEditor::saveTo(const QString& path)
{
    if (m_document == nullptr)
        return makeError(QStringLiteral("No document is open"));
    auto written = m_document->saveTo(path);
    if (!written)
        return written.error();
    const Status reloaded = reloadFrom(path);
    if (!reloaded)
        return reloaded.error();
    return written.value();
}

Status FileEditor::reloadFrom(const QString& path)
{
    if (m_document == nullptr)
        return makeError(QStringLiteral("No document is open"));
    const Status reloaded = m_document->reloadFrom(path);
    if (!reloaded)
        return reloaded;
    m_hex->notifyDocumentChanged();
    if (m_structurePanel != nullptr)
        m_structurePanel->refresh();
    m_formatLabel->setText(fmt::humanBytes(m_document->size()));
    updateTitleLabel();
    m_titleLabel->setToolTip(path);
    emit pathChanged(path);
    emit modificationChanged(false);
    return okStatus();
}

quint64 FileEditor::fileSize() const
{
    return m_document ? m_document->size() : 0;
}

void FileEditor::setAnalysis(const pkg::PackageInfo& info)
{
    m_info = info;
    m_hasAnalysis = true;
    m_formatLabel->setText(info.formatName);
    m_summaryLabel->setText(info.summary);
    m_summaryLabel->setToolTip(info.summary);
    m_infoView->setInfo(info, path(), fileSize());
    m_hex->setSignatures(info.signatures);
    m_tabs->setTabText(1, info.warnings.isEmpty()
                              ? QStringLiteral("Analysis")
                              : QStringLiteral("Analysis (%1)").arg(info.warnings.size()));
}

void FileEditor::setAnalysisError(const QString& message)
{
    m_formatLabel->setText(QStringLiteral("analysis failed"));
    m_summaryLabel->setText(message);
    m_infoView->showPlaceholder(QStringLiteral("Analysis failed"), message);
}

void FileEditor::showAnalysisTab()
{
    m_tabs->setCurrentIndex(1);
}

QString FileEditor::readableListing(quint64 maxBytes) const
{
    if (m_document == nullptr || !m_document->isValid())
        return {};
    const quint64 limit = std::min(maxBytes == 0 ? m_document->size() : maxBytes,
                                   m_document->size());
    auto listing = buildListing(*m_document, 0, limit, 16,
                                QStringLiteral("file: %1 (%2 bytes)")
                                    .arg(m_document->path())
                                    .arg(m_document->size()));
    return listing ? listing.value() : QString();
}

void FileEditor::refreshCodeListing()
{
    if (m_listingEdit == nullptr || m_document == nullptr)
        return;
    // A quarter of a megabyte is enough to see and patch real headers without
    // turning a multi-gigabyte package into a giant text document.
    constexpr quint64 kListingLimit = 256ULL * 1024ULL;
    const quint64 size = m_document->size();
    const quint64 limit = std::min(kListingLimit, size);
    auto listing = buildListing(*m_document, 0, limit, 16,
                                QStringLiteral("file: %1 (%2 bytes); listing covers %3 of %4 bytes")
                                    .arg(m_document->path())
                                    .arg(size)
                                    .arg(limit)
                                    .arg(size));
    if (!listing) {
        m_listingStatus->setText(listing.error().message);
        return;
    }
    m_listingEdit->setPlainText(listing.value());
    m_listingStatus->setText(limit < size
                                 ? QStringLiteral("first %1 bytes (limit)").arg(limit)
                                 : QStringLiteral("%1 bytes").arg(size));
}

Result<int> FileEditor::applyByteEdits(const QVector<pkg::ListingEntry>& entries)
{
    if (m_document == nullptr)
        return makeError(QStringLiteral("No document is open"));
    if (entries.isEmpty())
        return makeError(QStringLiteral("No byte edits were supplied"));
    // Edits must stay visible and reversible in the hex view.
    setEditingEnabled(true);
    auto applied = applyListing(*m_document, entries);
    if (!applied)
        return applied.error();
    refreshEditState();
    return applied.value();
}

void FileEditor::showCodeTab()
{
    m_tabs->setCurrentIndex(2);
}

void FileEditor::showByteInspectorAt(quint64 offset)
{
    if (m_inspector == nullptr)
        return;
    m_tabs->setCurrentIndex(0);
    m_inspector->showOffset(offset);
    positionInspector();
}

QString FileEditor::analysisReport() const
{
    return m_infoView->currentReportText();
}

void FileEditor::setEditorFont(const QFont& font)
{
    m_hex->setEditorFont(font);
}

void FileEditor::setHexLayout(const HexLayout& layout)
{
    m_hex->setLayout(layout);
}

void FileEditor::showFindBar(bool visible)
{
    m_findBar->setVisible(visible);
    if (visible) {
        m_tabs->setCurrentIndex(0);
        m_findInput->setFocus(Qt::ShortcutFocusReason);
        m_findInput->selectAll();
    }
}

void FileEditor::focusFindBar()
{
    showFindBar(true);
}

void FileEditor::goToOffsetInteractive()
{
    bool ok = false;
    const QString text = QInputDialog::getText(
        this, QStringLiteral("Go to offset"),
        QStringLiteral("Offset (decimal or 0x hex):"), QLineEdit::Normal,
        fmt::hexPrefixed(m_hex->cursorOffset()), &ok);
    if (!ok)
        return;
    const auto offset = safe::parseOffset(text);
    if (!offset) {
        m_findStatus->setText(QStringLiteral("invalid offset"));
        return;
    }
    goToOffset(*offset);
}

void FileEditor::goToOffset(quint64 offset)
{
    m_tabs->setCurrentIndex(0);
    m_hex->goToOffset(offset);
    m_hex->setFocus(Qt::OtherFocusReason);
}

void FileEditor::startSearch()
{
    if (m_searchRunning) {
        cancelSearch();
        return;
    }
    const QString raw = m_findInput->text().trimmed();
    if (raw.isEmpty()) {
        m_hex->clearSearchHits();
        m_hits.clear();
        m_pattern.clear();
        m_findStatus->setText(QString());
        return;
    }

    const QByteArray pattern = parsePattern(raw);
    m_pattern = pattern;
    m_hits.clear();
    m_token = CancelToken();
    m_searchRunning = true;
    m_findStatus->setText(QStringLiteral("searching\u2026"));
    m_nextButton->setEnabled(false);
    m_previousButton->setEnabled(false);

    std::shared_ptr<HexDocument> document = m_document;
    const CancelToken token = m_token;
    const bool caseSensitive = m_caseBox->isChecked();
    const QByteArray needle = pattern;

    m_watcher->setFuture(QtConcurrent::run([document, needle, token, caseSensitive]() {
        SearchOptions options;
        options.caseInsensitive = !caseSensitive;
        return searchBytes(document->source(), needle, options, token);
    }));
}

void FileEditor::replaceAll()
{
    if (m_document == nullptr)
        return;
    if (m_pattern.isEmpty() || m_hits.isEmpty()) {
        m_findStatus->setText(QStringLiteral("run a search first"));
        return;
    }

    const QByteArray replacement = parsePattern(m_replaceInput->text());
    if (replacement.size() != m_pattern.size()) {
        m_replaceStatus->setText(
            QStringLiteral("needs %1 byte(s)").arg(m_pattern.size()));
        return;
    }

    const auto answer = QMessageBox::question(
        this, QStringLiteral("Replace all"),
        QStringLiteral("Replace %1 match(es) in memory?\n\nThe file on disk is not changed "
                       "until you save, and each changed byte can be undone.")
            .arg(m_hits.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    const auto replaced = replaceAllEqualLength(*m_document, m_hits, m_pattern, replacement);
    if (!replaced) {
        m_replaceStatus->setText(replaced.error().message);
        return;
    }

    refreshEditState();
    const int count = replaced.value();
    m_hits.clear();
    m_hex->clearSearchHits();
    m_replaceStatus->setText(QStringLiteral("replaced %1 in memory").arg(count));
    m_findStatus->setText(QStringLiteral("save to persist changes"));
}

void FileEditor::cancelSearch()
{
    m_token.cancel();
    m_findStatus->setText(QStringLiteral("cancelling\u2026"));
}

void FileEditor::onSearchFinished()
{
    m_searchRunning = false;
    m_nextButton->setEnabled(true);
    m_previousButton->setEnabled(true);

    const Result<SearchResult> result = m_watcher->result();
    if (!result) {
        if (result.error().message == QLatin1String("Search cancelled")) {
            m_findStatus->setText(QStringLiteral("cancelled"));
            return;
        }
        m_findStatus->setText(result.error().message);
        logWarning(QString::fromLatin1(kLogCategory), result.error().message);
        return;
    }

    const SearchResult& found = result.value();
    m_hits = found.offsets;
    m_hex->setSearchHits(found.offsets, static_cast<int>(m_pattern.size()));
    if (found.offsets.isEmpty()) {
        m_findStatus->setText(QStringLiteral("no matches"));
        return;
    }
    m_findStatus->setText(
        QStringLiteral("%1 match(es)%2")
            .arg(found.offsets.size())
            .arg(found.truncated ? QStringLiteral(" (limit reached)") : QString()));
    m_hex->goToOffset(found.offsets.first());
    m_hex->setFocus(Qt::OtherFocusReason);
}

}  // namespace pkg::ui
