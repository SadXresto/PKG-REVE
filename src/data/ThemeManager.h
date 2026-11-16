// Theme loading, persistence and application.
#pragma once

#include "core/Result.h"
#include "data/Theme.h"

#include <QObject>
#include <QString>

class QApplication;

namespace pkg {

class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager& instance();
    ~ThemeManager() override;

    /// Loads built-in themes plus user themes from `userThemeDirectory`.
    void initialize(const QString& userThemeDirectory);

    [[nodiscard]] QList<Theme> themes() const;
    [[nodiscard]] QStringList themeIds() const;
    [[nodiscard]] bool contains(const QString& id) const;
    [[nodiscard]] const Theme& current() const;
    [[nodiscard]] const Theme& fallback() const;

    /// Selects a theme; fails when the id is unknown (current stays unchanged).
    [[nodiscard]] Status setCurrent(const QString& id);
    /// Writes a user theme as JSON with an atomic replace.
    [[nodiscard]] Status saveUserTheme(const Theme& theme);
    [[nodiscard]] Status importThemeFile(const QString& path);
    [[nodiscard]] Status exportThemeFile(const QString& id, const QString& path);
    [[nodiscard]] QString userThemeDirectory() const { return m_userDirectory; }

    /// Applies palette and style sheet to the application.
    static void apply(const Theme& theme, QApplication& app);
    [[nodiscard]] static QString styleSheet(const Theme& theme);

signals:
    void themeChanged(const pkg::Theme& theme);

private:
    ThemeManager();
    void loadUserThemes();

    QHash<QString, Theme> m_themes;
    QString m_currentId;
    QString m_userDirectory;
};

}  // namespace pkg
