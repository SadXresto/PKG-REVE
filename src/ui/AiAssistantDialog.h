#pragma once

#include "ai/AiClient.h"
#include "core/CodeListing.h"

#include <QDialog>
#include <QVector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;

namespace pkg {
class Database;
}

namespace pkg::ui {

class FileEditor;

/// Chat window for the local AI assistant. The endpoint, model and key are
/// configured here and stored locally. Replies that contain byte edits can be
/// applied to the open file, where they stay undoable until the user saves.
class AiAssistantDialog : public QDialog {
    Q_OBJECT

public:
    AiAssistantDialog(Database& database, FileEditor* editor, QWidget* parent = nullptr);

private:
    void loadSettings();
    void saveSettings();
    void sendPrompt();
    void applyPendingEdits();
    void appendMessage(const QString& who, const QString& text);
    void setBusy(bool busy, const QString& message);
    [[nodiscard]] QString fileContext() const;

    Database& m_database;
    FileEditor* m_editor = nullptr;

    QLineEdit* m_endpoint = nullptr;
    QLineEdit* m_model = nullptr;
    QLineEdit* m_key = nullptr;
    QCheckBox* m_enabled = nullptr;
    QCheckBox* m_includeContext = nullptr;
    QCheckBox* m_saveKey = nullptr;
    QTextBrowser* m_chat = nullptr;
    QPlainTextEdit* m_input = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_sendButton = nullptr;
    QPushButton* m_applyButton = nullptr;
    QPushButton* m_clearKeyButton = nullptr;

    AiClient* m_client = nullptr;
    QVector<AiMessage> m_history;
    QVector<ListingEntry> m_pendingEdits;
};

}  // namespace pkg::ui
