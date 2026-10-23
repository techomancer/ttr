# te: the text editing core

te is the editor without a screen. ttrc's F4 editor draws it with tu in a
terminal; ttrcode draws it with tw in a Motif window. Both get the same
editing behaviour from one place, and only the drawing, the keys and the
file handling differ between them.

Like the rest of ttr's portable code, it is C++98 for MIPSpro 7.4 and
`g++ -std=c++98 -pedantic`, with no STL, exceptions or RTTI.

## Document (`te/document.h`)

- The text is an array of lines of bytes. te never rejects bytes. UTF-8
  is decoded where columns matter, and a byte that is not valid UTF-8
  counts as one character.
- Line ends are `\n`, or `\r\n` when every line in the file has it. The
  file is saved the way it was loaded.
- `insert`, `erase` and `get` work between `Pos{line, byte}` positions.
- Undo and redo work in groups. `begin_group()` and `end_group()` nest,
  so a command made of several edits (replace all, moving a block, Enter
  with its indentation) is undone in one step. Consecutive typing merges
  into one step until the cursor jumps or a line ends.
- `modified()` compares the current undo state with the saved one, so
  undoing back to the saved text counts as unmodified.
- `version()` changes with every edit, undo and redo. Renderers use it
  to know when to redraw, and views use it to know whether their marks
  are still valid.
- Search runs within a line, wraps around, and can match case or not.
  Replace all is one undo step.

## View (`te/view.h`)

A view is one window onto a document. A document can have several views
(split panes in ttrcode).

- The cursor and the selection: the selection runs from the anchor to the
  cursor. `extend` keeps the anchor, as Shift does.
- Moves: characters, words, Home (to the first non-blank, then to column
  0, as in Visual Studio), End, pages, and the start and end of the
  document. Up and Down keep the wanted column.
- Commands: typing (replaces the selection), Enter with auto-indent,
  Backspace, Delete, Tab (with several lines selected, Tab indents them
  and Shift+Tab outdents them), delete line, copy, cut, paste, find,
  replace all, go to line, undo and redo, and selecting the word at a
  position (a double click).
- Settings: `tab_width` (the columns a tab character shows as),
  `indent_width` (how far Tab indents with spaces; 0 means the tab
  width), `insert_spaces` and `auto_indent`.
- mc's blocks: `mark_block()` turns the selection into a block that stays
  where it is while the cursor moves. F5 and F6 copy or move the block to
  the cursor, and the copy becomes the new block. Any other edit drops
  the block.
- Layout: `row(y, cells)` gives one screen row as cells. Each cell has a
  code point, a width (1, 2, or 0 for the right half of a wide
  character) and a kind (text, selected, control character, search hit,
  block). Tabs are expanded, and control characters are shown as `^X`.
  The host picks the colours for each kind.
- `follow()` scrolls the cursor into view. Hosts call it after commands.

The host owns everything else: files (ttrc reads and writes them through
tf), the clipboard, key bindings, dialogs and drawing.

## Tests

`make te-test` runs the tests for the document and the view. ttrc's
`editor.drive` and `block.drive` scripts test the tu frontend.

## Later

Syntax colouring as a per-line state that the host feeds into `row()`,
word wrap, rectangular blocks, and a gap buffer or piece table if files
of hundreds of megabytes need it. The line array is fast enough for
source code.
