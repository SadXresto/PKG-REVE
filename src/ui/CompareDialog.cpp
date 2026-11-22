#include "ui/CompareDialog.h"

#include "core/FormatUtil.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace pkg::ui {

CompareDialog::CompareDialog(const QString& basePath, QWidget* parent)
    : QDialog(parent), m_basePath(basePath)
{
    setWindowTitle(QStringLiteral("Compare files"));
    setModal(true);
    resize(680, 460);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto* base = new QLabel(QStringLiteral("Comparing with: %1").arg(basePath), this);
    base->setObjectName(QStringLiteral("hintLabel"));
    base->setWordWrap(true);
    base->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(base);

    auto* row = new QHBoxLayout();
    m_path = new QLineEdit(this);
    m_path->setPlaceholderText(QStringLiteral("Path of the file to compare against"));
    row->addWidget(m_path, 1);
    auto* browse = new QPushButton(QStringLiteral("Browse…"), this);
    row->addWidget(browse);
    layout->addLayout(row);

    auto* buttons = new QHBoxLayout();
    m_compare = new QPushButton(QStringLiteral("Compare"), this);
    m_compare->setDefault(true);
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    buttons->addWidget(m_compare);
    buttons->addWidget(close);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    m_summary = new QLabel(
        QStringLiteral("Pick a second file. Comparison reads both files in bounded chunks."), this);
    m_summary->setObjectName(QStringLiteral("hintLabel"));
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    m_runs = new QTableWidget(this);
    m_runs->setColumnCount(4);
    m_runs->setHorizontalHeaderLabels({QStringLiteral("Offset"), QStringLiteral("Length"),
                                       QStringLiteral("Kind"), QStringLiteral("Bytes")});
    m_runs->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_runs->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_runs->setAlternatingRowColors(true);
    m_runs->verticalHeader()->setVisible(false);
    m_runs->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_runs, 1);

    auto* hint = new QLabel(
        QStringLiteral("Double-click a row to jump to that offset in the hex view."), this);
    hint->setObjectName(QStringLiteral("hintLabel"));
    layout->addWidget(hint);

    m_watcher = std::make_shared<QFutureWatcher<Result<CompareResult>>>();
    connect(m_watcher.get(), &QFutureWatcher<Result<CompareResult>>::finished, this,
            &CompareDialog::onFinished);
    connect(browse, &QPushButton::clicked, this, &CompareDialog::chooseFile);
    connect(m_compare, &QPushButton::clicked, this, &CompareDialog::startCompare);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_runs, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const QTableWidgetItem* item = m_runs->item(row, 0);
        if (item == nullptr)
            return;
        emit offsetActivated(item->data(Qt::UserRole).toULongLong());
        accept();
    });
}

void CompareDialog::chooseFile()
{
    const QString start = QFileInfo(m_basePath).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Choose a file to compare"), start, QStringLiteral("All files (*)"));
    if (!path.isEmpty())
        m_path->setText(path);
}

void CompareDialog::startCompare()
{
    if (m_running)
        return;
    const QString other = m_path->text().trimmed();
    if (other.isEmpty()) {
        m_summary->setText(QStringLiteral("Choose a file first."));
        return;
    }
    if (!QFileInfo(other).isFile()) {
        m_summary->setText(QStringLiteral("File not found: %1").arg(other));
        return;
    }

    m_running = true;
    m_compare->setEnabled(false);
    m_runs->setRowCount(0);
    m_summary->setText(QStringLiteral("Comparing…"));
    m_token = CancelToken();
    const QString base = m_basePath;
    m_watcher->setFuture(QtConcurrent::run([base, other, token = m_token]() {
        return compareFiles(base, other, 64ULL * 1024ULL * 1024ULL, token);
    }));
}

void CompareDialog::cancelCompare()
{
    m_token.cancel();
}

void CompareDialog::onFinished()
{
    m_running = false;
    m_compare->setEnabled(true);
    const auto result = m_watcher->result();
    if (!result) {
        m_summary->setText(result.error().detail.isEmpty()
                               ? result.error().message
                               : QStringLiteral("%1: %2").arg(result.error().message,
                                                              result.error().detail));
        return;
    }
    populate(result.value());
}

void CompareDialog::populate(const CompareResult& result)
{
    QString summary;
    if (result.identical()) {
        summary = QStringLiteral("Files are identical (%1 compared).")
                      .arg(fmt::humanBytes(result.comparedBytes));
    } else {
        summary = QStringLiteral("%1 differing byte(s) at %2 offset(s) \\u00B7 %3 compared \\u00B7 sizes %4 / %5")
                      .arg(result.differingBytes)
                      .arg(result.runs.size())
                      .arg(fmt::humanBytes(result.comparedBytes),
                           fmt::humanBytes(result.sizeA), fmt::humanBytes(result.sizeB));
        if (result.truncated)
            summary += QStringLiteral(" \\u00B7 comparison stopped at the 64 MiB budget");
    }
    m_summary->setText(summary);

    m_runs->setRowCount(static_cast<int>(result.runs.size()));
    for (int row = 0; row < static_cast<int>(result.runs.size()); ++row) {
        const DiffRun& run = result.runs.at(row);
        auto* offset = new QTableWidgetItem(
            QStringLiteral("0x%1").arg(run.offset, 0, 16));
        offset->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(run.offset));
        m_runs->setItem(row, 0, offset);
        m_runs->setItem(row, 1, new QTableWidgetItem(QString::number(run.length)));
        m_runs->setItem(row, 2,
                        new QTableWidgetItem(run.onlyInOne
                                                 ? QStringLiteral("Only in one file")
                                                 : QStringLiteral("Different bytes")));
        m_runs->setItem(row, 3, new QTableWidgetItem(QStringLiteral("\\u2014")));
    }
    if (result.runs.isEmpty())
        m_runs->setRowCount(0);
}

}  // namespace pkg::ui
