#pragma once

#include "ui/Icons.h"

#include <QHash>
#include <QWidget>

class QButtonGroup;
class QToolButton;
class QVBoxLayout;

namespace pkg::ui {

class ActivityBar : public QWidget {
    Q_OBJECT

public:
    explicit ActivityBar(QWidget* parent = nullptr);

    void addItem(const QString& id, const QString& label, IconKind kind, bool checkable = true);
    void addStretch();
    void setCurrent(const QString& id);
    [[nodiscard]] QString currentId() const;
    void setIconColor(const QColor& color);

signals:
    void activated(const QString& id);

private:
    QVBoxLayout* m_layout = nullptr;
    QButtonGroup* m_group = nullptr;
    QHash<QString, QToolButton*> m_buttons;
    QColor m_iconColor;
};

}  // namespace pkg::ui
