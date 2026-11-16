#pragma once

#include "core/CancelToken.h"

#include <QByteArray>
#include <QFutureWatcher>
#include <QString>
#include <QVector>
#include <QWidget>

#include <memory>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace pkg::ui {

struct TextSearchHit {
    QString path;
    quint64 offset = 0;
    QString preview;
};

struct TextSearchOutcome {
    QVector<TextSearchHit> hits;
    int scannedFiles = 0;
    int skippedFiles = 0;
    bool truncated = false;
    QString error;
};

class SearchPanel : public QWidget {
    Q_OBJECT

public:
    explicit SearchPanel(QWidget* parent = nullptr);

    void setRootPath(const QString& path);
    void focusSearch();

signals:
    void hitActivated(const QString& path, quint64 offset);

private:
    void startSearch();
    void cancelSearch();
    void populate(const TextSearchOutcome& outcome);
    void updateRootLabel();

    QLineEdit* m_query = nullptr;
    QCheckBox* m_caseSensitive = nullptr;
    QPushButton* m_actionButton = nullptr;
    QTreeWidget* m_results = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_scope = nullptr;
    QString m_root;
    CancelToken m_token;
    std::shared_ptr<QFutureWatcher<TextSearchOutcome>> m_watcher;
};

}  // namespace pkg::ui
