#include "ui/FileExplorer.h"

#include "core/Logging.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace pkg::ui {
namespace {
constexpr auto kLogCategory = "explorer";
}

FileExplorer::FileExplorer(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("sideBar"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* headerRow = new QWidget(this);
    headerRow->setObjectName(QStringLiteral("sideBarHeaderRow"));
    auto* headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(8, 4, 4, 4);
    m_header = new QLabel(QStringLiteral("EXPLORER"), headerRow);
    m_header->setObjectName(QStringLiteral("sideBarHeader"));
    headerLayout->addWidget(m_header, 1);

    auto* refreshButton = new QToolButton(headerRow);
    refreshButton->setText(QStringLiteral("\u21BB"));
    refreshButton->setToolTip(QStringLiteral("Refresh tree"));
    refreshButton->setAutoRaise(true);
    connect(refreshButton, &QToolButton::clicked, this, &FileExplorer::refresh);
    headerLayout->addWidget(refreshButton);
    layout->addWidget(headerRow);

    m_model = new QFileSystemModel(this);
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    m_model->setReadOnly(true);

    m_view = new QTreeView(this);
    m_view->setModel(m_model);
    m_view->setHeaderHidden(true);
    m_view->setAnimated(false);
    m_view->setUniformRowHeights(true);
    m_view->setSortingEnabled(false);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    for (int column = 1; column < m_model->columnCount(); ++column)
        m_view->hideColumn(column);
    layout->addWidget(m_view, 1);

    connect(m_view, &QTreeView::activated, this, [this](const QModelIndex& index) {
        const QString path = m_model->filePath(index);
        if (!QFileInfo(path).isFile())
            return;
        logDebug(QString::fromLatin1(kLogCategory), QStringLiteral("Open %1").arg(path));
        emit fileActivated(path);
    });

    connect(m_view, &QTreeView::customContextMenuRequested, this,
            [this](const QPoint& position) {
                const QModelIndex index = m_view->indexAt(position);
                if (!index.isValid())
                    return;
                const QString path = m_model->filePath(index);
                QMenu menu(this);
                QAction* openAction = menu.addAction(QStringLiteral("Open"));
                menu.addSeparator();
                QAction* copyAction = menu.addAction(QStringLiteral("Copy path"));
                QAction* chosen = menu.exec(m_view->viewport()->mapToGlobal(position));
                if (chosen == openAction && QFileInfo(path).isFile())
                    emit fileActivated(path);
                else if (chosen == copyAction)
                    QApplication::clipboard()->setText(path);
            });

    updateHeader();
}

void FileExplorer::setRootPath(const QString& path)
{
    m_root = path;
    if (path.isEmpty()) {
        m_view->setModel(nullptr);
        updateHeader();
        return;
    }
    m_view->setModel(m_model);
    const QString absolute = QDir(path).absolutePath();
    m_model->setRootPath(absolute);
    m_view->setRootIndex(m_model->index(absolute));
    updateHeader();
}

void FileExplorer::refresh()
{
    if (m_root.isEmpty())
        return;
    m_model->setRootPath(QString());
    m_model->setRootPath(QDir(m_root).absolutePath());
    m_view->setRootIndex(m_model->index(QDir(m_root).absolutePath()));
}

void FileExplorer::updateHeader()
{
    if (m_root.isEmpty()) {
        m_header->setText(QStringLiteral("EXPLORER \u2014 no project"));
        m_header->setToolTip(QStringLiteral("Open a folder to browse its files"));
        return;
    }
    m_header->setText(QStringLiteral("EXPLORER \u2014 %1").arg(QFileInfo(m_root).fileName()));
    m_header->setToolTip(m_root);
}

}  // namespace pkg::ui
