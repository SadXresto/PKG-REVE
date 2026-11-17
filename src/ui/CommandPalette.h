#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QLineEdit;
class QListWidget;

namespace pkg::ui {

/// VS Code style quick picker used as the Smart Command Center. It executes
/// nothing itself: the caller reacts to commandChosen() with the entry id.
class CommandPalette : public QDialog {
    Q_OBJECT

public:
    struct Entry {
        QString id;
        QString title;
        QString category;
        QString shortcut;
        QString description;
        /// Extra search terms (synonyms), never displayed.
        QString keywords;
    };

    explicit CommandPalette(QList<Entry> entries, QWidget* parent = nullptr);

    [[nodiscard]] QString selectedCommandId() const;
    /// Pure ranking helper: filters by every whitespace-separated term and
    /// prefers title matches, so it can be unit tested without a widget.
    [[nodiscard]] static QList<Entry> rankEntries(const QList<Entry>& entries,
                                                  const QString& query);

signals:
    void commandChosen(const QString& id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refilter(const QString& text);
    void chooseCurrent();

    QList<Entry> m_entries;
    QLineEdit* m_filter = nullptr;
    QListWidget* m_list = nullptr;
};

}  // namespace pkg::ui
