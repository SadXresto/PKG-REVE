#pragma once

#include "ai/AiClient.h"
#include "core/CodeListing.h"

#include <QVector>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;

namespace pkg {
class Database;
struct LocalModelScan;
}

namespace pkg::ui {

class FileEditor;

/// Docked AI chat, modelled on the chat panels in VS Code and OpenCode: choose a
/// provider (a model already installed on this computer, or any OpenAI-compatible
/// API with a key), choose a model, then talk about the open file. Replies that
/// contain byte edits can be applied to the in-memory document.
class AiChatPanel : public QWidget {
    Q_OBJECT

public:
    using EditorProvider = std::function<FileEditor*()>;

    AiChatPanel(Database& database, EditorProvider editorProvider, QWidget* parent = nullptr);

    void focusInput();
    /// Updates the "context:" line after the active editor changes.
    void refreshContext();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    void loadSettings();
    void saveSettings();
    void applyPreset(int index);
    void refreshModels();
    void scanForLocalModels();
    void applyLocalScan(const LocalModelScan& scan);
    void updatePrivacyHint();
    void sendPrompt();
    void applyPendingEdits();
    void appendMessage(const QString& who, const QString& text, bool isError = false);
    void setBusy(bool busy, const QString& message);
    [[nodiscard]] AiSettingsValues currentValues() const;
    [[nodiscard]] FileEditor* editor() const;
    [[nodiscard]] QString fileContext(FileEditor* editor) const;

    Database& m_database;
    EditorProvider m_editorProvider;
    AiClient* m_client = nullptr;

    QComboBox* m_provider = nullptr;
    QPushButton* m_settingsToggle = nullptr;
    QWidget* m_settingsBox = nullptr;
    QLineEdit* m_endpoint = nullptr;
    QComboBox* m_model = nullptr;
    QPushButton* m_refreshModels = nullptr;
    QPushButton* m_scanLocal = nullptr;
    QLineEdit* m_key = nullptr;
    QPushButton* m_forgetKey = nullptr;
    QCheckBox* m_saveKey = nullptr;
    QCheckBox* m_includeContext = nullptr;
    QLabel* m_context = nullptr;
    QLabel* m_privacy = nullptr;
    QTextBrowser* m_chat = nullptr;
    QPlainTextEdit* m_input = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_sendButton = nullptr;
    QPushButton* m_applyButton = nullptr;

    QVector<AiMessage> m_history;
    QVector<ListingEntry> m_pendingEdits;
    QString m_pendingEditsPath;
    bool m_busy = false;
    bool m_scanning = false;
};

}  // namespace pkg::ui
