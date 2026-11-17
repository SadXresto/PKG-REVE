#pragma once

#include <QString>
#include <QVector>

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
    [[nodiscard]] static bool showTopic(QWidget* parent, const QString& id);
};

}  // namespace pkg::ui
