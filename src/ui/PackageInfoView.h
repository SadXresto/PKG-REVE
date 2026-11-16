#pragma once

#include "core/FormatInfo.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTextBrowser;

namespace pkg::ui {

/// Rich presentation of the analysis result for a file.
class PackageInfoView : public QWidget {
    Q_OBJECT

public:
    explicit PackageInfoView(QWidget* parent = nullptr);

    void setInfo(const pkg::PackageInfo& info, const QString& filePath, quint64 fileSize);
    void showPlaceholder(const QString& title, const QString& message);
    [[nodiscard]] QString currentReportText() const { return m_report; }

signals:
    void exportRequested();

private:
    QLabel* m_title = nullptr;
    QTextBrowser* m_browser = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPushButton* m_exportButton = nullptr;
    QString m_report;
};

}  // namespace pkg::ui
