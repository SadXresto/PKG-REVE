# Readable listings (Code tab)

The **Code** tab turns bytes into editable text and back:

```
00000000  7F 45 4C 46 02 01 01 00  00 00 00 00 00 00 00 00  |.ELF............|
00000010  03 00 3E 00 01 00 00 00  80 00 40 00 00 00 00 00  |..>.......@.....|
```

- The first column is the file offset (eight or more hex digits).
- The second column is the byte values, uppercase hex pairs.
- The last column is the printable ASCII rendering; `.` means non-printable.
- Lines starting with `#` or `;` are comments and ignored.

**Reload listing** rebuilds the text from the file (the first 256 KiB). Edit the
hex column, then **Assemble into file** parses the text and applies it as
ordinary undoable byte edits. Nothing touches disk until you save, and saving
still writes a timestamped backup and replaces the file atomically.

## What it does and does not do

It is a byte-level listing, not a CPU disassembler. It does not decode x86-64,
AArch64 or RISC-V instructions, does not decrypt or decompress payloads, does
not rebuild signatures, and cannot change the file length. It works identically
on ELF, PE, ZIP/APK, PSP/PS Vita PKG headers and PS4 PKG headers, because it
does not need to understand the container.

That makes it useful for:

- inspecting and patching documented header fields of a package you own;
- comparing two builds by assembling the same listing over each;
- preparing a patch that you then review in the hex view.

It is not a way around DRM or console security. Use it only on files you are
authorized to modify (see `TERMS.md`).

## AI proposals

The AI assistant can propose edits in the same format, inside a fence:

````
```pkgstudio-edit
00000010  02 00 00 00
```
````

The AI chat panel shows how many bytes a reply changes and offers **Apply
edits**, which uses exactly the same validated path as **Assemble into file**. Edits outside the file are rejected, and every change is undoable.

## Limits

- Listing generation and parsing are bounded to 256 KiB in the editor; the AI
  context sends the first 8 KiB.
- Parsing accepts up to 64 hex pairs per line and merges contiguous lines.
- Comments after `|`, `#` or `;` are not interpreted.
