#include "ui/AiAssistantDialog.h"

#include "data/Database.h"
#include "data/Settings.h"
#include "ui/FileEditor.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <algorithm>

namespace pkg::ui {
namespace {

QString escapeBlock(const QString& text)
{
    return QStringLiteral("<pre style='white-space: pre-wrap; margin: 0;'>%1</pre>")
        .arg(text.toHtmlEscaped());
}

}  // namespace

AiAssistantDialog::AiAssistantDialog(Database& database, FileEditor* editor, QWidget* parent)
    : QDialog(parent), m_database(database), m_editor(editor)
{
    setWindowTitle(QStringLiteral("PKG Studio AI assistant (local endpoint)"));
    setModal(false);
    resize(820, 720);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto* config = new QGroupBox(QStringLiteral("Provider (OpenAI-compatible, local by default)"),
                                 this);
    auto* configForm = new QFormLayout(config);
    configForm->setContentsMargins(10, 8, 10, 8);
    configForm->setSpacing(6);

    m_enabled = new QCheckBox(QStringLiteral("Enable the assistant"), config);
    configForm->addRow(QString(), m_enabled);

    m_endpoint = new QLineEdit(config);
    m_endpoint->setPlaceholderText(QStringLiteral("http://127.0.0.1:11434/v1"));
    configForm->addRow(QStringLiteral("Endpoint"), m_endpoint);

    m_model = new QLineEdit(config);
    m_model->setPlaceholderText(QStringLiteral("llama3.1"));
    configForm->addRow(QStringLiteral("Model"), m_model);

    auto* keyRow = new QWidget(config);
    auto* keyLayout = new QHBoxLayout(keyRow);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    keyLayout->setSpacing(6);
    m_key = new QLineEdit(keyRow);
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setPlaceholderText(QStringLiteral("Optional for local servers"));
    keyLayout->addWidget(m_key, 1);
    m_clearKeyButton = new QPushButton(QStringLiteral("Forget key"), keyRow);
    keyLayout->addWidget(m_clearKeyButton);
    configForm->addRow(QStringLiteral("API key"), keyRow);

    m_saveKey = new QCheckBox(
        QStringLiteral("Remember the key on this computer (owner-only file, not encrypted)"),
        config);
    m_saveKey->setChecked(true);
    configForm->addRow(QString(), m_saveKey);

    m_includeContext = new QCheckBox(
        QStringLiteral("Include the open file's readable listing (first 8 KiB) in each prompt"),
        config);
    configForm->addRow(QString(), m_includeContext);

    auto* privacy = new QLabel(
        QStringLiteral("Requests go only to the endpoint above. PKG Studio has no relay, no "
                       "telemetry and no account: a loopback endpoint keeps everything on this "
                       "computer."),
        config);
    privacy->setObjectName(QStringLiteral("hintLabel"));
    privacy->setWordWrap(true);
    configForm->addRow(privacy);

    layout->addWidget(config);

    m_chat = new QTextBrowser(this);
    m_chat->setOpenExternalLinks(false);
    layout->addWidget(m_chat, 1);

    m_input = new QPlainTextEdit(this);
    m_input->setPlaceholderText(
        QStringLiteral("Ask about this file, or describe the bytes you want changed\u2026"));
    m_input->setMaximumHeight(110);
    layout->addWidget(m_input);

    auto* actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("hintLabel"));
    actionRow->addWidget(m_status, 1);

    m_applyButton = new QPushButton(QStringLiteral("Apply edits to file"), this);
    m_applyButton->setEnabled(false);
    actionRow->addWidget(m_applyButton);

    m_sendButton = new QPushButton(QStringLiteral("Send"), this);
    m_sendButton->setDefault(true);
    actionRow->addWidget(m_sendButton);
    layout->addLayout(actionRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    m_client = new AiClient(this);
    loadSettings();

    if (m_editor != nullptr) {
        appendMessage(QStringLiteral("File"),
                      QStringLiteral("%1\n%2 bytes open. Ask a question or request a byte-level "
                                     "change; edits are applied to the in-memory document and "
                                     "can be undone with Ctrl+Z until you save.")
                          .arg(m_editor->path())
                          .arg(m_editor->fileSize()));
    } else {
        appendMessage(QStringLiteral("Note"),
                      QStringLiteral("No file is open. Open a file to let the assistant read its "
                                     "listing and apply byte edits."));
    }

    connect(m_sendButton, &QPushButton::clicked, this, &AiAssistantDialog::sendPrompt);
    connect(m_applyButton, &QPushButton::clicked, this, &AiAssistantDialog::applyPendingEdits);
    connect(m_clearKeyButton, &QPushButton::clicked, this, [this]() {
        m_key->clear();
        AiSettingsStore::storeKey(QString());
        m_status->setText(QStringLiteral("Stored key removed"));
    });
    connect(m_enabled, &QCheckBox::toggled, this, [this](bool enabled) {
        m_sendButton->setEnabled(enabled);
        if (!enabled)
            m_status->setText(QStringLiteral("Assistant disabled \u2014 enable it to send"));
    });
    connect(m_endpoint, &QLineEdit::editingFinished, this, [this]() { saveSettings(); });
    connect(m_model, &QLineEdit::editingFinished, this, [this]() { saveSettings(); });
    connect(m_includeContext, &QCheckBox::toggled, this, [this](bool) { saveSettings(); });

    m_sendButton->setEnabled(m_enabled->isChecked());
    if (!m_enabled->isChecked())
        m_status->setText(QStringLiteral("Enable the assistant to send a prompt"));
}

void AiAssistantDialog::loadSettings()
{
    const AiSettingsValues values = AiSettingsStore::load(m_database);
    m_enabled->setChecked(values.enabled);
    m_endpoint->setText(values.endpoint);
    m_model->setText(values.model);
    m_includeContext->setChecked(values.includeFileContext);
    const QString key = AiSettingsStore::loadKey();
    m_key->setText(key);
    m_saveKey->setChecked(!key.isEmpty());
}

void AiAssistantDialog::saveSettings()
{
    AiSettingsValues values;
    values.enabled = m_enabled->isChecked();
    values.endpoint = m_endpoint->text().trimmed();
    values.model = m_model->text().trimmed();
    values.includeFileContext = m_includeContext->isChecked();
    AiSettingsStore::save(m_database, values);

    if (m_saveKey->isChecked())
        AiSettingsStore::storeKey(m_key->text());
}

QString AiAssistantDialog::fileContext() const
{
    if (m_editor == nullptr)
        return {};
    constexpr quint64 kContextLimit = 8ULL * 1024ULL;
    QString listing = m_editor->readableListing(kContextLimit);
    if (listing.isEmpty())
        return {};
    return QStringLiteral("Open file: %1\nSize: %2 bytes\nReadable listing (first %3 bytes, "
                          "offset  hex  ascii):\n%4")
        .arg(m_editor->path())
        .arg(m_editor->fileSize())
        .arg(kContextLimit)
        .arg(listing);
}

void AiAssistantDialog::appendMessage(const QString& who, const QString& text)
{
    m_chat->append(QStringLiteral("<p style='margin-top: 8px;'><b>%1</b></p>%2")
                       .arg(who.toHtmlEscaped(), escapeBlock(text)));
}

void AiAssistantDialog::setBusy(bool busy, const QString& message)
{
    m_sendButton->setEnabled(!busy && m_enabled->isChecked());
    m_status->setText(message);
}

void AiAssistantDialog::sendPrompt()
{
    const QString prompt = m_input->toPlainText().trimmed();
    if (prompt.isEmpty())
        return;

    saveSettings();
    if (!m_enabled->isChecked()) {
        m_status->setText(QStringLiteral("Enable the assistant first"));
        return;
    }
    if (AiSettingsStore::chatCompletionsUrl(m_endpoint->text()).isEmpty()) {
        m_status->setText(QStringLiteral("Set an endpoint and model"));
        return;
    }

    QString outgoing = prompt;
    if (m_includeContext->isChecked()) {
        const QString context = fileContext();
        if (!context.isEmpty())
            outgoing = context + QStringLiteral("\n\nRequest: ") + prompt;
    }

    appendMessage(QStringLiteral("You"), prompt);
    m_input->clear();
    m_history.append(AiMessage{QStringLiteral("user"), outgoing});

    AiSettingsValues values;
    values.enabled = true;
    values.endpoint = m_endpoint->text().trimmed();
    values.model = m_model->text().trimmed();
    values.includeFileContext = m_includeContext->isChecked();

    setBusy(true, QStringLiteral("Waiting for %1\u2026").arg(values.endpoint));
    m_applyButton->setEnabled(false);
    m_pendingEdits.clear();

    m_client->send(values, m_key->text(), m_history, [this](const Result<AiClient::Reply>& reply) {
        setBusy(false, QString());
        if (!reply) {
            const QString detail = reply.error().detail;
            appendMessage(QStringLiteral("Error"),
                          detail.isEmpty() ? reply.error().message
                                           : reply.error().message + QStringLiteral("\n") + detail);
            m_status->setText(QStringLiteral("Request failed"));
            // Drop the unanswered turn so the next prompt is not stacked on a
            // conversation the model never saw.
            if (!m_history.isEmpty())
                m_history.removeLast();
            return;
        }

        appendMessage(QStringLiteral("Assistant"), reply.value().content);
        m_history.append(AiMessage{QStringLiteral("assistant"), reply.value().content});

        // Keep the conversation bounded; the file context is re-attached per turn.
        while (m_history.size() > 24)
            m_history.remove(0);

        const QVector<ListingEntry> edits = extractEdits(reply.value().content);
        if (edits.isEmpty()) {
            m_status->setText(QStringLiteral("No byte edits in this reply"));
            return;
        }
        m_pendingEdits = edits;
        const quint64 bytes = listingByteCount(edits);
        m_applyButton->setEnabled(m_editor != nullptr);
        m_status->setText(QStringLiteral("%1 byte(s) in %2 block(s) \u2014 review, then apply")
                              .arg(bytes)
                              .arg(edits.size()));
    });
}

void AiAssistantDialog::applyPendingEdits()
{
    if (m_editor == nullptr || m_pendingEdits.isEmpty())
        return;
    auto applied = m_editor->applyByteEdits(m_pendingEdits);
    if (!applied) {
        QMessageBox::warning(this, QStringLiteral("Cannot apply the edits"),
                             applied.error().detail.isEmpty() ? applied.error().message
                                                              : applied.error().detail);
        return;
    }
    m_pendingEdits.clear();
    m_applyButton->setEnabled(false);
    m_status->setText(QStringLiteral("%1 byte(s) changed in memory \u2014 undo with Ctrl+Z or "
                                     "save to write them")
                          .arg(applied.value()));
    appendMessage(QStringLiteral("PKG Studio"),
                  QStringLiteral("Applied %1 byte change(s) to %2. They stay in memory and are "
                                 "undoable until you save.")
                      .arg(applied.value())
                      .arg(m_editor->path()));
}

}  // namespace pkg::ui
