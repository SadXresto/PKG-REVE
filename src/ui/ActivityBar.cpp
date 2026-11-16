#include "ui/ActivityBar.h"

#include <QButtonGroup>
#include <QToolButton>
#include <QVBoxLayout>

namespace pkg::ui {

ActivityBar::ActivityBar(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("activityBar"));
    setFixedWidth(46);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 4, 0, 4);
    m_layout->setSpacing(2);
    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
}

void ActivityBar::addItem(const QString& id, const QString& label, IconKind kind, bool checkable)
{
    auto* button = new QToolButton(this);
    button->setObjectName(QStringLiteral("activityButton"));
    button->setToolTip(label);
    button->setAccessibleName(label);
    button->setCheckable(checkable);
    button->setAutoRaise(true);
    button->setIconSize(QSize(20, 20));
    button->setFixedSize(44, 40);
    button->setIcon(makeIcon(kind, m_iconColor, 20));
    connect(button, &QToolButton::clicked, this, [this, id]() {
        emit activated(id);
    });
    m_buttons.insert(id, button);
    if (checkable)
        m_group->addButton(button);
    m_layout->addWidget(button);
}

void ActivityBar::addStretch()
{
    m_layout->addStretch(1);
}

void ActivityBar::setCurrent(const QString& id)
{
    const auto it = m_buttons.constFind(id);
    if (it != m_buttons.constEnd() && it.value()->isCheckable())
        it.value()->setChecked(true);
}

QString ActivityBar::currentId() const
{
    const auto* checked = m_group->checkedButton();
    for (auto it = m_buttons.constBegin(); it != m_buttons.constEnd(); ++it) {
        if (it.value() == checked)
            return it.key();
    }
    return {};
}

void ActivityBar::setIconColor(const QColor& color)
{
    m_iconColor = color;
    // Icon kinds are recreated from the button tooltip order, so keep a small
    // mapping of the icons we created.
    static const QHash<QString, IconKind> kinds = {
        {QStringLiteral("explorer"), IconKind::Explorer},
        {QStringLiteral("search"), IconKind::Search},
        {QStringLiteral("recent"), IconKind::Recent},
        {QStringLiteral("settings"), IconKind::Settings},
        {QStringLiteral("themes"), IconKind::Palette},
        {QStringLiteral("diagnostics"), IconKind::Diagnostics},
    };
    for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it) {
        const auto kind = kinds.constFind(it.key());
        if (kind != kinds.constEnd())
            it.value()->setIcon(makeIcon(kind.value(), color, 20));
    }
}

}  // namespace pkg::ui
