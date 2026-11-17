#include "ui/CommandPalette.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace pkg::ui {
namespace {

/// -1 when any term is absent. Substring matches score higher than
/// subsequence matches; title hits beat category/description hits.
int scoreEntry(const CommandPalette::Entry& entry, const QStringList& terms)
{
    const QString title = entry.title.toLower();
    const QString category = entry.category.toLower();
    const QString haystack = title + QLatin1Char(' ') + category + QLatin1Char(' ') +
                             entry.description.toLower() + QLatin1Char(' ') +
                             entry.keywords.toLower() + QLatin1Char(' ') +
                             entry.shortcut.toLower();

    int score = 0;
    for (const QString& term : terms) {
        if (term.isEmpty())
            continue;
        if (!haystack.contains(term))
            return -1;
        if (title == term) {
            score += 1200;
        } else if (title.startsWith(term)) {
            score += 900;
        } else if (title.contains(term)) {
            score += 600;
        } else if (category.contains(term)) {
            score += 300;
        } else {
            score += 180;
        }
        if (entry.keywords.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts).contains(term))
            score += 50;
        score += 40;
    }
    return score;
}

}  // namespace

CommandPalette::CommandPalette(QList<Entry> entries, QWidget* parent)
    : QDialog(parent), m_entries(std::move(entries))
{
    setWindowTitle(QStringLiteral("Command center"));
    setModal(true);
    resize(680, 440);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(
        QStringLiteral("Search commands, open files, analysis tools and help\u2026"));
    layout->addWidget(m_filter);

    auto* hint = new QLabel(
        QStringLiteral("Enter runs the selection \u00B7 Up/Down navigates \u00B7 Esc closes"),
        this);
    hint->setObjectName(QStringLiteral("hintLabel"));
    layout->addWidget(hint);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("commandList"));
    m_list->setUniformItemSizes(false);
    m_list->setWordWrap(true);
    layout->addWidget(m_list, 1);

    m_filter->installEventFilter(this);
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

QList<CommandPalette::Entry> CommandPalette::rankEntries(const QList<Entry>& entries,
                                                         const QString& query)
{
    const QString needle = query.trimmed().toLower();
    if (needle.isEmpty())
        return entries;

    const QStringList terms = needle.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    struct Scored {
        const Entry* entry = nullptr;
        int score = 0;
        int order = 0;
    };
    QVector<Scored> scored;
    scored.reserve(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const int score = scoreEntry(entries.at(i), terms);
        if (score >= 0)
            scored.append({&entries.at(i), score, i});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.order < b.order;
    });

    QList<Entry> result;
    result.reserve(scored.size());
    for (const Scored& item : scored)
        result.append(*item.entry);
    return result;
}

void CommandPalette::refilter(const QString& text)
{
    m_list->clear();
    const QList<Entry> matches = rankEntries(m_entries, text);
    for (const Entry& entry : matches) {
        QString detail = entry.description;
        if (detail.isEmpty())
            detail = entry.category;
        if (!entry.category.isEmpty())
            detail += QStringLiteral("  \u00B7  ") + entry.category;
        if (!entry.shortcut.isEmpty())
            detail += QStringLiteral("  \u00B7  ") + entry.shortcut;

        QString label = entry.title;
        if (!detail.isEmpty())
            label += QLatin1Char('\n') + detail;
        auto* item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, entry.id);
        item->setToolTip(entry.title +
                         (entry.description.isEmpty() ? QString()
                                                      : QStringLiteral("\n") + entry.description));
    }
    if (m_list->count() == 0) {
        auto* item = new QListWidgetItem(
            QStringLiteral("No matches. Try 'structure', 'save', 'analysis' or 'help'."), m_list);
        item->setFlags(Qt::NoItemFlags);
    } else {
        m_list->setCurrentRow(0);
    }
}

bool CommandPalette::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_filter && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        const int row = m_list->currentRow();
        switch (key->key()) {
        case Qt::Key_Down:
            m_list->setCurrentRow(std::min(row + 1, m_list->count() - 1));
            return true;
        case Qt::Key_Up:
            m_list->setCurrentRow(std::max(row - 1, 0));
            return true;
        case Qt::Key_PageDown:
            m_list->setCurrentRow(std::min(row + 8, m_list->count() - 1));
            return true;
        case Qt::Key_PageUp:
            m_list->setCurrentRow(std::max(row - 8, 0));
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
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
