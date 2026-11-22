# PKG Studio sample workspace

Every binary in this folder is generated from scratch by
[`tools/make_test_packages.py`](../tools/make_test_packages.py); the
`gameforge-demo` subfolder is a small hand-written CMake project. There is no
commercial, copyrighted or console-signed content here, and nothing is
encrypted or extracted. These files exist so the analysis, hex, editing,
structure and save paths can be demonstrated and tested offline.

`gameforge-demo` is the demo project for the GameForge workspace. Open it with
`PKGStudio -p samples/gameforge-demo` to see the build-config inspector,
dependency graph and porting intelligence work on real files. It is synthetic
and not buildable without SDL2; see
[`gameforge-demo/README.md`](gameforge-demo/README.md).

Open this folder with **File → Open sample project** (or **Help → Open sample
workspace**) to show it in the explorer.

## Files

| File | What it demonstrates |
| --- | --- |
| `sample.elf` | ELF64 little-endian header, entry point, one `PT_INTERP` program header. Analysis fields, the signature overlay and the built-in ELF64 structure template all line up. |
| `sample.exe` | PE/PE32+ header, optional header and two sections. |
| `sample.apk` | ZIP-level Android package with a central directory and an `AndroidManifest.xml` entry (binary XML is **not** decoded). |
| `sample_vita.pkg` | PSP/PS Vita PKG header: content ID, item counts and sizes. |
| `sample_ps4.pkg` | PS4 PKG container header. Payloads are encrypted and are never decrypted, extracted or modified. |
| `unrecognized.bin` | Deliberately unrecognized bytes: shows the honest raw-byte fallback instead of a guessed format. |

## Suggested five-minute demonstration

1. **Welcome → Open ELF sample.** The analysis tab opens automatically and
   lists the verified ELF header fields. Note the warning list: it is empty
   only because every parsed field passed its bounds and signature checks.
2. **Hover bytes** in the hex view. The inspector shows offset, hex, decimal,
   binary, ASCII, signed/unsigned integers in both byte orders and floats.
3. **Ctrl+F** for `ELF` or a hex pattern. Search runs on a worker thread and
   selection/copy (`Ctrl+Shift+H`) works on the hits.
4. **Structure toggle** (`Ctrl+Shift+B`). Pick the ELF64 built-in template,
   or **Edit… → Form** and add a field; the tree highlights the matching bytes.
5. **Editing** (optional, on a copy): click *Editing off* in the file header,
   change two hex digits, use `Ctrl+Z`, then save. Saving asks before
   overwriting, writes a timestamped backup and can be cancelled while the
   background write is running.
6. **Open APK sample** repeats the tour for ZIP/APK, including the ZIP local
   header structure template.
7. **What works in this build?** on the Welcome tab opens the honest feature
   status list (implemented / partial / planned).

## Honest limits

- These are minimal synthetic headers, not real applications or games. They
  prove the parsers read the documented fields; they do not represent every
  real-world variant.
- PS4/PS Vita payloads are encrypted in real packages and are not supported.
  PS5 packages are not supported or claimed.
- Editing is same-size only; inserting or deleting bytes is not implemented.
- Structure templates are layouts, not format detection.

Only inspect files you own or are authorized to analyze.
