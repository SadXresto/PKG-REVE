#include "ui/AiChatPanel.h"

#include "ai/LocalModels.h"
#include "core/FormatUtil.h"
#include "core/ProcessInfo.h"
#include "data/Database.h"
#include "data/Settings.h"
#include "ui/FileEditor.h"
#include "ui/PatchReviewDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QtConcurrent>

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
                  QStringLiteral("Press Find on this computer to look for Ollama, LM Studio or "
                                 "llama.cpp and list every model already installed \u2014 "
                                 "including namespaced names such as huihui_ai/qwen3.4b:7b. "
                                 "Then ask about the open file. Nothing is sent until you press "
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
    m_refreshModels->setToolTip(QStringLiteral("Asks the endpoint above for the models it "
                                               "offers. Only runs when you press it."));
    modelRow->addWidget(m_refreshModels);
    settingsLayout->addLayout(modelRow);

    m_scanLocal = new QPushButton(QStringLiteral("Find on this computer"), m_settingsBox);
    m_scanLocal->setToolTip(QStringLiteral("Checks the usual local AI ports for a running "
                                           "server and looks for Ollama model folders on every "
                                           "drive. Also runs only when you press it."));
    settingsLayout->addWidget(m_scanLocal);

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

    auto* aiActionRow = new QHBoxLayout();
    aiActionRow->setContentsMargins(0, 0, 0, 0);
    aiActionRow->setSpacing(6);
    auto* actionCaption = new QLabel(QStringLiteral("Action"), this);
    aiActionRow->addWidget(actionCaption);
    m_action = new QComboBox(this);
    m_action->setObjectName(QStringLiteral("aiActionCombo"));
    m_action->addItem(QStringLiteral("Ask about this file"), QStringLiteral("ask"));
    m_action->addItem(QStringLiteral("Explain selected bytes"), QStringLiteral("selection"));
    m_action->addItem(QStringLiteral("Explain last build error"), QStringLiteral("error"));
    m_action->addItem(QStringLiteral("Suggest a refactoring"), QStringLiteral("refactor"));
    m_action->addItem(QStringLiteral("Generate documentation"), QStringLiteral("docs"));
    m_action->addItem(QStringLiteral("Propose a patch (reviewed before apply)"),
                      QStringLiteral("patch"));
    m_action->setToolTip(QStringLiteral("The action shapes the prompt. Nothing is applied "
                                        "without a separate review and approval."));
    aiActionRow->addWidget(m_action, 1);
    layout->addLayout(aiActionRow);

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
    connect(m_scanLocal, &QPushButton::clicked, this, &AiChatPanel::scanForLocalModels);
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
        presetIndex = static_cast<int>(presets.size()) - 1;  // Custom
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
    if (!preset.hint.isEmpty())
        m_status->setText(preset.hint);
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
    // Edits belong to the file that was open when the reply arrived. Drop them
    // rather than letting them land in a different document.
    if (!m_pendingEditsPath.isEmpty()
        && (openEditor == nullptr || openEditor->path() != m_pendingEditsPath)) {
        m_pendingEdits.clear();
        m_pendingEditsPath.clear();
        m_applyButton->setEnabled(false);
        m_applyButton->setVisible(false);
    }
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
    m_scanLocal->setEnabled(!busy && !m_scanning);
    m_status->setText(message);
}

void AiChatPanel::setBuildErrorProvider(BuildErrorProvider provider)
{
    m_buildErrorProvider = std::move(provider);
}

void AiChatPanel::explainLastBuildError()
{
    if (m_action == nullptr)
        return;
    const int index = m_action->findData(QStringLiteral("error"));
    if (index >= 0)
        m_action->setCurrentIndex(index);
    if (m_buildErrorProvider == nullptr || m_buildErrorProvider().trimmed().isEmpty()) {
        m_status->setText(QStringLiteral("No build output is available yet. Run a build in "
                                         "DevFlow first."));
        return;
    }
    m_input->setFocus(Qt::ShortcutFocusReason);
    m_status->setText(QStringLiteral("Press Send to explain the last build output."));
}

void AiChatPanel::sendPrompt()
{
    const QString actionId = m_action != nullptr
        ? m_action->currentData().toString()
        : QStringLiteral("ask");
    QString prompt = m_input->toPlainText().trimmed();
    if (prompt.isEmpty()) {
        if (actionId == QLatin1String("selection"))
            prompt = QStringLiteral("Explain the selected bytes.");
        else if (actionId == QLatin1String("error"))
            prompt = QStringLiteral("Explain this build error and name one concrete next step.");
        else if (actionId == QLatin1String("refactor"))
            prompt = QStringLiteral("Suggest a refactoring for the code in context.");
        else if (actionId == QLatin1String("docs"))
            prompt = QStringLiteral("Generate concise documentation for this file.");
        else if (actionId == QLatin1String("patch"))
            prompt = QStringLiteral("Propose a complete replacement for this file.");
        else
            return;
    }

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

    FileEditor* openEditor = editor();
    const QString targetPath = openEditor != nullptr ? openEditor->path() : QString();
    QString context;
    if (actionId == QLatin1String("error")) {
        const QString output = m_buildErrorProvider != nullptr ? m_buildErrorProvider()
                                                              : QString();
        if (output.trimmed().isEmpty()) {
            m_status->setText(QStringLiteral("No build output is available yet. Run a build in "
                                             "DevFlow first."));
            return;
        }
        context = QStringLiteral("Build output (bounded):\n%1")
                      .arg(output.right(8000));
    } else if (actionId == QLatin1String("selection")) {
        if (openEditor == nullptr || !openEditor->hasSelection()) {
            m_status->setText(QStringLiteral("Select bytes in the hex view first, then run "
                                             "Explain selected bytes."));
            return;
        }
        context = QStringLiteral("Selection in %1:\n%2")
                      .arg(targetPath, openEditor->selectionContext(4096));
    } else if (actionId == QLatin1String("patch")) {
        if (openEditor == nullptr) {
            m_status->setText(QStringLiteral("Open the file to patch first."));
            return;
        }
        constexpr quint64 kPatchLimit = 256ULL * 1024ULL;
        if (openEditor->fileSize() > kPatchLimit) {
            m_status->setText(QStringLiteral("Patch proposals are limited to files up to "
                                             "256 KiB in this build."));
            return;
        }
        context = fileContext(openEditor);
    } else if (values.includeFileContext && openEditor != nullptr) {
        context = fileContext(openEditor);
    }

    // A remote endpoint must never receive project content silently.
    if (!context.isEmpty() && !AiSettingsStore::isLoopbackEndpoint(values.endpoint)
        && !m_remoteContextAccepted) {
        const QUrl url(values.endpoint);
        const QString host = url.host().isEmpty() ? values.endpoint : url.host();
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Send project content to %1?").arg(host),
            QStringLiteral("This request includes project content (a file listing, selection or "
                           "build output). It will be sent to %1 for this session. PKG Studio "
                           "has no relay and no telemetry.\n\nContinue?")
                .arg(host),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            m_status->setText(QStringLiteral("Request cancelled; nothing was sent."));
            return;
        }
        m_remoteContextAccepted = true;
    }

    QString instruction;
    if (actionId == QLatin1String("selection"))
        instruction = QStringLiteral("Explain what the selected bytes are and how they are "
                                     "used. Base the answer only on the listing; say so when "
                                     "the bytes are not enough to identify a format.");
    else if (actionId == QLatin1String("error"))
        instruction = QStringLiteral("Explain this build diagnostic in plain language, name the "
                                     "likely cause from the evidence in the output, and give "
                                     "one concrete next step. Do not claim anything was fixed.");
    else if (actionId == QLatin1String("refactor"))
        instruction = QStringLiteral("Suggest a refactoring for the code in context. Describe "
                                     "the change and its trade-offs; do not claim it was "
                                     "applied. Byte-level edits may be returned in a "
                                     "```pkgstudio-edit block for review.");
    else if (actionId == QLatin1String("docs"))
        instruction = QStringLiteral("Write concise documentation for this file: purpose, "
                                     "structure, notable fields and limits. Do not invent facts "
                                     "the listing does not show.");
    else if (actionId == QLatin1String("patch"))
        instruction = QStringLiteral("Return the complete replacement file inside one "
                                     "```pkgstudio-file fenced block, with no commentary "
                                     "inside the block. This is a proposal only: the user "
                                     "reviews a diff and approves it before anything is "
                                     "applied. Do not change more than the request needs.");
    else
        instruction = QStringLiteral("Answer using the evidence in the context. Do not invent "
                                     "file contents.");

    QString outgoing = prompt;
    if (!context.isEmpty())
        outgoing = context + QStringLiteral("\n\nRequest: ") + prompt;
    outgoing += QStringLiteral("\n\n") + instruction;

    appendMessage(QStringLiteral("You"), prompt);
    m_input->clear();
    m_history.append(AiMessage{QStringLiteral("user"), outgoing});
    m_pendingEdits.clear();
    m_applyButton->setEnabled(false);
    m_applyButton->setVisible(false);

    setBusy(true, QStringLiteral("Waiting for %1\u2026").arg(values.endpoint));
    m_client->send(values, m_key->text(), m_history,
                   [this, targetPath, actionId](const Result<AiClient::Reply>& reply) {
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
                           m_pendingEdits.clear();
                           m_pendingEditsPath.clear();
                           m_applyButton->setEnabled(false);
                           m_applyButton->setVisible(false);
                           return;
                       }

                       appendMessage(QStringLiteral("Assistant"), reply.value().content);
                       m_history.append(
                           AiMessage{QStringLiteral("assistant"), reply.value().content});
                       while (m_history.size() > 24)
                           m_history.remove(0);

                       if (actionId == QLatin1String("patch")) {
                           const FileProposal proposal =
                               extractFileProposal(reply.value().content);
                           if (!proposal.found) {
                               m_status->setText(proposal.note.isEmpty()
                                                     ? QStringLiteral("No proposal block in "
                                                                      "this reply")
                                                     : proposal.note);
                               return;
                           }
                           reviewProposedFile(proposal.bytes, targetPath);
                           return;
                       }

                       const QVector<ListingEntry> edits = extractEdits(reply.value().content);
                       if (edits.isEmpty()) {
                           m_status->setText(QStringLiteral("No byte edits in this reply"));
                           return;
                       }
                       m_pendingEdits = edits;
                       m_pendingEditsPath = targetPath;
                       const quint64 bytes = listingByteCount(edits);
                       // Only offer Apply while the file the reply is about is
                       // still the open one; a raw editor pointer could be gone.
                       FileEditor* liveEditor = editor();
                       const bool sameFile = liveEditor != nullptr && !targetPath.isEmpty()
                           && liveEditor->path() == targetPath;
                       m_applyButton->setVisible(true);
                       m_applyButton->setEnabled(sameFile);
                       QString status = QStringLiteral(
                                            "%1 byte(s) in %2 block(s) \u2014 review, then apply")
                                            .arg(bytes)
                                            .arg(edits.size());
                       if (!sameFile)
                           status += QStringLiteral(
                               " \u2014 switch back to the file this reply was about");
                       m_status->setText(status);
                   });
}

void AiChatPanel::reviewProposedFile(const QByteArray& proposal, const QString& targetPath)
{
    FileEditor* openEditor = editor();
    if (openEditor == nullptr || targetPath.isEmpty() || openEditor->path() != targetPath) {
        m_status->setText(QStringLiteral("The proposal belongs to %1. Open that file again "
                                         "to review it.")
                              .arg(targetPath));
        return;
    }
    auto before = openEditor->document()->readMerged(0, openEditor->document()->size());
    if (!before) {
        m_status->setText(QStringLiteral("Cannot read the current file for review: %1")
                              .arg(before.error().detail.isEmpty() ? before.error().message
                                                                   : before.error().detail));
        return;
    }
    PatchReviewDialog review(targetPath, before.value(), proposal, this);
    if (review.exec() != QDialog::Accepted || !review.approved()) {
        m_status->setText(QStringLiteral("Proposal discarded; the file was not changed."));
        appendMessage(QStringLiteral("PKG Studio"),
                      QStringLiteral("The proposed patch was not applied."));
        return;
    }
    QVector<ListingEntry> entries;
    entries.append(ListingEntry{0, proposal});
    const auto applied = openEditor->applyByteEdits(entries);
    if (!applied) {
        m_status->setText(applied.error().detail.isEmpty() ? applied.error().message
                                                          : applied.error().detail);
        return;
    }
    m_status->setText(QStringLiteral("Approved patch applied: %1 byte(s) changed in memory "
                                     "\u2014 undo with Ctrl+Z or save to write them")
                          .arg(applied.value()));
    appendMessage(QStringLiteral("PKG Studio"),
                  QStringLiteral("The reviewed patch was applied after approval. The changes "
                                 "stay in memory and are undoable until you save."));
}

void AiChatPanel::applyPendingEdits()
{
    FileEditor* openEditor = editor();
    if (openEditor == nullptr || m_pendingEdits.isEmpty())
        return;
    if (m_pendingEditsPath.isEmpty() || openEditor->path() != m_pendingEditsPath) {
        QMessageBox::information(
            this, QStringLiteral("These edits belong to another file"),
            QStringLiteral("The reply was about %1. Open that file again and press Apply edits.")
                .arg(m_pendingEditsPath));
        return;
    }
    auto applied = openEditor->applyByteEdits(m_pendingEdits);
    if (!applied) {
        QMessageBox::warning(this, QStringLiteral("Cannot apply the edits"),
                             applied.error().detail.isEmpty() ? applied.error().message
                                                              : applied.error().detail);
        return;
    }
    m_pendingEdits.clear();
    m_pendingEditsPath.clear();
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

void AiChatPanel::scanForLocalModels()
{
    if (m_scanning)
        return;
    m_scanning = true;
    m_scanLocal->setEnabled(false);
    m_scanLocal->setText(QStringLiteral("Scanning\u2026"));
    m_status->setText(
        QStringLiteral("Looking for local AI servers and Ollama models on every drive\u2026"));

    // The scan touches a handful of sockets and folders; keep it off the UI
    // thread so the window stays responsive on low-end machines.
    auto* watcher = new QFutureWatcher<LocalModelScan>(this);
    connect(watcher, &QFutureWatcher<LocalModelScan>::finished, this, [this, watcher]() {
        const LocalModelScan scan = watcher->result();
        watcher->deleteLater();
        m_scanning = false;
        m_scanLocal->setEnabled(!m_busy);
        m_scanLocal->setText(QStringLiteral("Find on this computer"));
        applyLocalScan(scan);
    });
    watcher->setFuture(QtConcurrent::run([]() { return scanLocalModelServers(); }));
}

void AiChatPanel::applyLocalScan(const LocalModelScan& scan)
{
    if (!scan.endpoint.isEmpty() && m_endpoint->text().trimmed() != scan.endpoint) {
        m_endpoint->setText(scan.endpoint);
        const QVector<AiProviderPreset> presets = providerPresets();
        for (int i = 0; i < presets.size(); ++i) {
            if (presets.at(i).endpoint == scan.endpoint) {
                const QSignalBlocker blocker(m_provider);
                m_provider->setCurrentIndex(i);
                break;
            }
        }
    }
    updatePrivacyHint();

    if (!scan.models.isEmpty()) {
        const QString wanted = m_model->currentText().trimmed();
        const quint64 ramBytes = sysinfo::totalRamBytes();
        m_model->clear();
        for (const LocalModelInfo& model : scan.models) {
            m_model->addItem(model.name);
            const int index = m_model->count() - 1;
            QStringList tooltip{model.name};
            tooltip << (model.sizeBytes > 0
                            ? QStringLiteral("%1 on disk").arg(fmt::humanBytes(model.sizeBytes))
                            : QStringLiteral("Size not reported by the endpoint"));
            const QString hint = modelFitHint(model.sizeBytes, ramBytes);
            if (!hint.isEmpty())
                tooltip << hint;
            m_model->setItemData(index, tooltip.join(QLatin1Char('\n')), Qt::ToolTipRole);
        }
        int selected = wanted.isEmpty() ? -1 : m_model->findText(wanted);
        if (selected < 0)
            selected = 0;
        m_model->setCurrentIndex(selected);
        saveSettings();
    }

    if (!scan.notes.isEmpty())
        appendMessage(QStringLiteral("PKG Studio"), scan.notes.join(QLatin1Char('\n')));

    if (!scan.models.isEmpty()) {
        m_status->setText(
            scan.endpoint.isEmpty()
                ? QStringLiteral("%1 model(s) found on disk \u2014 start the server, then scan "
                                 "again")
                      .arg(scan.models.size())
                : QStringLiteral("%1 model(s) ready \u2014 type a question and press Send")
                      .arg(scan.models.size()));
    } else if (!scan.endpoint.isEmpty()) {
        m_status->setText(QStringLiteral("%1 is running but has no models installed yet")
                              .arg(scan.providerName));
    } else {
        m_status->setText(QStringLiteral("Nothing found \u2014 see the note in the chat"));
    }
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
