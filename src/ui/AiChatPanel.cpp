#include "ui/AiChatPanel.h"

#include "data/Database.h"
#include "data/Settings.h"
#include "ui/FileEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <utility>

namespace pkg::ui {
namespace {

QString escapeBlock(const QString& text)
{
    return QStringLiteral("<pre style='white-space: pre-wrap; margin: 0;'>%1</pre>")
        .arg(text.toHtmlEscaped());
}

}  // namespace

AiChatPanel::AiChatPanel(Database& database, EditorProvider editorProvider, QWidget* parent)
    : QWidget(parent), m_database(database), m_editorProvider(std::move(editorProvider))
{
    setObjectName(QStringLiteral("aiChatPanel"));
    buildUi();
    m_client = new AiClient(this);
    loadSettings();
    refreshContext();

    appendMessage(QStringLiteral("PKG Studio"),
                  QStringLiteral("Pick a model under Provider settings \u2014 one already running on "
                                 "this computer (Ollama, LM Studio, llama.cpp) or an API key \u2014 "
                                 "then ask about the open file. Nothing is sent until you press "
                                 "Send, and a loopback endpoint keeps every request on this "
                                 "machine."));
}

void AiChatPanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* providerRow = new QHBoxLayout();
    providerRow->setContentsMargins(0, 0, 0, 0);
    providerRow->setSpacing(6);
    m_provider = new QComboBox(this);
    m_provider->setObjectName(QStringLiteral("aiProviderCombo"));
    for (const AiProviderPreset& preset : providerPresets())
        m_provider->addItem(preset.name, preset.id);
    providerRow->addWidget(m_provider, 1);

    m_settingsToggle = new QPushButton(QStringLiteral("Provider settings"), this);
    m_settingsToggle->setCheckable(true);
    m_settingsToggle->setChecked(true);
    providerRow->addWidget(m_settingsToggle);
    layout->addLayout(providerRow);

    m_context = new QLabel(this);
    m_context->setObjectName(QStringLiteral("hintLabel"));
    m_context->setWordWrap(true);
    layout->addWidget(m_context);

    m_settingsBox = new QWidget(this);
    auto* settingsLayout = new QVBoxLayout(m_settingsBox);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(4);

    auto* endpointRow = new QHBoxLayout();
    endpointRow->setContentsMargins(0, 0, 0, 0);
    endpointRow->setSpacing(6);
    auto* endpointCaption = new QLabel(QStringLiteral("Endpoint"), m_settingsBox);
    endpointRow->addWidget(endpointCaption);
    m_endpoint = new QLineEdit(m_settingsBox);
    m_endpoint->setPlaceholderText(QStringLiteral("http://127.0.0.1:11434/v1"));
    endpointRow->addWidget(m_endpoint, 1);
    settingsLayout->addLayout(endpointRow);

    auto* modelRow = new QHBoxLayout();
    modelRow->setContentsMargins(0, 0, 0, 0);
    modelRow->setSpacing(6);
    auto* modelCaption = new QLabel(QStringLiteral("Model"), m_settingsBox);
    modelRow->addWidget(modelCaption);
    m_model = new QComboBox(m_settingsBox);
    m_model->setEditable(true);
    m_model->setInsertPolicy(QComboBox::NoInsert);
    m_model->setPlaceholderText(QStringLiteral("llama3.1"));
    modelRow->addWidget(m_model, 1);
    m_refreshModels = new QPushButton(QStringLiteral("List models"), m_settingsBox);
    modelRow->addWidget(m_refreshModels);
    settingsLayout->addLayout(modelRow);

    auto* keyRow = new QHBoxLayout();
    keyRow->setContentsMargins(0, 0, 0, 0);
    keyRow->setSpacing(6);
    auto* keyCaption = new QLabel(QStringLiteral("API key"), m_settingsBox);
    keyRow->addWidget(keyCaption);
    m_key = new QLineEdit(m_settingsBox);
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setPlaceholderText(QStringLiteral("Not needed for a local server"));
    keyRow->addWidget(m_key, 1);
    m_forgetKey = new QPushButton(QStringLiteral("Forget"), m_settingsBox);
    keyRow->addWidget(m_forgetKey);
    settingsLayout->addLayout(keyRow);

    m_saveKey = new QCheckBox(
        QStringLiteral("Remember the key on this computer (owner-only file, not encrypted)"),
        m_settingsBox);
    m_saveKey->setChecked(true);
    settingsLayout->addWidget(m_saveKey);

    m_includeContext = new QCheckBox(
        QStringLiteral("Include the open file's readable listing (first 8 KiB)"), m_settingsBox);
    settingsLayout->addWidget(m_includeContext);

    m_privacy = new QLabel(m_settingsBox);
    m_privacy->setObjectName(QStringLiteral("hintLabel"));
    m_privacy->setWordWrap(true);
    settingsLayout->addWidget(m_privacy);

    layout->addWidget(m_settingsBox);

    m_chat = new QTextBrowser(this);
    m_chat->setObjectName(QStringLiteral("aiTranscript"));
    m_chat->setOpenExternalLinks(false);
    layout->addWidget(m_chat, 1);

    m_input = new QPlainTextEdit(this);
    m_input->setPlaceholderText(
        QStringLiteral("Ask about this file, or describe the bytes to change\u2026 "
                       "(Enter sends, Shift+Enter adds a line)"));
    m_input->setMaximumHeight(88);
    m_input->installEventFilter(this);
    layout->addWidget(m_input);

    auto* actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(6);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("hintLabel"));
    m_status->setWordWrap(true);
    actionRow->addWidget(m_status, 1);

    m_applyButton = new QPushButton(QStringLiteral("Apply edits"), this);
    m_applyButton->setEnabled(false);
    m_applyButton->setVisible(false);
    actionRow->addWidget(m_applyButton);

    m_sendButton = new QPushButton(QStringLiteral("Send"), this);
    m_sendButton->setDefault(true);
    actionRow->addWidget(m_sendButton);
    layout->addLayout(actionRow);

    connect(m_settingsToggle, &QPushButton::toggled, m_settingsBox, &QWidget::setVisible);
    connect(m_provider, &QComboBox::activated, this, &AiChatPanel::applyPreset);
    connect(m_refreshModels, &QPushButton::clicked, this, &AiChatPanel::refreshModels);
    connect(m_applyButton, &QPushButton::clicked, this, &AiChatPanel::applyPendingEdits);
    connect(m_sendButton, &QPushButton::clicked, this, [this]() {
        if (m_busy) {
            m_client->abort();
            return;
        }
        sendPrompt();
    });
    connect(m_forgetKey, &QPushButton::clicked, this, [this]() {
        m_key->clear();
        AiSettingsStore::storeKey(QString());
        m_status->setText(QStringLiteral("Stored key removed"));
    });
    connect(m_endpoint, &QLineEdit::editingFinished, this, [this]() {
        saveSettings();
        updatePrivacyHint();
    });
    connect(m_model, &QComboBox::editTextChanged, this, [this](const QString&) { saveSettings(); });
    connect(m_includeContext, &QCheckBox::toggled, this, [this](bool) { saveSettings(); });
    connect(m_saveKey, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked)
            AiSettingsStore::storeKey(m_key->text());
        else
            AiSettingsStore::storeKey(QString());
    });
    connect(m_key, &QLineEdit::editingFinished, this, [this]() {
        if (m_saveKey->isChecked())
            AiSettingsStore::storeKey(m_key->text());
    });
}

void AiChatPanel::loadSettings()
{
    const AiSettingsValues values = AiSettingsStore::load(m_database);
    m_includeContext->setChecked(values.includeFileContext);
    m_endpoint->setText(values.endpoint);
    m_model->setCurrentText(values.model);
    m_key->setText(AiSettingsStore::loadKey());
    m_saveKey->setChecked(!m_key->text().isEmpty());

    int presetIndex = -1;
    const QVector<AiProviderPreset> presets = providerPresets();
    for (int i = 0; i < presets.size(); ++i) {
        if (presets.at(i).endpoint == values.endpoint) {
            presetIndex = i;
            break;
        }
    }
    if (presetIndex < 0)
        presetIndex = presets.size() - 1;  // Custom
    m_provider->setCurrentIndex(presetIndex);
    updatePrivacyHint();
}

void AiChatPanel::saveSettings()
{
    const AiSettingsValues values = currentValues();
    AiSettingsStore::save(m_database, values);
    if (m_saveKey->isChecked())
        AiSettingsStore::storeKey(m_key->text());
}

void AiChatPanel::applyPreset(int index)
{
    const QVector<AiProviderPreset> presets = providerPresets();
    if (index < 0 || index >= presets.size())
        return;
    const AiProviderPreset& preset = presets.at(index);
    if (preset.id == QLatin1String("custom")) {
        // Keep whatever the user has typed so it can be adjusted by hand.
        updatePrivacyHint();
        return;
    }
    m_endpoint->setText(preset.endpoint);
    m_model->setCurrentText(preset.model);
    saveSettings();
    updatePrivacyHint();

    // A local server usually has models already downloaded; offer them.
    if (!preset.requiresKey && AiSettingsStore::isLoopbackEndpoint(preset.endpoint))
        refreshModels();
}

void AiChatPanel::refreshModels()
{
    const QString endpoint = m_endpoint->text().trimmed();
    if (endpoint.isEmpty()) {
        m_status->setText(QStringLiteral("Set an endpoint first"));
        return;
    }
    saveSettings();
    m_refreshModels->setEnabled(false);
    m_status->setText(QStringLiteral("Asking %1 for its models\u2026").arg(endpoint));

    AiSettingsValues values = currentValues();
    m_client->listModels(values, m_key->text(), [this](const Result<QStringList>& models) {
        m_refreshModels->setEnabled(true);
        if (!models) {
            const QString detail = models.error().detail.isEmpty() ? models.error().message
                                                                  : models.error().detail;
            m_status->setText(QStringLiteral("Model list failed: %1").arg(detail));
            return;
        }
        if (models.value().isEmpty()) {
            m_status->setText(QStringLiteral("The endpoint answered but reported no models"));
            return;
        }
        const QString current = m_model->currentText();
        m_model->clear();
        m_model->addItems(models.value());
        const int index = m_model->findText(current);
        if (index >= 0)
            m_model->setCurrentIndex(index);
        m_status->setText(QStringLiteral("%1 model(s) available").arg(models.value().size()));
    });
}

void AiChatPanel::updatePrivacyHint()
{
    if (m_privacy == nullptr)
        return;
    const QString endpoint = m_endpoint->text().trimmed();
    if (AiSettingsStore::isLoopbackEndpoint(endpoint)) {
        m_privacy->setText(QStringLiteral("This endpoint is on this computer: prompts and file "
                                          "listings never leave the machine."));
    } else {
        const QUrl url(endpoint);
        m_privacy->setText(QStringLiteral("This endpoint is remote (%1): prompts and the open "
                                          "file's listing are sent there. PKG Studio has no relay "
                                          "and no telemetry.")
                               .arg(url.host().isEmpty() ? endpoint : url.host()));
    }
}

QString AiChatPanel::fileContext(FileEditor* openEditor) const
{
    if (openEditor == nullptr)
        return {};
    constexpr quint64 kContextLimit = 8ULL * 1024ULL;
    const QString listing = openEditor->readableListing(kContextLimit);
    if (listing.isEmpty())
        return {};
    return QStringLiteral("Open file: %1\nSize: %2 bytes\nReadable listing (first %3 bytes, "
                          "offset  hex  ascii):\n%4")
        .arg(openEditor->path())
        .arg(openEditor->fileSize())
        .arg(kContextLimit)
        .arg(listing);
}

void AiChatPanel::refreshContext()
{
    FileEditor* openEditor = editor();
    if (openEditor == nullptr) {
        m_context->setText(QStringLiteral("Context: no file open"));
        return;
    }
    m_context->setText(QStringLiteral("Context: %1 (%2 bytes)")
                           .arg(openEditor->path())
                           .arg(openEditor->fileSize()));
}

FileEditor* AiChatPanel::editor() const
{
    return m_editorProvider ? m_editorProvider() : nullptr;
}

void AiChatPanel::focusInput()
{
    m_input->setFocus(Qt::ShortcutFocusReason);
}

AiSettingsValues AiChatPanel::currentValues() const
{
    AiSettingsValues values;
    values.enabled = true;
    values.endpoint = m_endpoint->text().trimmed();
    values.model = m_model->currentText().trimmed();
    values.includeFileContext = m_includeContext->isChecked();
    return values;
}

void AiChatPanel::appendMessage(const QString& who, const QString& text, bool isError)
{
    const QString colour = isError ? QStringLiteral("#ff7b72") : QStringLiteral("inherit");
    m_chat->append(QStringLiteral("<p style='margin-top: 8px;'><b style='color: %1;'>%2</b></p>%3")
                       .arg(colour, who.toHtmlEscaped(), escapeBlock(text)));
}

void AiChatPanel::setBusy(bool busy, const QString& message)
{
    m_busy = busy;
    m_sendButton->setText(busy ? QStringLiteral("Stop") : QStringLiteral("Send"));
    m_refreshModels->setEnabled(!busy);
    m_status->setText(message);
}

void AiChatPanel::sendPrompt()
{
    const QString prompt = m_input->toPlainText().trimmed();
    if (prompt.isEmpty())
        return;

    const AiSettingsValues values = currentValues();
    if (AiSettingsStore::chatCompletionsUrl(values.endpoint).isEmpty()
        || values.endpoint.isEmpty()) {
        m_status->setText(QStringLiteral("Set an endpoint and a model first"));
        return;
    }
    if (values.model.isEmpty()) {
        m_status->setText(QStringLiteral("Choose or type a model name"));
        return;
    }
    saveSettings();

    QString outgoing = prompt;
    FileEditor* openEditor = editor();
    if (values.includeFileContext && openEditor != nullptr) {
        const QString context = fileContext(openEditor);
        if (!context.isEmpty())
            outgoing = context + QStringLiteral("\n\nRequest: ") + prompt;
    }

    appendMessage(QStringLiteral("You"), prompt);
    m_input->clear();
    m_history.append(AiMessage{QStringLiteral("user"), outgoing});
    m_pendingEdits.clear();
    m_applyButton->setEnabled(false);
    m_applyButton->setVisible(false);

    setBusy(true, QStringLiteral("Waiting for %1\u2026").arg(values.endpoint));
    m_client->send(values, m_key->text(), m_history,
                   [this, openEditor](const Result<AiClient::Reply>& reply) {
                       setBusy(false, QString());
                       if (!reply) {
                           const QString detail = reply.error().detail;
                           appendMessage(QStringLiteral("Error"),
                                         detail.isEmpty()
                                             ? reply.error().message
                                             : reply.error().message
                                                   + QStringLiteral("\n") + detail,
                                         true);
                           m_status->setText(QStringLiteral("Request failed"));
                           // Drop the unanswered turn so the next prompt is not
                           // stacked on a conversation the model never saw.
                           if (!m_history.isEmpty())
                               m_history.removeLast();
                           return;
                       }

                       appendMessage(QStringLiteral("Assistant"), reply.value().content);
                       m_history.append(
                           AiMessage{QStringLiteral("assistant"), reply.value().content});
                       while (m_history.size() > 24)
                           m_history.remove(0);

                       const QVector<ListingEntry> edits = extractEdits(reply.value().content);
                       if (edits.isEmpty()) {
                           m_status->setText(QStringLiteral("No byte edits in this reply"));
                           return;
                       }
                       m_pendingEdits = edits;
                       const quint64 bytes = listingByteCount(edits);
                       m_applyButton->setVisible(true);
                       m_applyButton->setEnabled(openEditor != nullptr);
                       m_status->setText(
                           QStringLiteral("%1 byte(s) in %2 block(s) \u2014 review, then apply")
                               .arg(bytes)
                               .arg(edits.size()));
                   });
}

void AiChatPanel::applyPendingEdits()
{
    FileEditor* openEditor = editor();
    if (openEditor == nullptr || m_pendingEdits.isEmpty())
        return;
    auto applied = openEditor->applyByteEdits(m_pendingEdits);
    if (!applied) {
        QMessageBox::warning(this, QStringLiteral("Cannot apply the edits"),
                             applied.error().detail.isEmpty() ? applied.error().message
                                                              : applied.error().detail);
        return;
    }
    m_pendingEdits.clear();
    m_applyButton->setEnabled(false);
    m_applyButton->setVisible(false);
    m_status->setText(QStringLiteral("%1 byte(s) changed in memory \u2014 undo with Ctrl+Z or "
                                     "save to write them")
                          .arg(applied.value()));
    appendMessage(QStringLiteral("PKG Studio"),
                  QStringLiteral("Applied %1 byte change(s) to %2. They stay in memory and are "
                                 "undoable until you save.")
                      .arg(applied.value())
                      .arg(openEditor->path()));
}

bool AiChatPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
        if (enter && (key->modifiers() & Qt::ShiftModifier) == 0) {
            sendPrompt();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace pkg::ui
