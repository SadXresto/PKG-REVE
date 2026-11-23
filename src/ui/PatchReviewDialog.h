// Before/after review for an AI patch proposal. Nothing is applied from here:
// the dialog only decides whether the caller may apply the reviewed bytes.
#pragma once

#include <QByteArray>
#include <QDialog>
#include <QString>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace pkg::ui {

class PatchReviewDialog : public QDialog {
    Q_OBJECT

public:
    PatchReviewDialog(const QString& path, const QByteArray& before, const QByteArray& after,
                      QWidget* parent = nullptr);

    /// True when the user approved applying the proposal.
    [[nodiscard]] bool approved() const { return m_approved; }
    /// True when the proposal can be applied to the existing bytes. This build
    /// edits bytes in place, so a size change cannot be applied to the document.
    [[nodiscard]] static bool canApplyInPlace(const QByteArray& before, const QByteArray& after);
    /// Line diff for display and tests. Text is compared as UTF-8 text.
    [[nodiscard]] static QString diffText(const QByteArray& before, const QByteArray& after,
                                          int contextLines = 3);

private:
    void buildUi(const QString& path, const QByteArray& before, const QByteArray& after);
    void saveProposal(const QByteArray& after);

    QPlainTextEdit* m_diff = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_apply = nullptr;
    QPushButton* m_save = nullptr;
    bool m_approved = false;
    QByteArray m_proposal;
};

}  // namespace pkg::ui
