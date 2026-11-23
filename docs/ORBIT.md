# Orbit update (0.11.0)

Orbit extends PKG Studio's project intelligence and developer automation. Every
feature below is implemented in this build; limits are stated where they exist.

## Project Navigator

`core/projectnav/ProjectNavigator` merges the existing project scan, parsed
build configuration, dependency graph, portability findings and DevFlow build
diagnostics into one read-only index.

- Entity kinds: file, build target, dependency, configuration file, source
  reference, portability finding and build error.
- Every relationship records evidence (`file:line` or the scan it came from)
  and a confidence class:
  - **confirmed** — stated literally in a build file or observed in a scan;
  - **inferred** — derived by a parser or a scanner match;
  - **weak** — name similarity only, never a build-file statement.
- Limits: 2000 files, 4000 entities, 8000 relationships, 400 build errors.
  Truncation is reported, never hidden.
- The index is rebuilt when a project opens and when a DevFlow build finishes.

## AI code actions

The docked chat gains an action selector:

| Action | Context | Output |
| --- | --- | --- |
| Ask about this file | file listing (if enabled) | answer, optional byte edits |
| Explain selected bytes | bounded selection listing | explanation |
| Explain last build error | tail of the last DevFlow build output | explanation and next step |
| Suggest a refactoring | file listing | description, optional edits |
| Generate documentation | file listing | documentation |
| Propose a patch | file listing (<256 KiB) | complete file in a `pkgstudio-file` block |

Patch proposals open a diff review dialog. Nothing is written until the user
presses **Apply to editor**, and this build only edits existing bytes, so a
size-changing proposal can be saved separately but never applied in place.
Remote endpoints require a per-session confirmation before project content is
sent; loopback endpoints stay on the machine.

## Test & Debug Center

Detection covers CTest/CMake registration, GoogleTest, Catch2, pytest, an npm
`test` script and Cargo.toml. Only detected frameworks are listed, and a
detected framework without a runnable command shows the reason.

Runs use `QProcess` with a program and argument list (no shell), stream output,
support cancellation with a kill grace period, and parse CTest, GoogleTest,
pytest, cargo and npm-style output. Failures are grouped by a normalised
message fingerprint. The last 60 runs are stored in the local `devflow` table
and compared with the previous run of the same framework. Reports export as
Markdown. Output that does not match a known format is shown raw with no
invented per-test results.

## Smart project diff

Compares two folder trees without writing to either. It reports added, removed,
modified and unchanged files; configuration edits with isolated lines;
dependency version changes; bounded binary differences with the first differing
offset; and portability occurrence deltas. Content is hashed up to 4 MiB per
file, and a same-size match beyond that is labelled sampled rather than
identical. Markdown export is available.

## Extension Studio

Manifest editor, permission audit, semantic-version and dependency checks, a
ready-to-validate example plugin, and checksummed bundle export. Plugin code is
not executed. See [PLUGIN-SDK.md](PLUGIN-SDK.md).

## Product experience

- Three-step welcome guide on first launch (never during screenshots or
  self-tests), stored through `ui.onboarded`.
- Sample project and getting-started guide reachable from the guide.
- Empty states with a next action for the Navigator, Test Center and Extension
  Studio.
- F1 opens help for the active workspace; new help topics cover the Navigator,
  Test Center, smart diff, plugin SDK and DevFlow.
- Every new command is in the command center, menus and the palette.

## Known limitations

- Patch proposals cannot change file size.
- Plugins are declarative only; no runtime, no sandbox, no permission grants.
- GoogleTest and Catch2 run through CTest and need a configured build folder.
- Binary diffing stops at the comparison budget and says so.
- No Windows build was executed in this environment (see the packaging notes).
