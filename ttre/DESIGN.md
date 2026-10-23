# ttre: the To The Roots editor

ttre edits one file in a terminal. It is the text-mode, single-file
sibling of ttrcode's editor: the same editing core (te), the same syntax
colouring and colour schemes, and the same editor options. It has mc's
keys and ttrc's look. Everything about workspaces stays out of it: files
of a project, find in files and the like belong to ttrc, which plays the
workspace manager here, and to ttrcode.

```
ttre [+LINE] [file]
```

ttre is a program of its own, in the Unix way:
- ttrc's F4 runs it in ttrc's shell, on the file under the cursor. A
  file inside an archive is edited as a temporary copy.
- `$EDITOR` and `$VISUAL` can name it, so git, crontab and mail
  programs use it.
- ttrc can run another editor instead: Options > Configuration, F4 off
  runs `$VISUAL`, then `$EDITOR`, then `vi`.

ttrc no longer has an editor inside; ttre replaced it.

## Parts

| Part | From |
|---|---|
| Editing: document, undo, search and replace, blocks, indentation | te (`te/document.h`, `te/view.h`) |
| Syntax colouring: C, C++, Python, shell, make | te (`te/syntax.h`) |
| Loading, and saving through a new file renamed over the old one | te (`te/file.h`), shared with ttrcode |
| Terminal, screen, input, dialogs, menus | tu |
| The look of the bars, menus and dialogs | tu's themes (`tu/theme.h`), shared with ttrc |
| Text colours | the theme's, or a th text scheme (`th/scheme.h`), shared with ttrcode and ttrt |
| Cursor shape, blinking and colour | `tu::Term` (DECSCUSR, OSC 12) |

The program itself is small. `src/ttre/editor.cpp` holds the drawing,
the keys and the dialogs, and `settings.cpp` the settings file.

## Colours

- **Default text:** the theme's text style.
  - Blue themes (classic, midnight) get mc's bright syntax colours.
  - Other themes get th's light or dark defaults for their background.
- **With a th scheme** chosen (F9 > Options > Editor > Colors), the
  scheme's text, selection, syntax, gutter, current-line and ruler
  colours are used.
- **The current line and the column marker** are tinted backgrounds,
  shown only where the terminal has 256 or 24-bit colour.
- **On a terminal without colours:** keywords are bold, and everything
  else follows the mono theme.

## Settings

`~/.config/ttre/settings`, written by F9 > Options > Editor:

| Setting | Default |
|---|---|
| `theme`, `terminal` | by the terminal: tu's choice, as in ttrc |
| `colorScheme` | the theme's colours |
| `tabWidth`, `indentWidth`, `insertSpaces`, `autoIndent` | 8, 4, true, true |
| `lineNumbers`, `highlightLine`, `showWhitespace` | false, true, false |
| `rulerColumn` | 80 |
| `cursorShape` (`default`, `bar`, `block`, `underline`), `cursorBlink`, `cursorColor` | default, true, the terminal's |

## Tests

`make ttre-test` drives ttre on a pty (ttrc's `ttrc_drive`). The scripts
in `src/ttre/test` cover typing, saving, undo, the quit question, blocks
and indenting, search with whole words, replace, and the menu. A
`NAME.expect` file next to a script holds what the file must contain
afterwards. ttrc's `editor.drive` checks that F4 runs ttre through the
shell.
