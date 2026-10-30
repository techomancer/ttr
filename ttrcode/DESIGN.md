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

- The window is nested XmForms: the menu bar, the status bar, and
  between them the workspace on the left and, to its right, the editor
  over the bottom pane.
  - The workspace keeps its width and the bottom pane its height when
    the window grows; the editor takes the rest, as in VS6.
    (XmPanedWindow shares extra room among its panes, and its horizontal
    mode left part of a wide window empty in Open Motif 2.3.)
  - Between the panes are sashes, XmSeparators that follow a dragged
    pointer. The sizes are saved in the settings.
- Each pane can be hidden and shown again from the View menu or its key:
  - Alt+0: the workspace.
  - Alt+2: the bottom pane.
- **Docking:** tool panes live in three dock areas around the editor
  (section 4.1).
- **Tabs** are toggle buttons kept as radio buttons by ttrcode, each
  with a close button (or a middle click) beside it.
- **Distraction-free mode** (Shift+Alt+Enter; F11 until the debugger
  took it for Step Into, td/DESIGN.md section 11):
  - Hides everything except the current editor: the menu bar, the
    tree, the tabs, the bottom pane and the status bar.
  - Asks the window manager for full screen through
    `_NET_WM_STATE_FULLSCREEN` (ttr's window manager supports it). mwm
    and 4Dwm do not know it, but read `_MOTIF_WM_HINTS` when a window is
    mapped: there the window is withdrawn, loses its decorations, takes
    the screen's geometry and is mapped again, and the reverse on the
    way back.
  - Shift+Alt+Enter, Esc or F11 brings everything back exactly as it was.

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
| Shift+Alt+Enter | distraction-free full screen |
| F5, F9, F10, F11 | the debugger (td/DESIGN.md section 8.2) |
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
  current directory. File > Open Workspace... changes it (a file
  selection box showing directories). Open documents stay open. The
  terminal's shell gets a `cd` only when it is at its prompt (the pty's
  foreground process group is the shell's own, `TermPane::at_prompt`), so
  a running command never receives typed text.
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

## 4. Tool panes

### 4.1 Dock areas, groups and tabs

Decided 2026-10-26, for the debugger's windows (td/DESIGN.md 8.2) as
much as for the panes there are now, modelled on Visual C++ 6:

```
+-----------+----------------------------------+-----------+
| left area | [main.cpp] [view.cpp]   (center: | right     |
|           |   documents; Disassembly opens   | area      |
| group     |   here as a document tab)        | group     |
| [Workspa] |                                  | [Call St] |
|           +----------------------------------+ [Threads] |
|           | bottom area: groups side by side +-----------+
|           | [Terminal|Find Results] | [Variables] | [Watch]|
+-----------+----------------------------------+-----------+
```

- **Three areas**, left, bottom and right, around the documents. The
  bottom one spans the documents' column only, as the terminal pane
  always did. An area with nothing in it takes no room.
- **An area holds groups**, tiled along it: one over another on the
  left and right, side by side at the bottom, with draggable sashes
  between them and along the area's inner edge.
- **A group is a tab set**: a row of tabs and the chosen pane under it.
  A group always shows its tabs, so a single pane has its title there
  and the right-click menu works the same everywhere.
- **Moving a pane**: the tab's right-click menu has *Move to* with each
  area: into one of its groups, or a new group of its own; and *Close*.
  A closed pane keeps where it was, and coming back (its View menu item
  or key) puts it there again. Dragging tabs onto areas may come later;
  the menu does the same.
- **Panes are rebuilt, not reparented.** Xt cannot give a widget a new
  parent, so a pane is an object that makes its widgets on whatever
  parent it is given, and moving it destroys them and makes them again
  in the new group. The workspace tree and Find Results fill again from
  ttrcode's own lists; the terminal keeps its terminal and its pty
  (`tt::TermPane::rebuild`) and only its widgets are made again. The
  same is what floating panes in their own windows will need.
- **The layout is a line of text** in the settings:
  `left:220=workspace;bottom:220=terminal,results;right:240=` — each
  area's size, its groups separated by `|`, each group's panes by `,`,
  and a group's size (all but an area's last) after `@`.
- **Two layouts, editing and debugging**, as VC6 had: the debug panes
  (Variables, Watch, Registers, Memory, Call Stack, Threads) appear when
  debugging starts and go when it ends, each where it was last time.
  The debugging layout's default:
  - left: Workspace;
  - bottom: [Terminal · Find Results] | [Variables] | [Watch];
  - right: [Call Stack · Threads] over [Registers];
  - Memory goes to the bottom area the first time it is opened, and
    Disassembly to the right one.
- **Keys** (VC6's): Alt+0 the workspace, Alt+2 the bottom area, Alt+3
  Watch, Alt+4 Variables, Alt+5 Registers, Alt+6 Memory, Alt+7 Call
  Stack, Alt+8 Disassembly; Shift+Alt+Enter hides every area (section 1).

### 4.2 The terminal

A `tt::TermPane` running the user's shell in the workspace directory.
It takes every key except ttrcode's global ones (Ctrl+`, Alt+0, Alt+2,
Shift+Alt+Enter), which it gives up through `PaneHost::pane_key`.

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

## 6. Settings and colours

The settings are ttrcode's own, in the same spirit as ttrt:
- They live in `~/.config/ttrcode/settings`, which wins over X resources
  of the same names (`TTRcode.font`, `.tabWidth`, …).
- The View menu and the sashes write them as they change. Tools >
  Options (milestone 2) will edit the rest.

Colours come from th (`th/DESIGN.md`), shared by every ttr program:
- The **colour scheme** colours the editor and the terminal pane, and
  changes at once. Light and Dark are built in; more are files.
- The **UI theme** colours the Motif widgets: ttr's themes and the IRIX
  schemes. It changes at once too (`th::recolor`). By default it is the
  theme ttr's settings name, so ttrcode matches the desktop until the
  user picks another.

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
2. **Settings.** Done: Tools > Options (font, tabs, indentation, line
   numbers, current line, whitespace, column marker, cursor). Still to
   do: remembering the open files per workspace, recent files and
   workspaces. Remembering open files and pane sizes
   per workspace. Recent files and workspaces.
3. **Colours.**
   - Done: syntax colouring for C and C++, shell, make and Python
     (`te/syntax.h`: a per-line lexer state that te feeds into
     `View::row()`, so an edit relexes only from the edited line).
     Markdown is still to come.
   - Matching braces (Ctrl+]).
   - Done: visible whitespace, the current line and a column marker.
4. **Build.**
   - F7 runs the build command (`make` by default) in an Output pane,
     not in the terminal.
   - `file:line:` messages become links. F4 and Shift+F4 go to the next
     and previous error, as in VS6.
   - Ctrl+Break stops the build.
5. **Finding.**
   - Done: Find and Replace in Files (Ctrl+Shift+F, Ctrl+Shift+H) with
     a Find Results page in the bottom pane and F4 / Shift+F4. The search
     runs in the process for now; a child process comes when workspaces
     get big enough to need one.
   - Quick open by name.
   - Bookmarks: Ctrl+F2 sets one, F2 and Shift+F2 go to the next and
     previous.
6. **Navigation.**
   - ctags: F12 goes to the definition and Ctrl+- goes back.
   - Split editors: two views of one document, or two documents side
     by side.
   - Floating panes in their own windows; dragging tabs between areas.

Not planned: extensions, language servers in the first versions, a web
view, telemetry, and auto-update.
