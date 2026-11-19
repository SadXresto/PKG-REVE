#pragma once

#include <QString>
#include <QVector>

class QDialog;
class QWidget;

namespace pkg::ui {

/// Small offline help viewer used by the Help menu and command palette.
/// Content is compiled in so it works without network access or installed
/// documentation files.
class HelpDialog {
public:
    struct Topic {
        QString id;
        QString title;
        QString description;
        QString html;
    };

    [[nodiscard]] static QVector<Topic> topics();
    /// Builds a topic window without showing it, for tests and the
    /// --screenshot-dialog diagnostic mode. Returns nullptr for unknown ids;
    /// the caller owns the result and it is parented to `parent`.
    [[nodiscard]] static QDialog* createTopicDialog(QWidget* parent, const QString& id);
    [[nodiscard]] static bool showTopic(QWidget* parent, const QString& id);
};

}  // namespace pkg::ui
