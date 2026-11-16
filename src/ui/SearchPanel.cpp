#include "ui/SearchPanel.h"

#include "core/FormatUtil.h"
#include "core/Logging.h"

#include <QCheckBox>
#include <QDirIterator>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace pkg::ui {
namespace {

constexpr auto kLogCategory = "search";
constexpr int kMaxFiles = 400;
constexpr qint64 kMaxFileBytes = 2 * 1024 * 1024;
constexpr int kMaxHits = 300;

bool looksBinary(const QByteArray& head)
{
    const int limit = qMin<qsizetype>(head.size(), 8192);
    for (int i = 0; i < limit; ++i) {
        if (head.at(i) == '\0')
            return true;
    }
    return false;
}

QByteArray foldAscii(const QByteArray& data)
{
    QByteArray folded = data;
    for (qsizetype i = 0; i < folded.size(); ++i) {
        const char c = folded.at(i);
        if (c >= 'A' && c <= 'Z')
            folded[i] = static_cast<char>(c - 'A' + 'a');
    }
    return folded;
}

QString makePreview(const QByteArray& data, qsizetype offset, qsizetype length)
{
    const qsizetype start = qMax<qsizetype>(0, offset - 30);
    const qsizetype end = qMin<qsizetype>(data.size(), offset + length + 30);
    QString text = QString::fromUtf8(data.mid(start, end - start));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    return text.simplified();
}

TextSearchOutcome scanDirectory(const QString& root, const QString& query, bool caseSensitive,
                                const CancelToken& token)
{
    TextSearchOutcome outcome;
    if (root.isEmpty() || query.isEmpty()) {
        outcome.error = QStringLiteral("No project folder is open");
        return outcome;
    }

    const QByteArray pattern = caseSensitive ? query.toUtf8() : foldAscii(query.toUtf8());

    QDirIterator iterator(root,
                          QDir::Files | QDir::NoDotAndDotDot | QDir::Readable,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        if (token.isCancelled()) {
            outcome.error = QStringLiteral("Search cancelled");
            return outcome;
        }
        const QString path = iterator.next();
        if (path.contains(QLatin1String("/.git/")) ||
            path.contains(QLatin1String("/build/")) ||
            path.contains(QLatin1String("/node_modules/"))) {
            continue;
        }
        if (outcome.scannedFiles >= kMaxFiles) {
            outcome.truncated = true;
            break;
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            ++outcome.skippedFiles;
            continue;
        }
        if (file.size() <= 0 || file.size() > kMaxFileBytes) {
            ++outcome.skippedFiles;
            continue;
        }
        QByteArray data = file.readAll();
        if (!caseSensitive)
            data = foldAscii(data);
        if (looksBinary(data)) {
            ++outcome.skippedFiles;
            continue;
        }
        ++outcome.scannedFiles;

        qsizetype index = 0;
        while (index < data.size()) {
            const qsizetype found = data.indexOf(pattern, index);
            if (found < 0)
                break;
            TextSearchHit hit;
            hit.path = path;
            hit.offset = static_cast<quint64>(found);
            hit.preview = makePreview(data, found, pattern.size());
            outcome.hits.append(hit);
            if (outcome.hits.size() >= kMaxHits) {
                outcome.truncated = true;
                return outcome;
            }
            index = found + pattern.size();
        }
    }
    return outcome;
}

}  // namespace

SearchPanel::SearchPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("sideBar"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QLabel(QStringLiteral("SEARCH"), this);
    header->setObjectName(QStringLiteral("sideBarHeader"));
    layout->addWidget(header);

    auto* controls = new QWidget(this);
    auto* controlsLayout = new QVBoxLayout(controls);
    controlsLayout->setContentsMargins(8, 8, 8, 6);
    controlsLayout->setSpacing(6);

    m_query = new QLineEdit(controls);
    m_query->setPlaceholderText(QStringLiteral("Text to find in the project"));
    m_query->setClearButtonEnabled(true);
    controlsLayout->addWidget(m_query);

    auto* row = new QHBoxLayout();
    m_caseSensitive = new QCheckBox(QStringLiteral("Match case"), controls);
    row->addWidget(m_caseSensitive);
    row->addStretch(1);
    m_actionButton = new QPushButton(QStringLiteral("Search"), controls);
    row->addWidget(m_actionButton);
    controlsLayout->addLayout(row);

    m_scope = new QLabel(controls);
    m_scope->setObjectName(QStringLiteral("hintLabel"));
    m_scope->setWordWrap(true);
    controlsLayout->addWidget(m_scope);
    layout->addWidget(controls);

    m_results = new QTreeWidget(this);
    m_results->setHeaderHidden(true);
    m_results->setUniformRowHeights(true);
    m_results->setAlternatingRowColors(false);
    layout->addWidget(m_results, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("sideBarHint"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    m_watcher = std::make_shared<QFutureWatcher<TextSearchOutcome>>();

    connect(m_actionButton, &QPushButton::clicked, this, [this]() {
        if (m_watcher->isRunning())
            cancelSearch();
        else
            startSearch();
    });
    connect(m_query, &QLineEdit::returnPressed, this, [this]() { startSearch(); });
    connect(m_watcher.get(), &QFutureWatcher<TextSearchOutcome>::finished, this, [this]() {
        m_actionButton->setText(QStringLiteral("Search"));
        const TextSearchOutcome outcome = m_watcher->result();
        if (!outcome.error.isEmpty() && outcome.hits.isEmpty()) {
            m_status->setText(outcome.error);
            logWarning(QString::fromLatin1(kLogCategory), outcome.error);
            return;
        }
        populate(outcome);
    });
    connect(m_results, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem* item, int) {
                if (item == nullptr || item->parent() == nullptr)
                    return;
                emit hitActivated(item->data(0, Qt::UserRole).toString(),
                                  item->data(0, Qt::UserRole + 1).toULongLong());
            });
    connect(m_results, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (item != nullptr && item->parent() != nullptr) {
                    emit hitActivated(item->data(0, Qt::UserRole).toString(),
                                      item->data(0, Qt::UserRole + 1).toULongLong());
                }
            });

    updateRootLabel();
}

void SearchPanel::setRootPath(const QString& path)
{
    m_root = path;
    updateRootLabel();
}

void SearchPanel::focusSearch()
{
    m_query->setFocus(Qt::ShortcutFocusReason);
    m_query->selectAll();
}

void SearchPanel::updateRootLabel()
{
    if (m_root.isEmpty()) {
        m_scope->setText(QStringLiteral("Open a project folder to search its files."));
        m_actionButton->setEnabled(false);
        return;
    }
    m_actionButton->setEnabled(true);
    m_scope->setText(QStringLiteral("Scope: %1").arg(m_root));
}

void SearchPanel::startSearch()
{
    if (m_root.isEmpty())
        return;
    const QString query = m_query->text();
    if (query.trimmed().isEmpty()) {
        m_status->setText(QStringLiteral("Enter text to search for."));
        return;
    }
    if (m_watcher->isRunning())
        return;

    m_results->clear();
    m_status->setText(QStringLiteral("Searching\u2026"));
    m_token = CancelToken();
    m_actionButton->setText(QStringLiteral("Cancel"));
    const bool caseSensitive = m_caseSensitive->isChecked();
    const QString root = m_root;
    const CancelToken token = m_token;

    m_watcher->setFuture(QtConcurrent::run([root, query, caseSensitive, token]() {
        return scanDirectory(root, query, caseSensitive, token);
    }));
}

void SearchPanel::cancelSearch()
{
    m_token.cancel();
    m_status->setText(QStringLiteral("Cancelling\u2026"));
}

void SearchPanel::populate(const TextSearchOutcome& outcome)
{
    m_results->clear();
    QString currentFile;
    QTreeWidgetItem* fileItem = nullptr;

    for (const TextSearchHit& hit : outcome.hits) {
        if (hit.path != currentFile) {
            currentFile = hit.path;
            fileItem = new QTreeWidgetItem(m_results);
            fileItem->setText(0, QDir(m_root).relativeFilePath(hit.path));
            fileItem->setToolTip(0, hit.path);
            fileItem->setExpanded(true);
        }
        if (fileItem == nullptr)
            continue;
        auto* item = new QTreeWidgetItem(fileItem);
        item->setText(0, QStringLiteral("%1  %2")
                             .arg(fmt::hexPrefixed(hit.offset, 8), hit.preview));
        item->setData(0, Qt::UserRole, hit.path);
        item->setData(0, Qt::UserRole + 1, QVariant::fromValue<qulonglong>(hit.offset));
    }

    QString status = QStringLiteral("%1 match(es) in %2 file(s)")
                         .arg(outcome.hits.size())
                         .arg(outcome.scannedFiles);
    if (outcome.skippedFiles > 0)
        status += QStringLiteral(", %1 skipped").arg(outcome.skippedFiles);
    if (outcome.truncated)
        status += QStringLiteral(" \u2014 result limit reached");
    m_status->setText(status);
    logInfo(QString::fromLatin1(kLogCategory), status);
}

}  // namespace pkg::ui
