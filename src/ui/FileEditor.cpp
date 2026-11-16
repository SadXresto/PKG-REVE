#include "ui/FileEditor.h"

#include "core/FormatUtil.h"
#include "core/Logging.h"
#include "core/SafeMath.h"
#include "ui/PackageInfoView.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

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

    layout->addWidget(header);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    layout->addWidget(m_tabs, 1);

    auto* hexPage = new QWidget(m_tabs);
    auto* hexLayout = new QVBoxLayout(hexPage);
    hexLayout->setContentsMargins(0, 0, 0, 0);
    hexLayout->setSpacing(0);

    m_hex = new HexView(hexPage);
    hexLayout->addWidget(m_hex, 1);

    // Find bar -----------------------------------------------------------
    m_findBar = new QWidget(hexPage);
    m_findBar->setObjectName(QStringLiteral("fileEditorHeader"));
    auto* findLayout = new QHBoxLayout(m_findBar);
    findLayout->setContentsMargins(8, 4, 8, 4);
    findLayout->setSpacing(6);

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

    hexLayout->addWidget(m_findBar);
    m_findBar->setVisible(false);

    m_tabs->addTab(hexPage, QStringLiteral("Hex"));

    m_infoView = new PackageInfoView(m_tabs);
    m_tabs->addTab(m_infoView, QStringLiteral("Analysis"));
    connect(m_infoView, &PackageInfoView::exportRequested, this,
            &FileEditor::analysisExportRequested);

    updateTitleLabel();
    m_titleLabel->setToolTip(m_document->path());
    m_formatLabel->setText(fmt::humanBytes(m_document->size()));

    m_hex->setDocument(m_document);

    connect(m_hex, &HexView::cursorMoved, this, &FileEditor::cursorMoved);
    connect(m_hex, &HexView::selectionChanged, this, &FileEditor::selectionChanged);
    connect(m_hex, &HexView::findRequested, this, [this]() { showFindBar(true); });
    connect(m_hex, &HexView::gotoRequested, this, &FileEditor::goToOffsetInteractive);
    connect(m_hex, &HexView::documentEdited, this, &FileEditor::refreshEditState);
    connect(m_editToggle, &QToolButton::toggled, this, [this](bool enabled) {
        setEditingEnabled(enabled);
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

void FileEditor::refreshEditState()
{
    m_hex->notifyDocumentChanged();
    updateTitleLabel();
    emit modificationChanged(isModified());
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
        m_findStatus->setText(QString());
        return;
    }

    // Accept "AA BB CC" hex notation or plain text.
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

    m_pattern = pattern;
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
