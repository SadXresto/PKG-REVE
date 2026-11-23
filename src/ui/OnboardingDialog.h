// First-launch guide: a short, honest tour of what this build does, with the
// sample project and help topics one click away.
#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;
class QStackedWidget;

namespace pkg::ui {

class OnboardingDialog : public QDialog {
    Q_OBJECT

public:
    explicit OnboardingDialog(const QString& version, QWidget* parent = nullptr);

    /// True when the user kept "Show this guide on first launch" checked.
    [[nodiscard]] bool showOnFirstLaunch() const;

signals:
    void sampleProjectRequested();
    void gettingStartedRequested();

private:
    void buildUi(const QString& version);
    void updatePage();
    void next();

    QStackedWidget* m_pages = nullptr;
    QLabel* m_step = nullptr;
    QPushButton* m_back = nullptr;
    QPushButton* m_next = nullptr;
    QCheckBox* m_showAgain = nullptr;
    int m_index = 0;
};

}  // namespace pkg::ui
