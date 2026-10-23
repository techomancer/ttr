# Text colour schemes

Colour schemes for ttr's text views: ttrcode's editor and terminal pane,
and ttrt. "Light" and "Dark" are built into the programs; these files add
more. Your own go in `~/.config/ttr/schemes/` with the same format, and a
file there with the name of one of these replaces it.

A scheme is lines of `key: #rrggbb` (`!` or `#` at the start of a line
starts a comment):

| Key | Colours |
|---|---|
| `name` | the name shown in menus (default: the file name) |
| `isDark` | `true` or `false` (default: from the background) |
| `foreground`, `background` | text |
| `cursor` | the caret or the terminal's cursor |
| `selectionForeground`, `selectionBackground` | selected text |
| `gutterForeground`, `gutterBackground` | line numbers |
| `findBackground` | the last search hit |
| `control` | control characters shown as `^X` |
| `comment`, `keyword`, `string`, `number`, `preprocessor` | syntax colours (ttrcode) |
| `currentLine` | the background of the line with the caret (ttrcode) |
| `ruler` | the column marker line (ttrcode) |
| `whitespace` | visible spaces and tabs (ttrcode) |
| `color0` ... `color15` | the terminal's 16 colours |

Left-out keys are derived from the ones given (see `th/scheme.h`).
