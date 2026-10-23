# tt: the terminal pane

`tt::TermPane` is a terminal packaged as a Motif widget tree, ready to
embed in a program. ttrt is a menu bar and a settings dialog around one
pane. ttrcode docks one or more panes beside its editors.

A pane is assembled from three of ttr's libraries:

- tl: emulation (bytes to screen state, keys to bytes).
- tw: drawing. `tt::TermSource` adapts a `tl::Terminal` to `tw::Source`,
  and the TwGrid widget draws it.
- tp: the pty and the child process.

## What the pane does

- It makes an XmForm with the TwGrid and, optionally, a vertical
  XmScrollBar for the scrollback. The host attaches the form wherever it
  likes.
- It starts the program when the grid first has a size on screen, so
  the pty starts with the real size. A host that realizes late calls
  `start()`.
- The program's I/O goes through Xt inputs:
  - Output from the program is read in bounded chunks, so X events
    still get through.
  - Bytes for the pty wait in a queue when the pty doesn't take them.
- Keys go through tl's encoders. The host sees each key first
  (`PaneHost::pane_key`); this is how ttrcode keeps its global shortcuts.
  The pane itself handles:
  - Shift+PgUp/PgDn: scroll back.
  - Ctrl+Shift+C/V: copy and paste with CLIPBOARD.
  - Shift+Insert: paste PRIMARY.
- The mouse works as in xterm:
  - Selection: click, double-click for a word, triple-click for a line.
    Dragging past the edge scrolls.
  - Button 2 pastes. Button 3 extends the selection.
  - The wheel scrolls back, or sends arrow keys in full-screen programs.
  - Mouse reporting goes to programs that ask for it; Shift overrides it.
- Selections:
  - Each pane owns PRIMARY and CLIPBOARD on its own. The selection
    callbacks find their pane through the grid's XmNuserData.
  - Paste asks for UTF8_STRING first and falls back to STRING (Latin-1)
    for IRIX's own clients.
- The bell can be off, audible, or visual (reverse video for 100 ms).

## What the host does

The host implements `tt::PaneHost`:

- `pane_exited(pane, code)` (required): ttrt exits with the code.
  ttrcode leaves the pane's last screen up and offers to restart.
- `pane_title`: the window or tab title (OSC 0/1/2).
- `pane_context_menu`: Ctrl+button 3.
- `pane_key`: shortcuts the host takes before the terminal sees them.
- `pane_focus`: focus in and out of the pane.

Menus, settings files, fonts and window size hints also belong to the
host. The pane provides the commands (`copy`, `paste`, `select_all`,
`clear_scrollback`, `reset`), the settings that apply at once (font,
colours, the 16-colour palette, cursor colour, bold-is-bright, backspace,
bell), and the sizes
(`cell_size`, `extra_size`).

`tt::pick_term()`, `tt::pick_font()` and `tt::font_path()` choose
TERM/TERMINFO, an automatic font, and TwGrid's BDF fallback path the same
way for every host.
