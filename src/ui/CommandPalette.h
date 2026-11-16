#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QLineEdit;
class QListWidget;

namespace pkg::ui {

/// VS Code style quick command picker. Executes nothing by itself: the caller
/// reacts to commandChosen().
class CommandPalette : public QDialog {
    Q_OBJECT

public:
    struct Entry {
        QString id;
        QString title;
        QString category;
        QString shortcut;
    };

    explicit CommandPalette(QList<Entry> entries, QWidget* parent = nullptr);

    [[nodiscard]] QString selectedCommandId() const;

signals:
    void commandChosen(const QString& id);

private:
    void refilter(const QString& text);
    void chooseCurrent();

    QList<Entry> m_entries;
    QLineEdit* m_filter = nullptr;
    QListWidget* m_list = nullptr;
};

}  // namespace pkg::ui
