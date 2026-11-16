# Design system

PKG Studio uses one role-based token system for every screen. Widgets never hardcode colours;
the active theme is rendered into a single application style sheet.

## Tokens (Void Black, default)

| Token | Value | Use |
| --- | --- | --- |
| `window` | `#0a0a0a` | application background |
| `panel` | `#101010` | sidebars, headers, dialogs, inspector |
| `alternateBase` | `#101010` | alternate rows, hover surfaces |
| `button` | `#151515` | buttons, scrollbar handles |
| `base` | `#0a0a0a` | editor/hex canvas |
| `border` | `#1c3b28` | subtle green borders and separators |
| `accent` | `#00ff41` | primary signal colour: selection outline, active tab, links |
| `highlight` | `#123b24` | list/tree selection |
| `statusBar` | `#101010` | status bar background |
| `statusBarText` | `#00ff41` | status bar text |
| `hexOffset` | `#4ad66d` | offset gutter |
| `hexBytes` | `#e8e8e8` | hex digits |
| `hexAscii` | `#ff5c7a` | ASCII column |
| `hexSelection` | `#123b24` | byte selection |
| `hexSearch` | `#4a1420` | search hits |
| `gutter` | `#151515` | muted/metadata text |

The optional cyberpunk red (`#ff003c`) is reserved for destructive actions and search-hit
variants; it is not a general text colour.

`windowText` (`#e8e8e8`) on `window` and `panelText` (`#c9d1c9`) on `panel` both exceed WCAG AA
(4.5:1) and AAA for body text; the automated theme test asserts a 7:1 ratio for primary text.

## Themes

Built in: **Void Black** (default), PKG Studio Dark, PKG Studio Light, Darcula, Cyberpunk,
Neon Hex. User themes are JSON files validated against every required role; the theme editor
edits them live and can import/export.

## Typography

- Editors, hex view and inspectors use a fixed-pitch family. If installed, the first match of
  JetBrains Mono, Fira Code, Cascadia Code/Mono, Source Code Pro, Consolas, Liberation Mono,
  DejaVu Sans Mono is used; otherwise the system fixed font.
- Font size is user-configurable (7–24 pt) and applies to all open editors immediately.
- UI text uses the platform UI font; no display font is bundled.

## Components

- **Header strip**: file name, format, summary, editing toggle. One row, 8 px padding.
- **Sidebar**: 12 px icon column, section header in caps, 6 px padding, selection uses
  `highlight` with an accent left border.
- **Tabs**: flat, active tab gets a 1 px accent top border; close button inline.
- **Inspector**: floating panel bottom-right of the hex view, 1 px border, accent title, key
  column in muted text, values in panel text. Appears on hover or selection.
- **Status bar**: mode, file, format, offset, selection, memory, theme. Muted until a file is
  open; accent text for the current mode.
- **Dialogs**: same tokens, 12 px margins, default button uses accent.

## Accessibility and motion

- Minimum body contrast 7:1 on primary surfaces; focus rings use `accent` borders.
- Every command is reachable from the command palette and has a configurable shortcut.
- Keyboard navigation works in the tree, tabs, hex view (arrows, page, home/end) and dialogs.
- Motion is limited to Qt's standard menu/fade effects, which lightweight mode disables.
  No glitch, flashing or parallax effects are used.
- Colours are never the only signal: modified bytes also show a ● marker in the title and a
  status-bar mode change.
