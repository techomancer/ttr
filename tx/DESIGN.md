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
