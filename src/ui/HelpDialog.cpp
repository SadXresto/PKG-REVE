#include "ui/HelpDialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <memory>

namespace pkg::ui {
namespace {

QString page(const QString& title, const QString& body)
{
    return QStringLiteral("<h2>%1</h2>%2").arg(title, body);
}

}  // namespace

QVector<HelpDialog::Topic> HelpDialog::topics()
{
    QVector<Topic> topics;

    topics.append(
        {QStringLiteral("help.guide"), QStringLiteral("Getting started"),
         QStringLiteral("Open a file, read the analysis, edit bytes and inspect structures."),
         page(QStringLiteral("Getting started"),
              QStringLiteral(
                  "<p><b>Start with a sample.</b> The Welcome tab has synthetic ELF, APK and "
                  "sample-project buttons. Everything they show is generated for this project "
                  "and contains no commercial content.</p>"
                  "<p><b>1. Open a file</b> with <i>File &rarr; Open file</i> (Ctrl+O) or drop it "
                  "on the window. Files are memory mapped, so large packages do not load into "
                  "RAM.</p>"
                  "<p><b>2. Read the Analysis tab.</b> Only parsers that verified a documented "
                  "signature claim a format. Unrecognized files stay raw with an honest notice; "
                  "yellow/red warnings describe what could not be parsed.</p>"
                  "<p><b>3. Explore bytes.</b> Hover any byte for the inspector. Ctrl+F searches "
                  "text or hex bytes, Ctrl+G jumps to an offset, and selections can be copied as "
                  "hex or ASCII.</p>"
                  "<p><b>4. Edit safely.</b> Click <i>Editing off</i> to opt in. Edits stay in "
                  "memory, are undoable, and only reach disk when you save. Saving writes a "
                  "timestamped backup first and replaces the file atomically; you can cancel the "
                  "background save.</p>"
                  "<p><b>5. Describe a layout.</b> Open <i>Structure</i> in the file header, "
                  "choose a template or create one with <i>Edit&hellip;</i>, and click a field to "
                  "highlight its bytes.</p>"
                  "<p><b>6. Find anything</b> with Ctrl+Shift+P: commands, open files, analysis "
                  "tools and these help pages are searchable.</p>"))});

    topics.append(
        {QStringLiteral("help.shortcuts"), QStringLiteral("Keyboard shortcuts"),
         QStringLiteral("Every command and its current shortcut, searchable in the palette."),
         page(QStringLiteral("Keyboard shortcuts"),
              QStringLiteral(
                  "<table cellpadding='4'>"
                  "<tr><td>Ctrl+O</td><td>Open file</td></tr>"
                  "<tr><td>Ctrl+Shift+O</td><td>Open project folder</td></tr>"
                  "<tr><td>Ctrl+S / Ctrl+Shift+S</td><td>Save / Save as</td></tr>"
                  "<tr><td>Ctrl+Z / Ctrl+Shift+Z</td><td>Undo / redo byte edit</td></tr>"
                  "<tr><td>Ctrl+F</td><td>Find in current file</td></tr>"
                  "<tr><td>Ctrl+G</td><td>Go to offset</td></tr>"
                  "<tr><td>Ctrl+Shift+H / Ctrl+Shift+A</td><td>Copy selection as hex / ASCII</td></tr>"
                  "<tr><td>Ctrl+Shift+B</td><td>Toggle structure builder</td></tr>"
                  "<tr><td>Ctrl+Shift+P</td><td>Command palette</td></tr>"
                  "<tr><td>Ctrl+,</td><td>Settings</td></tr>"
                  "<tr><td>F1</td><td>About PKG Studio</td></tr>"
                  "</table>"
                  "<p>Shortcuts can be changed in Settings. The palette always shows the current "
                  "binding.</p>"))});

    topics.append(
        {QStringLiteral("help.structureTemplates"), QStringLiteral("Structure templates"),
         QStringLiteral("Template JSON schema, offsets, endianness, arrays and nested structs."),
         page(QStringLiteral("Structure templates"),
              QStringLiteral(
                  "<p>The structure builder reads a JSON template and interprets bytes with it. "
                  "This is a <b>layout description, not format detection</b>: a template can be "
                  "applied to any file, and validation only checks the schema and that fields "
                  "fit inside the file.</p>"
                  "<h3>Schema (version 1)</h3>"
                  "<pre>{\n"
                  "  \"format\": \"pkgstudio.structure\",\n"
                  "  \"version\": 1,\n"
                  "  \"name\": \"Example header\",\n"
                  "  \"endianness\": \"little\",\n"
                  "  \"fields\": [\n"
                  "    {\"name\": \"magic\", \"type\": \"bytes\", \"offset\": 0, \"length\": 4},\n"
                  "    {\"name\": \"count\", \"type\": \"u16\", \"offset\": 4},\n"
                  "    {\"name\": \"entries\", \"type\": \"struct\", \"offset\": 6, \"count\": 2,\n"
                  "     \"fields\": [\n"
                  "       {\"name\": \"id\", \"type\": \"u32\", \"offset\": 0},\n"
                  "       {\"name\": \"label\", \"type\": \"ascii\", \"offset\": 4, \"length\": 8}\n"
                  "     ]}\n"
                  "  ]\n"
                  "}</pre>"
                  "<p>Types: <code>u8 i8 u16 i16 u32 i32 u64 i64 f32 f64 ascii utf8 bytes "
                  "struct</code>. Offsets are relative to the file, or to the enclosing struct. "
                  "A field-level <code>endianness</code> overrides the template default. "
                  "<code>count</code> repeats a field; struct arrays reuse the same layout for "
                  "every element. Text and bytes fields require <code>length</code>.</p>"
                  "<p>Build templates without JSON on the <b>Form</b> tab: add, edit, remove "
                  "and reorder fields, override byte order per field, and add a struct then use "
                  "<i>Add nested field</i> for nested layouts. The <i>JSON (advanced)</i> tab "
                  "edits the same template; switching tabs keeps both in sync.</p>"
                  "<p>Limits: 8 nesting levels, 4096 array elements, 1024 KiB per text field and "
                  "4000 displayed fields. Errors never produce guessed values &mdash; out-of-bounds "
                  "fields are marked and explained.</p>"
                  "<p>Built-in templates live in <code>templates/</code> and cover an ELF64 header, "
                  "a ZIP local header and a PNG IHDR chunk. User templates are stored in the "
                  "application data folder under <code>templates/</code>; use "
                  "<i>Structure &rarr; Folder</i> to open it.</p>"))});

    topics.append(
        {QStringLiteral("help.featureStatus"), QStringLiteral("Feature status"),
         QStringLiteral("Implemented, partial or experimental, and planned features."),
         page(QStringLiteral("Feature status"),
              QStringLiteral(
                  "<p><b>PKG Studio is a development build.</b> This page is the honest "
                  "feature list; a planned item is not a promise of a future release.</p>"
                  "<h3>Working and tested</h3>"
                  "<ul>"
                  "<li>Format analysis for ELF, PE/COFF, ZIP/APK, PSP/PS Vita PKG and the PS4 "
                  "PKG container header, with an explicit raw-byte fallback.</li>"
                  "<li>Hex workspace: navigation, selection, copy, streaming search, live byte "
                  "inspector, signature overlay and same-size replace.</li>"
                  "<li>Opt-in byte editing with per-file undo/redo, timestamped backup, atomic "
                  "write and cancellable background saving.</li>"
                  "<li>Structure builder with form and JSON editing, validation, hex "
                  "synchronisation and built-in examples.</li>"
                  "<li>Command center, offline help, themes, settings, session restore and the "
                  "synthetic sample workspace.</li>"
                  "<li>GameForge workspace: bounded background project scan, build-system "
                  "and engine detection, categorised file inventory with sizes, filename "
                  "filter, open-in-hex-editor, and a portability report that lists "
                  "platform/API matches with file:line evidence.</li>"
                  "</ul>"
                  "<h3>Partial or experimental</h3>"
                  "<ul>"
                  "<li>Editing replaces bytes of the same length; inserting or deleting bytes "
                  "is not supported.</li>"
                  "<li>ZIP/APK analysis lists the archive directory and detects the manifest; "
                  "binary AndroidManifest.xml is not decoded.</li>"
                  "<li>PSP/Vita and PS4 support covers documented header fields only. Payloads "
                  "are not decrypted or extracted, and PS5 is not implemented.</li>"
                  "<li>The Code tab lists bytes in editable text and assembles them back; it is "
                  "a byte listing, not a CPU disassembler, and cannot change the file length.</li>"
                  "<li>The AI assistant needs an endpoint you configure. It proposes byte edits "
                  "that you review and apply; replies can be wrong.</li>"
                  "<li>Structure templates are user layouts: validated for schema and file "
                  "bounds, never proof that a file conforms to a format.</li>"
                  "<li>Signature matches are documented patterns with a confidence note, not a "
                  "malware or integrity verdict.</li>"
                  "<li>GameForge portability findings are textual API matches in source-like "
                  "files. They are not a build, not an automatic port, and they do not read "
                  "generated, compressed, encrypted or oversized files.</li>"
                  "</ul>"
                  "<h3>Planned, not present</h3>"
                  "<ul>"
                  "<li>CPU disassembly, debugging, binary diff/navigation, a plugin runtime, "
                  "code-signed installers, an updater and a Windows build verified in CI.</li>"
                  "<li>GameForge phases that are not implemented: mod project manager and "
                  "asset replacement, performance-lab monitoring, project build execution, "
                  "project export/packaging and a game-aware AI prompt preset.</li>"
                  "</ul>"))});

    topics.append(
        {QStringLiteral("help.codeListing"), QStringLiteral("Readable listings (Code tab)"),
         QStringLiteral("Disassemble bytes into editable text and assemble them back."),
         page(QStringLiteral("Readable listings"),
              QStringLiteral(
                  "<p>The <b>Code</b> tab shows the file as an editable listing:</p>"
                  "<pre>00000000  7F 45 4C 46 02 01 01 00  |.ELF....|"
                  "\n00000008  00 00 00 00 00 00 00 00  |........|</pre>"
                  "<p>Offset, hex bytes and printable ASCII. Edit the hex column and press "
                  "<i>Assemble into file</i>: the text is validated (offsets in range, whole "
                  "byte pairs) and applied as ordinary undoable edits. Nothing is written "
                  "until you save, and saving still writes a timestamped backup.</p>"
                  "<p>It works on any file, including PS4/PSP/PS Vita PKG headers and Android "
                  "APK/ZIP containers, because it does not depend on the container. It is a "
                  "<b>byte listing, not a CPU disassembler</b>: machine instructions are not "
                  "decoded, payloads are not decrypted or decompressed, signatures are not "
                  "rebuilt, and the file length cannot change.</p>"
                  "<p>Listings cover the first 256 KiB. Use them only on files you own or are "
                  "authorized to modify; see <i>Help &rarr; Terms of use</i>.</p>"))});

    topics.append(
        {QStringLiteral("help.ai"), QStringLiteral("AI assistant"),
         QStringLiteral("Connect a local model, ask about a file and apply its byte edits."),
         page(QStringLiteral("AI chat (local endpoint)"),
              QStringLiteral(
                  "<p><b>Tools &rarr; AI chat</b> (Ctrl+Shift+I) opens a chat panel docked "
                  "beside the editors, like the chat views in VS Code and OpenCode. It talks "
                  "to an OpenAI-compatible endpoint that you choose, and follows the active "
                  "editor.</p>"
                  "<h3>Choose a provider</h3>"
                  "<ol><li>Pick a preset: <i>Ollama</i>, <i>LM Studio</i> or <i>llama.cpp "
                  "server</i> for a model already installed on this computer, <i>OpenAI API</i> "
                  "for a hosted key, or <i>Custom</i> for any other server.</li>"
                  "<li>Press <i>Find on this computer</i> to detect a local server and list "
                  "every installed model, including namespaced Ollama tags such as "
                  "<code>huihui_ai/qwen3.4b:7b</code>, with its size and whether it fits this "
                  "machine's RAM. It also finds an Ollama model folder on any drive when the "
                  "server is not running.</li>"
                  "<li>Press <i>List models</i> only when you want the endpoint above to "
                  "report its own list, or type a model name. Neither button contacts "
                  "anything on its own.</li>"
                  "<li>Add an API key only if the endpoint needs one. It is stored in "
                  "<code>ai.key</code> in the application data folder, readable only by your "
                  "account, and is never logged. <i>Forget</i> deletes it.</li></ol>"
                  "<h3>Asking</h3>"
                  "<p>Enter sends and Shift+Enter adds a line. With <i>include the open file's "
                  "readable listing</i> on, each prompt carries the first 8 KiB of the open "
                  "file as offsets, hex and ASCII; the panel shows which file it will attach. "
                  "A loopback endpoint keeps the whole conversation on this computer.</p>"
                  "<h3>Applying edits</h3>"
                  "<p>When a reply contains a <code>pkgstudio-edit</code> block (or bare "
                  "<code>OFFSET  HEX BYTES</code> lines), the panel counts the bytes and "
                  "enables <i>Apply edits</i>. Applied edits are bounded to the file, stay in "
                  "memory and can be undone with Ctrl+Z until you save.</p>"
                  "<p><b>Replies can be wrong.</b> Review the diff in the hex view before "
                  "saving, and do not send secrets or files you may not share to a remote "
                  "endpoint.</p>"))});

    topics.append(
        {QStringLiteral("help.privacy"), QStringLiteral("Privacy policy"),
         QStringLiteral("What is stored locally and what the AI endpoint receives."),
         page(QStringLiteral("Privacy policy"),
              QStringLiteral(
                  "<p>PKG Studio is local. There is no account, no telemetry, no analytics "
                  "and no crash-reporting service. The authors receive nothing.</p>"
                  "<h3>Stored on this computer</h3>"
                  "<ul>"
                  "<li><code>pkgstudio.db</code>: settings, recent files, session and parser "
                  "cache (path and size/timestamp, not contents).</li>"
                  "<li><code>logs/</code>: diagnostics and crash traces. No file contents.</li>"
                  "<li><code>themes/</code>: saved themes and copied background images.</li>"
                  "<li><code>ai.key</code>: your optional endpoint key, owner read/write, not "
                  "encrypted.</li>"
                  "<li>Timestamped <code>*.pkgstudio-backup-*.bak</code> files next to files "
                  "you save.</li>"
                  "</ul>"
                  "<h3>AI requests</h3>"
                  "<p>Only sent to the endpoint you configure, only when you press Send: your "
                  "prompt, the conversation, and (if enabled) the first 8 KiB of the open "
                  "file's listing. A loopback endpoint keeps it on this machine; a remote "
                  "endpoint is governed by its own policy.</p>"
                  "<h3>Deleting it</h3>"
                  "<p>Delete the application data folder, or use <i>Forget key</i> for the "
                  "key alone. Nothing else needs an account to be removed.</p>"
                  "<p>The full text is in <code>PRIVACY.md</code> next to the application.</p>"))});

    topics.append(
        {QStringLiteral("help.terms"), QStringLiteral("Terms of use"),
         QStringLiteral("Authorized use, AI responsibility, piracy and warranty."),
         page(QStringLiteral("Terms of use"),
              QStringLiteral(
                  "<p><b>Authorized use only.</b> Use PKG Studio on files you own or may "
                  "legally inspect and modify. Do not use it to bypass DRM, console "
                  "security, signing or copy protection, or to handle content you have no "
                  "rights to. Lawfulness of modification depends on your jurisdiction and "
                  "the package licence; that call is yours.</p>"
                  "<p><b>Honest limits.</b> The Code tab lists bytes, not CPU instructions. "
                  "Payloads are not decrypted or decompressed, packages are not re-signed, "
                  "and PS5 PKG parsing is not implemented.</p>"
                  "<p><b>AI edits are proposals.</b> You configure the endpoint, you review "
                  "the edits, you save them. The authors are not responsible for a model's "
                  "output or for a package that stops working after a change.</p>"
                  "<p><b>Copies.</b> Distributing PKG Studio or modified builds without "
                  "written permission is prohibited. Unauthorized copies are pirated copies; "
                  "identified cases may be reported to hosting providers, platforms or "
                  "authorities and pursued as described in the licence.</p>"
                  "<p><b>No warranty.</b> This is a development build provided as is. Back up "
                  "before saving. The full text is in <code>TERMS.md</code> and "
                  "<code>LICENSE</code>.</p>"))});

    topics.append(
        {QStringLiteral("help.gameforge"), QStringLiteral("GameForge workspace"),
         QStringLiteral("Project inventory, build-system detection and portability evidence."),
         page(QStringLiteral("GameForge workspace"),
              QStringLiteral(
                  "<p><b>GameForge</b> (Ctrl+Shift+G, or the gamepad icon in the activity "
                  "bar) inspects a local game project folder you are allowed to work on. It "
                  "never writes to the project and never runs anything from it.</p>"
                  "<h3>Inventory</h3>"
                  "<p>Opening a folder (or opening any project with <i>File &rarr; Open "
                  "project folder</i>) starts a background scan. The scan reports the "
                  "detected project type from marker files &mdash; CMake, Meson, Makefile, "
                  "Godot, Unity (Assets/ + ProjectSettings/) or Unreal (*.uproject) &mdash; "
                  "and a folder tree with a category and size per file. Version-control, "
                  "engine cache and build output folders are skipped, and symbolic links are "
                  "never followed. Use the filter box to narrow by name, folder or "
                  "category, and double-click a file to open it in the hex editor.</p>"
                  "<h3>Portability evidence</h3>"
                  "<p><i>Analyze portability</i> reads source-like files (source, headers, "
                  "shaders, scripts, build files; limited to a few hundred KiB per file) "
                  "and counts known API families: Win32, Direct3D, Metal, OpenGL, Vulkan, "
                  "SDL, GLFW, XInput, XAudio2, CoreAudio, OpenAL, FMOD/Wwise, pthread and "
                  "the C++ standard threads. Each row shows matches, an example "
                  "<code>file:line</code> and what the family means for a port. Double-click "
                  "a row to open its first match.</p>"
                  "<p><b>These are matches, not a build result.</b> GameForge does not port "
                  "anything, does not generate patches and does not run the project's build "
                  "system. A missing finding only means the text was not found in the files "
                  "that were read.</p>"
                  "<p>Not implemented yet: mod projects, asset replacement, performance "
                  "profiling and build execution. They are listed as planned in <i>Feature "
                  "status</i> and never shown as working controls.</p>"))});

    topics.append(
        {QStringLiteral("help.limitations"), QStringLiteral("Limitations and honest scope"),
         QStringLiteral("What PKG Studio does not parse, confirm or promise."),
         page(QStringLiteral("Limitations"),
              QStringLiteral(
                  "<ul>"
                  "<li>Only documented, signature-matched formats are parsed: ELF, PE, ZIP/APK, "
                  "PSP/Vita PKG and the PS4 PKG header. PS5 PKG layout is not implemented and no "
                  "format is guessed.</li>"
                  "<li>Structure templates are user layouts. They are validated for schema and "
                  "bounds, but never presented as proof of conformance.</li>"
                  "<li>Hex editing cannot insert or delete bytes yet. Replace requires the same "
                  "length.</li>"
                  "<li>There is no CPU disassembler, debugger, plugin runtime or signed "
                  "installer yet. The Code tab is a byte listing, not instruction decoding.</li>"
                  "<li>Compressed or encrypted payloads are not extracted. Do not use PKG Studio "
                  "to bypass DRM or console security.</li>"
                  "<li>Analysis and caching are local; there is no telemetry and no upload.</li>"
                  "</ul>"))});

    return topics;
}

QDialog* HelpDialog::createTopicDialog(QWidget* parent, const QString& id)
{
    const QVector<Topic> available = topics();
    const auto topic = std::find_if(available.begin(), available.end(),
                                    [&id](const Topic& entry) { return entry.id == id; });
    if (topic == available.end())
        return nullptr;

    auto* dialog = new QDialog(parent);
    dialog->setObjectName(QStringLiteral("helpTopicDialog"));
    dialog->setWindowTitle(QStringLiteral("PKG Studio help \u2014 %1").arg(topic->title));
    dialog->resize(680, 560);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(8, 8, 8, 8);

    auto* browser = new QTextBrowser(dialog);
    browser->setObjectName(QStringLiteral("helpTopicBrowser"));
    browser->setOpenExternalLinks(false);
    browser->setHtml(
        QStringLiteral("<html><body style='font-family: sans-serif;'>%1</body></html>")
            .arg(topic->html));
    layout->addWidget(browser, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::clicked, dialog, &QDialog::accept);
    layout->addWidget(buttons);
    return dialog;
}

bool HelpDialog::showTopic(QWidget* parent, const QString& id)
{
    std::unique_ptr<QDialog> dialog(createTopicDialog(parent, id));
    if (!dialog)
        return false;
    dialog->exec();
    return true;
}

}  // namespace pkg::ui
