# Structure templates

The structure builder interprets a file through a JSON template. A template is a
**layout description, not format detection**: it can be applied to any file, and
the application never claims the file conforms to it. Confirmed formats still
come only from signature-matched parsers in the Analysis tab.

## Where templates live

- Built-in examples: [`templates/`](../templates/) in this repository, also compiled
  into the application so they work offline and in packaged builds.
- User templates: `<application data>/templates/*.json`. Open that folder from
  **Structure → Folder** in the file header, or **Tools → Structure: load template…**.
- The template editor can validate and save a template into the user folder.

Built-in examples:

| Template | Layout |
| --- | --- |
| `elf64-header.json` | 64-byte ELF64 header, little-endian |
| `zip-local-header.json` | first ZIP/APK entry header, fixed 19-byte name |
| `png-ihdr.json` | PNG signature and IHDR chunk, big-endian |

## Schema (version 1)

```json
{
  "format": "pkgstudio.structure",
  "version": 1,
  "id": "my.header",
  "name": "Example header",
  "description": "Shown as a tooltip; explain what this layout is for.",
  "endianness": "little",
  "fields": [
    { "name": "magic", "type": "bytes", "offset": 0, "length": 4 },
    { "name": "version", "type": "u16", "offset": 4 },
    { "name": "big_endian_field", "type": "u32", "offset": 6, "endianness": "big" },
    { "name": "label", "type": "ascii", "offset": 10, "length": 8 },
    { "name": "entries", "type": "struct", "offset": 18, "count": 2,
      "fields": [
        { "name": "id", "type": "u32", "offset": 0 },
        { "name": "value", "type": "u32", "offset": 4 }
      ] }
  ]
}
```

### Fields

| Key | Required | Meaning |
| --- | --- | --- |
| `name` | yes | Non-empty, unique among siblings (case-insensitive). |
| `type` | yes | `u8 i8 u16 i16 u32 i32 u64 i64 f32 f64 ascii utf8 bytes struct`. |
| `offset` | yes | Non-negative number or `"0x..."` string, relative to the enclosing struct (file start for roots). |
| `length` | text/bytes only | Fixed byte length of one element, 1 … 1 MiB. |
| `count` | no | Array element count, default 1. Arrays of structs repeat the nested layout per element. |
| `endianness` | no | `little` or `big`; overrides the template default for this field. |
| `fields` | struct only | Non-empty child list. |
| `description` | no | Tooltip / documentation text. |

Limits: 8 nesting levels, 4096 array elements, 4000 displayed fields, template
total of 20 000 fields. Arrays display at most the first 256 elements in the
tree and say so.

## Validation versus correctness

`Validate` (and every evaluation) checks:

- JSON shape, supported version, known types, required keys;
- duplicate sibling names, empty structs, missing text lengths;
- overflow-safe offset arithmetic and whether every field lies inside the file.

Errors are listed in the panel and the affected field is shown as `! not read`
with an explanation. The evaluator never fabricates a value for bytes it could
not read.

It does **not** check semantics — that a "magic" really matches, that a length
field agrees with a text field, or that the file is really that format. Those
require a parser with documented rules. Treat a template as a lens, not a
verdict.

## Editing

**Structure → Edit…** opens the template editor with two tabs describing the
same template:

- **Form** — build a layout without learning the schema: template name,
  description and default byte order, plus a field tree with *Add field*,
  *Add nested field*, *Edit*, *Remove*, *Move up* and *Move down*. The field
  dialog covers name, type, offset, length (text/bytes only), a per-field byte
  order override, array count and description.
- **JSON (advanced)** — the schema above, for anything the form does not cover.
  JSON remains the saved format, so the two views never diverge.

Switching tabs carries the layout across. Invalid JSON is never thrown away: the
dialog stays on the JSON tab and reports the error. To build a nested struct,
add a field with type `struct`, select it and use *Add nested field*; child
offsets are relative to that struct, not the file.

- **Validate** parses the active tab and evaluates it against the current file,
  reporting schema and bounds problems.
- **Apply** shows the validated template in the tree without saving it.
- **Save template…** writes indented JSON to the user template folder (default
  name derived from the template id) and reloads the list.

Session templates that are applied but not saved are labelled "Session template
(not saved to disk)" and disappear when the file closes.

## Current limits

- Absolute base is always offset 0; there are no per-template start offsets yet.
- Lengths and offsets are static values; expressions that reference other fields
  are not supported.
- Drag-and-drop reordering is not available; fields move with *Move up* and
  *Move down* in the form.
- No unions/overlaps control (overlaps are allowed and coverage counts each byte
  once), no bitfields, no string encodings beyond ASCII/UTF-8, no conditionals.
- Struct arrays must be homogeneous and fixed-size.
