#include "ui/PatchReviewDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace pkg::ui {
namespace {

QStringList splitLines(const QByteArray& data)
{
    QString text = QString::fromUtf8(data);
    if (text.isEmpty())
        return {};
    return text.split(QLatin1Char('\n'));
}

}  // namespace

PatchReviewDialog::PatchReviewDialog(const QString& path, const QByteArray& before,
                                     const QByteArray& after, QWidget* parent)
    : QDialog(parent), m_proposal(after)
{
    setObjectName(QStringLiteral("patchReviewDialog"));
    setWindowTitle(QStringLiteral("Review the proposed patch"));
    resize(900, 640);
    buildUi(path, before, after);
}

bool PatchReviewDialog::canApplyInPlace(const QByteArray& before, const QByteArray& after)
{
    return before.size() == after.size() && !after.isEmpty();
}

QString PatchReviewDialog::diffText(const QByteArray& before, const QByteArray& after,
                                    int contextLines)
{
    const QStringList left = splitLines(before);
    const QStringList right = splitLines(after);
    constexpr int kMaxLines = 1200;
    if (left.size() > kMaxLines || right.size() > kMaxLines) {
        return QStringLiteral("The file is too large for a line diff (%1 \u2192 %2 lines). "
                              "Only a summary is shown; review the saved proposal instead.")
            .arg(left.size())
            .arg(right.size());
    }

    // Classic LCS table; bounded by kMaxLines so memory stays modest.
    const int rows = static_cast<int>(left.size()) + 1;
    const int columns = static_cast<int>(right.size()) + 1;
    QVector<QVector<int>> lcs(rows, QVector<int>(columns, 0));
    for (int i = 1; i < rows; ++i) {
        for (int j = 1; j < columns; ++j) {
            if (left.at(i - 1) == right.at(j - 1))
                lcs[i][j] = lcs[i - 1][j - 1] + 1;
            else
                lcs[i][j] = std::max(lcs[i - 1][j], lcs[i][j - 1]);
        }
    }

    enum class Op { Same, Removed, Added };
    QVector<QPair<Op, QString>> operations;
    int i = static_cast<int>(left.size());
    int j = static_cast<int>(right.size());
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && left.at(i - 1) == right.at(j - 1)) {
            operations.append({Op::Same, left.at(i - 1)});
            --i;
            --j;
        } else if (j > 0 && (i == 0 || lcs[i][j - 1] >= lcs[i - 1][j])) {
            operations.append({Op::Added, right.at(j - 1)});
            --j;
        } else {
            operations.append({Op::Removed, left.at(i - 1)});
            --i;
        }
    }
    std::reverse(operations.begin(), operations.end());

    QStringList output;
    int leftLine = 1;
    int rightLine = 1;
    int unchangedRun = 0;
    output.append(QStringLiteral("--- current (%1 lines)").arg(left.size()));
    output.append(QStringLiteral("+++ proposal (%1 lines)").arg(right.size()));
    for (int index = 0; index < operations.size(); ++index) {
        const Op op = operations.at(index).first;
        const QString& line = operations.at(index).second;
        if (op == Op::Same) {
            ++unchangedRun;
            const bool beforeChange = index + 1 < operations.size()
                && operations.at(index + 1).first != Op::Same;
            const bool afterChange = index > 0 && operations.at(index - 1).first != Op::Same;
            if (unchangedRun <= contextLines || beforeChange || afterChange) {
                output.append(QStringLiteral(" %1").arg(line));
            } else if (unchangedRun == contextLines + 1) {
                output.append(QStringLiteral("\u2026"));
            }
            ++leftLine;
            ++rightLine;
            continue;
        }
        unchangedRun = 0;
        if (op == Op::Removed) {
            output.append(QStringLiteral("-%1").arg(line));
            ++leftLine;
        } else {
            output.append(QStringLiteral("+%1").arg(line));
            ++rightLine;
        }
        if (index + 1 < operations.size() && operations.at(index + 1).first == Op::Same)
            unchangedRun = 0;
    }
    Q_UNUSED(leftLine)
    Q_UNUSED(rightLine)
    return output.join(QLatin1Char('\n'));
}

void PatchReviewDialog::buildUi(const QString& path, const QByteArray& before,
                                const QByteArray& after)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 10);
    layout->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("<b>%1</b>").arg(path.toHtmlEscaped()), this);
    title->setWordWrap(true);
    layout->addWidget(title);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setObjectName(QStringLiteral("hintLabel"));
    const bool inPlace = canApplyInPlace(before, after);
    QString summary = QStringLiteral("%1 \u2192 %2 bytes")
                          .arg(before.size())
                          .arg(after.size());
    if (inPlace)
        summary += QStringLiteral(" \u00B7 same size, so it can be applied as undoable byte "
                                  "edits");
    else
        summary += QStringLiteral(" \u00B7 the proposal changes the file size. This build edits "
                                  "existing bytes only, so it cannot be applied in place; save it "
                                  "as a separate file instead.");
    m_summary->setText(summary);
    layout->addWidget(m_summary);

    m_diff = new QPlainTextEdit(this);
    m_diff->setReadOnly(true);
    m_diff->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(9);
    m_diff->setFont(mono);
    m_diff->setPlainText(diffText(before, after));
    layout->addWidget(m_diff, 1);

    auto* buttons = new QDialogButtonBox(this);
    m_apply = new QPushButton(QStringLiteral("Apply to editor"), this);
    m_apply->setEnabled(inPlace);
    m_apply->setToolTip(inPlace ? QStringLiteral("Applies the reviewed bytes as undoable edits")
                                : QStringLiteral("Cannot apply: the proposal changes the file "
                                                 "size"));
    m_save = new QPushButton(QStringLiteral("Save proposal\u2026"), this);
    auto* cancel = new QPushButton(QStringLiteral("Discard"), this);
    buttons->addButton(m_apply, QDialogButtonBox::AcceptRole);
    buttons->addButton(m_save, QDialogButtonBox::ActionRole);
    buttons->addButton(cancel, QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);

    connect(m_apply, &QPushButton::clicked, this, [this]() {
        m_approved = true;
        accept();
    });
    connect(m_save, &QPushButton::clicked, this, [this]() { saveProposal(m_proposal); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void PatchReviewDialog::saveProposal(const QByteArray& after)
{
    const QString suggested = QDir::homePath() + QStringLiteral("/pkgstudio-proposal.bin");
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save the proposed file"), suggested,
        QStringLiteral("All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("Cannot write the proposal"), path);
        return;
    }
    file.write(after);
    file.close();
    QMessageBox::information(this, QStringLiteral("Proposal saved"),
                             QStringLiteral("Written to %1. The open document was not "
                                            "modified.").arg(path));
}

}  // namespace pkg::ui
