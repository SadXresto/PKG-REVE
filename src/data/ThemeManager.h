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
    /// Sets or clears the wallpaper of `id`, copying the image into the user
    /// theme folder so the theme stays self-contained. `opacity` (20-100)
    /// controls how much of the image shows through the panel colours.
    [[nodiscard]] Status setWallpaper(const QString& id, const QString& imagePath, int opacity);
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
