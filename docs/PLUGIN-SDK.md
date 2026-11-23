# PKG Studio plugin SDK (API version 1)

PKG Studio plugins are **declarative packages**. In 0.11.0 the Extension Studio
validates, audits and packages them. It never loads, links, interprets or runs
plugin code, and no permission is granted at runtime. This is a deliberate
limitation, not an unfinished control: there is no sandbox for native code in
this build.

## Folder layout

```
plugins/
  com.example.hello-toolkit/
    plugin.json        manifest
    actions.json       declarative entry file
    README.md
```

User plugins live under the application data directory:

- Linux: `~/.local/share/PKGStudio/plugins/`
- Windows: `%APPDATA%\PKGStudio\plugins\`
- macOS: `~/Library/Application Support/PKGStudio/plugins/`

`Settings::dataDirectory()` resolves this location; the Extension Studio shows
it in the empty state.

## Manifest

```json
{
  "id": "com.example.hello-toolkit",
  "name": "Hello Toolkit",
  "version": "0.1.0",
  "apiVersion": "1",
  "description": "Declarative example plugin.",
  "author": "Your name",
  "entry": "actions.json",
  "permissions": ["files.read"],
  "capabilities": ["declarative-commands"],
  "dependencies": [
    { "id": "com.example.base", "minimumVersion": "1.0.0" }
  ]
}
```

| Field | Rules |
| --- | --- |
| `id` | 2-64 lowercase letters, digits, `.`, `_` or `-`; starts alphanumeric |
| `name` | required |
| `version` | `MAJOR.MINOR.PATCH`, optional pre-release/build suffix |
| `apiVersion` | must be `1` |
| `entry` | relative path inside the plugin folder; JSON is parsed during validation |
| `permissions` | subset of the permissions below |
| `capabilities` | free-form labels for the audit screen |
| `dependencies` | plugin ids with a semantic minimum version |

## Permissions

| Id | Declared intent |
| --- | --- |
| `files.read` | Read files the user explicitly opens |
| `files.write` | Write files the user explicitly saves |
| `network` | Contact a network endpoint |
| `process.spawn` | Start an external process |
| `clipboard` | Read or write the clipboard |
| `ai.prompt` | Send a prompt to the configured AI endpoint |

Permissions are documentation for review. `process.spawn` and `files.write`
produce validation warnings because they cannot be honoured in this build.

## Validation

`plugins::validateManifest` checks:

- id format, required name, semantic version, supported API version;
- the entry path is relative, does not escape the folder, exists, is at most
  512 KiB and parses when it is JSON;
- permissions are known, duplicates are warned about;
- dependency ids and minimum versions are valid, are not self-references, and
  resolve against the installed manifests at a sufficient version.

## Packaging

`plugins::exportBundle` copies the plugin into
`<destination>/<id>-<version>/`, writes a normalised `manifest.json`, adds the
SDK README when none exists and writes `SHA256SUMS.txt` with one digest per
copied file. Files above 25 MiB are skipped and reported. Existing export
folders are not overwritten; the source folder is never modified.

## Testing a plugin

The Extension Studio's validation workflow is the test: manifest structure,
entry parsing, version and dependency resolution, permission audit and bundle
export with checksums. Because plugin code is not executed, there is no runtime
behaviour to test in this version.
