#include "ui/CommandPalette.h"

#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace pkg::ui {

CommandPalette::CommandPalette(QList<Entry> entries, QWidget* parent)
    : QDialog(parent), m_entries(std::move(entries))
{
    setWindowTitle(QStringLiteral("Command palette"));
    setModal(true);
    resize(620, 380);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(QStringLiteral("Type a command name\u2026"));
    layout->addWidget(m_filter);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("commandList"));
    m_list->setUniformItemSizes(false);
    layout->addWidget(m_list, 1);

    connect(m_filter, &QLineEdit::textChanged, this, &CommandPalette::refilter);
    connect(m_filter, &QLineEdit::returnPressed, this, &CommandPalette::chooseCurrent);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem*) {
        chooseCurrent();
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem*) {
        chooseCurrent();
    });

    refilter(QString());
    m_filter->setFocus();
}

void CommandPalette::refilter(const QString& text)
{
    const QString needle = text.trimmed();
    m_list->clear();
    for (const Entry& entry : m_entries) {
        const QString haystack = entry.title + QLatin1Char(' ') + entry.category;
        if (!needle.isEmpty() && !haystack.contains(needle, Qt::CaseInsensitive))
            continue;
        QString label = entry.title;
        if (!entry.category.isEmpty())
            label += QStringLiteral("   [%1]").arg(entry.category);
        if (!entry.shortcut.isEmpty())
            label += QStringLiteral("   %1").arg(entry.shortcut);
        auto* item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, entry.id);
        item->setToolTip(entry.title);
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
}

void CommandPalette::chooseCurrent()
{
    const QString id = selectedCommandId();
    if (id.isEmpty())
        return;
    emit commandChosen(id);
    accept();
}

QString CommandPalette::selectedCommandId() const
{
    const QListWidgetItem* item = m_list->currentItem();
    if (item == nullptr && m_list->count() > 0)
        item = m_list->item(0);
    return item != nullptr ? item->data(Qt::UserRole).toString() : QString();
}

}  // namespace pkg::ui
