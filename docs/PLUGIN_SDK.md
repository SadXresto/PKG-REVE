# Plugin SDK — design plan (not implemented)

> **Status: planned, not available.** There is no plugin loader, manifest parser, permission
> enforcement or sample plugin in this build. This document describes the intended design so
> the current parser/theme architecture can grow into it without rewrites. Do not ship or rely
> on plugins until this document says otherwise.

## Goals

- Add format parsers, platform adapters, themes, editor tools and import/export actions without
  rebuilding the application.
- Keep the core stable when a plugin misbehaves: crashes, hangs and exceptions must be contained.
- Make capabilities explicit and consent-based, with clear error reporting.

## Non-goals

- No plugin may decrypt protected content or bypass console security; the host API exposes no
  such primitives, and manifests requesting them are rejected.
- No silent network access: a network permission must be declared and visible to the user.

## Package layout

```
my-plugin/
├── plugin.json          # manifest
├── libmyplugin.so       # native module (optional)
└── scripts/             # optional Python automation entry points
```

```json
{
  "id": "com.example.psx-pkg",
  "name": "PSX PKG support",
  "version": "0.2.0",
  "apiVersion": 1,
  "entry": "libpsxpkg.so",
  "capabilities": ["format-parser"],
  "permissions": ["read-files"],
  "dependencies": { "pkgstudio": ">=0.2.0 <0.3.0" }
}
```

## Host API (proposed)

- `registerParser(factory)` — returns a parser implementing the existing `IFormatParser`
  contract (`probe`, `parse`); results are validated and bounded by the host.
- `registerTheme(json)` — validated against the required role list.
- `registerCommand(id, title, handler)` — appears in menus/palette with a plugin badge.
- `log(level, message)` — routed to the plugin's own log category.
- `readFileRange(path, offset, length)` — permission-gated, size-capped, read-only.

Plugins never receive a raw `FileSource` mapping they can mutate, and never run on the UI thread
for parsing.

## Isolation strategy

1. **In-process, trusted mode (initial):** native plugins are loaded only from the user's plugin
   directory, after an explicit enable action. Parsing runs on worker threads inside try/catch;
   a crash is caught by the host crash handler and the plugin is disabled for the session.
2. **Out-of-process mode (preferred for untrusted plugins):** a small helper process hosts the
   parser and communicates over a length-prefixed pipe with timeouts. A hung or crashed helper
   is killed and reported without affecting the UI.
3. **Python plugins:** executed through a restricted runner with no filesystem or network access
   unless the manifest requests it; results are treated as untrusted data.

## Permissions

| Permission | Meaning |
| --- | --- |
| `read-files` | Read ranges of files the user opened (never arbitrary paths) |
| `write-export` | Write files only through a user-confirmed save dialog |
| `network` | Explicit, off by default, displayed before first use |
| `ui-panel` | Add a sidebar or bottom panel |

Permissions are shown at install time, stored per plugin, and can be revoked.

## Compatibility and failures

- `apiVersion` is checked at load; mismatches disable the plugin with an actionable message.
- Dependency ranges are validated before enabling.
- Every plugin failure is logged with its id and surfaced in Diagnostics; the application keeps
  running and the affected file falls back to raw-byte analysis.
- Plugins are disabled by default after a crash and require re-enabling.

## Sample plugin (planned)

A minimal "text signature" parser that recognises a documented custom container and reports its
header fields, plus a theme-only plugin, will be provided once the loader and permission gate
exist. Until then, new formats live in `src/core/parsers` with tests and documentation.
