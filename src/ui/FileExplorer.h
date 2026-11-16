#pragma once

#include <QWidget>

class QFileSystemModel;
class QLabel;
class QTreeView;

namespace pkg::ui {

class FileExplorer : public QWidget {
    Q_OBJECT

public:
    explicit FileExplorer(QWidget* parent = nullptr);

    void setRootPath(const QString& path);
    [[nodiscard]] QString rootPath() const { return m_root; }
    void refresh();

signals:
    void fileActivated(const QString& path);

private:
    void updateHeader();

    QLabel* m_header = nullptr;
    QTreeView* m_view = nullptr;
    QFileSystemModel* m_model = nullptr;
    QString m_root;
};

}  // namespace pkg::ui
