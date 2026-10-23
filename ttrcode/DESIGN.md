# ttrcode: a code editor for ttr

ttrcode is a Motif code editor for IRIX and Linux. It aims for what Visual
Studio 6.0 got right and modern editors lost:

- It starts in well under a second, even on an Indy.
- Typing never waits for anything.
- Every key does the same thing every time. There are no popups the user
  didn't ask for, no telemetry, and no extension host.
- Windows stay where the user put them.

It is built from ttr's own modules. Nothing in it is a browser.

| Module | Role in ttrcode |
|---|---|
| [`te/`](../te/DESIGN.md) | the text: documents, undo, search, views, mc-style blocks |
| [`tw/`](../tw/DESIGN.md) | drawing: TwGrid draws every editor (ttrt's renderer) |
| [`tt/`](../tt/DESIGN.md) | the terminal pane (ttrt's terminal) |
| Motif | everything else: menus, panes, tree, dialogs, status bar |

The language and portability rules are the same as the rest of ttr: C++98
for MIPSpro 7.4 and `g++ -std=c++98 -pedantic`, no STL, no exceptions,
Motif 2.1 as the floor. ttrcode is one process with one thread and one Xt
event loop. Anything slow (builds, searches) runs in a child process
whose output arrives through the event loop, the same way ttrt's pty
output does.

---

## 1. The window

```
+--------------------------------------------------------------------+
| File Edit Search View Build Window Help                  (menu bar)|
+-------------+------------------------------------------------------+
| Workspace   | [main.cpp] [view.cpp*] [Makefile]          (tab bar) |
|  src/       |  12 | int main(int argc, char** argv)                 |
|   main.cpp  |  13 | {                                    (editor:   |
|   view.cpp  |  14 |     App* a = new App();               TwGrid +  |
|  Makefile   |  15 |                                       scroll    |
|             |     |                                       bar)      |
|  (XmList)   +------------------------------------------------------+
|             | Terminal / Output                          (tt pane) |
|             | $ make                                              |
+-------------+------------------------------------------------------+
| Ln 14, Col 5    INS    UTF-8  LF    view.cpp            (status bar)|
+--------------------------------------------------------------------+
```

- The window is an XmForm made of four parts:
  - the menu bar;
  - a horizontal XmPanedWindow: the workspace tree, and the editor
    area with the bottom pane;
  - inside that, a vertical XmPanedWindow: the editor area over the
    bottom pane;
  - the status bar.
- Panes are resized with the sashes. Each pane can be hidden and shown
  again from the View menu or its key:
  - Alt+0: the workspace.
  - Alt+2: the bottom pane.
- **Docking** in the first version means these fixed slots, each of
  which can be shown, hidden and resized. Undocking panes into their own
  windows, and more slots, come later (section 7). The slots are
  ttrcode's own XmForm containers, so moving a pane from one slot to
  another is a reparent, not a redesign.
- **Distraction-free mode** (F11):
  - Hides everything except the current editor: the menu bar, the
    tree, the tabs, the bottom pane and the status bar.
  - Asks the window manager for full screen through
    `_NET_WM_STATE_FULLSCREEN` (ttr's window manager supports it). On
    other window managers it covers the screen with the shell's own
    geometry.
  - F11 or Esc brings everything back exactly as it was.

## 2. Editors

- **One TwGrid draws every document.** Each open document is a
  `te::Document` with its own `te::View`, which holds its cursor, its
  selection and its scroll position. Switching tabs means pointing the
  grid at another document's view and drawing one frame. No widgets are
  created or destroyed.
- **EditSource** (`src/ttrcode/editsource.*`) is the `tw::Source` over
  a `te::View`. On each frame it:
  - lays out the visible rows (a gutter with line numbers, then the
    cells from `View::row()`);
  - compares them with the previous frame, row by row, and reports only
    the columns that changed;
  - turns a change of the view's top line into a scroll record, which
    tw copies on the server instead of redrawing;
  - redraws the cell the caret just left.

  A keystroke therefore costs one row of drawing. Scrolling costs one
  `XCopyArea` plus the new rows. tw's renderers do the rest, as they do
  for ttrt.
- Fonts: any fixed-width X font, plus tw's bundled BDF fallback for
  characters the font lacks (IRIX servers have only Latin-1 fonts).
- The caret is a blinking bar, and it does not blink while you type.

### Keys (Visual Studio 6)

| Key | Does |
|---|---|
| arrows, Home, End, PgUp, PgDn | move; Shift selects; Home goes to the first non-blank, then to column 0 |
| Ctrl+Left/Right | word left/right |
| Ctrl+Home/End | start/end of the file |
| Ctrl+Up/Down | scroll without moving the caret |
| Tab, Shift+Tab | indent; with several lines selected, indent/outdent them |
| Ctrl+Z, Ctrl+Y | undo, redo (also Alt+Backspace, Ctrl+Shift+Z) |
| Ctrl+X, Ctrl+C, Ctrl+V | cut, copy, paste (CLIPBOARD; Shift+Del, Ctrl+Ins, Shift+Ins too) |
| Ctrl+A | select all |
| Ctrl+L, Ctrl+Shift+L | cut line, delete line |
| Ctrl+F, F3, Shift+F3 | find, find next, find previous |
| Ctrl+H | replace |
| Ctrl+G | go to line |
| Ctrl+N, Ctrl+O, Ctrl+S | new, open, save |
| Ctrl+F4, Ctrl+Tab, Ctrl+Shift+Tab | close the document, next and previous document |
| F11 | distraction-free full screen |
| Alt+0, Alt+2 | show or hide the workspace, the bottom pane |
| Ctrl+` | to the terminal and back |

The mouse:
- Click places the caret. Drag selects, and dragging past an edge
  scrolls. Double-click selects a word.
- Shift+click extends the selection.
- The wheel scrolls three lines.
- Button 2 pastes PRIMARY, as everywhere on X.

## 3. The workspace tree

- The workspace is a directory: the command-line argument, or the
  current directory.
- The tree is an XmList with indented names:
  - Directories come first and open or close with Enter or a double
    click.
  - Files open with Enter or a double click.
- An XmList stays fast with thousands of entries, and its keyboard
  behaviour is the same in every Motif application. XmContainer's
  outline mode exists in Motif 2.1, but it is slower and behaves
  differently from one implementation to another.
- Directories are read only when they are opened.
- Names that start with `.` (`.git` among them), and build output
  directories (`build`, `build-irix`, `build-asan`, any `build-*`), are
  hidden.

## 4. The bottom pane

A `tt::TermPane` running the user's shell in the workspace directory.
Later the same slot also holds the Output pane for builds (section 7).
The terminal pane takes every key except ttrcode's global ones (Ctrl+`,
Alt+0, Alt+2, F11), which it gives up through `PaneHost::pane_key`.

## 5. Files

- Files are read and written with stdio, as bytes. te keeps CRLF files
  CRLF, and the status bar shows `LF` or `CRLF`.
- Saving writes a temporary file next to the original and renames it
  over the original, so a full disk never leaves half a file. The
  original's mode is kept.
- When the window gets the focus again, documents that changed on disk
  are reloaded if they have no unsaved changes. A document that does have
  unsaved changes gets a warning that saving will overwrite the other
  version.
- Closing a modified document, or quitting with modified documents, asks
  Save / Discard / Cancel.

## 6. Settings

The settings are ttrcode's own, in the same spirit as ttrt:
- They live in `~/.config/ttrcode/settings`.
- They are edited in Tools > Options.
- They are also available as X resources (`TTRcode.font`, `.tabWidth`,
  …).

The first version reads only the resources: font, colours, tab width,
spaces or tabs, and line numbers.

## 7. Milestones

1. **The window.**
   - The layout from section 1: the tree, tabs, one editor, the
     terminal pane and the status bar.
   - The editing from section 2.
   - Open, save, save as, new, close, with the questions about unsaved
     changes.
   - Find, replace and go to line dialogs, and the clipboard.
   - F11.
   - Builds and runs on Linux and on IRIX.
2. **Settings.** The settings file and the Options dialog (font,
   colours, tabs, line numbers). Remembering open files and pane sizes
   per workspace. Recent files and workspaces.
3. **Colours.**
   - Syntax colouring for C and C++, shell, make, Python and Markdown.
     It is a per-line lexer state that te feeds into `View::row()`, so
     an edit relexes only until the state settles.
   - Matching braces (Ctrl+]).
   - Visible whitespace, as an option.
4. **Build.**
   - F7 runs the build command (`make` by default) in an Output pane,
     not in the terminal.
   - `file:line:` messages become links. F4 and Shift+F4 go to the next
     and previous error, as in VS6.
   - Ctrl+Break stops the build.
5. **Finding.**
   - Find in files (Ctrl+Shift+F) runs in a child process, with
     results in the bottom pane.
   - Quick open by name.
   - Bookmarks: Ctrl+F2 sets one, F2 and Shift+F2 go to the next and
     previous.
6. **Navigation.**
   - ctags: F12 goes to the definition and Ctrl+- goes back.
   - Split editors: two views of one document, or two documents side
     by side.
   - Undocking panes into their own windows.

Not planned: extensions, language servers in the first versions, a web
view, telemetry, and auto-update.
