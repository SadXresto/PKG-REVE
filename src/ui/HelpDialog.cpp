#include "ui/HelpDialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

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
                  "<li>There is no disassembler, debugger, AI assistant, plugin runtime or "
                  "installer yet.</li>"
                  "<li>Compressed or encrypted payloads are not extracted. Do not use PKG Studio "
                  "to bypass DRM or console security.</li>"
                  "<li>Analysis and caching are local; there is no telemetry and no upload.</li>"
                  "</ul>"))});

    return topics;
}

bool HelpDialog::showTopic(QWidget* parent, const QString& id)
{
    const QVector<Topic> available = topics();
    const auto topic = std::find_if(available.begin(), available.end(),
                                    [&id](const Topic& entry) { return entry.id == id; });
    if (topic == available.end())
        return false;

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("PKG Studio help \u2014 %1").arg(topic->title));
    dialog.resize(680, 560);
    dialog.setModal(true);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(8, 8, 8, 8);

    auto* browser = new QTextBrowser(&dialog);
    browser->setOpenExternalLinks(false);
    browser->setHtml(
        QStringLiteral("<html><body style='font-family: sans-serif;'>%1</body></html>")
            .arg(topic->html));
    layout->addWidget(browser, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::clicked, &dialog, &QDialog::accept);
    layout->addWidget(buttons);
    dialog.exec();
    return true;
}

}  // namespace pkg::ui
