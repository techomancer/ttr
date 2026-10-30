# tx: Motif pieces every ttr program shares

tx holds the Motif user interface parts that more than one ttr program
uses, so they look and work the same everywhere. Each program still owns
its settings and its dialogs (AGENTS.md: no central control panel); tx
only gives them the same parts.

## The font chooser (`tx/fontchooser.h`)

Fonts are X server fonts in every ttr program, named by XLFD. Typing one
is unfriendly, and each program had its own way: ttrt a family and size
list, ttr a family list with fixed sizes and a Bold toggle, ttrcode a text
field. They now share ttrt's, the most complete:

- families listed once, sorted; scalable ones (Type1, TrueType: pixel
  size 0 in their XLFDs) marked `*`;
- the sizes a family comes in, and the usual sizes (8 to 36) for scalable
  ones; fixed-width families of one height in several widths show them
  ("13 (6x13)");
- among fonts of one family and size: Unicode first, then medium weight,
  then normal width;
- a preview, in colours and at a scale the program chooses.

Options for the program's needs: `FIXED` (fixed-width families only:
terminals, editors), `UNICODE` (ISO 10646 fonts as well as Latin-1),
`AUTOMATIC` (an entry for "the program decides", with its own label, such
as ttrcode's "Same as the editor"), `BOLD` (a toggle; the bold font of the
family and size is looked up, as families name their bold weights
differently). A font the lists do not have (an alias such as `9x15`, a
pattern the server resolves to another family) shows as "As set: ..." and
is kept unless changed. Names the server resolves come back in mixed case
on IRIX (`-Adobe-Helvetica-Bold-R-...`) and are compared in lower case.

`tx_demo` shows the chooser in its configurations.

## The Settings dialog (`tx/pages.h`)

ttr's Settings dialog was the one users liked: the pages' names in a
list on the left, one page at a time on the right, and the buttons along
the bottom. `tx::PagedDialog` is that dialog, and ttr, ttrt and ttrcode
all build theirs with it, so they look and work alike:

- `create()` makes the dialog and an empty page per name; the page shown
  last is shown again when a program builds the dialog anew.
- A page is a column of `tx::frame()`s, a frame a column of `tx::row()`s,
  each row starting with a label of one width (`LABEL_WIDTH`), so the
  controls of every frame line up. `tx::radio()`, `tx::drop_down()`,
  `tx::note()`, `tx::choose()` and `tx::chosen()` fill them.
- `finish()` makes the page area as big as the biggest page, so switching
  pages does not resize the dialog, and adds a note (where the settings
  are saved, or that ttr restarts) and OK, Apply and Cancel as the
  program asks. The program decides what each button does.

Each program still owns its settings and what they mean. Appearance
(the UI scale and fonts) and colours have pages of those names in all
three, so they are where users expect them.

## The UI scale (`tx/scale.h`)

ttr scaled itself for high-resolution screens (`uiScale`,
`docs/wm-design.md`); ttrt and ttrcode stayed at 100%. The scaling moved
here from ttr's `x11/config.cpp`:

- `scale_font()`: an XLFD at a percent of its size, the nearest size the
  X server has, in the family asked for or a similar one.
- `scale_motif_resources()`: the pixel sizes Motif does not take from its
  fonts (shadows, focus highlights, combo box arrows).
- `apply_ui_scale()`: for ttrt and ttrcode, before their widgets are
  made: `ttr::set_ui_scale()`, the program's Motif fonts at the scale,
  17-pixel scroll bars at the scale, and the above. At 100% it changes
  nothing, so X resources keep their say.

ttrt and ttrcode have a `uiScale` setting of their own. Empty, the
default, follows ttr's (`ttr_ui_scale()` reads ttr's settings file), so
one setting does for all three; `scale_drop_down()` is the choice in
their Settings dialogs. Their own pixel sizes go through `ttr::scaled()`.
The font they pick when none is set (`tt::pick_font()`) is scaled too; a
font the user chose stays the size chosen. ttrcode saves its docks'
sizes at 100% (`ttr::unscaled()`), so a layout keeps its proportions
when the scale changes.
