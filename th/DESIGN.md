# th: ttr's colours

th is the colour library every ttr program can link with: the window
manager, ttrt, ttrcode, and whatever comes next. It has no dependencies
beyond the C library, except `th/motif.h`, which needs Xt and Motif. It is
written in C++98 with fixed-size buffers: no STL, no exceptions, and none
of ttr's `core/`. ttr's `core/theme.h` is a thin layer over it.

There are two kinds of colours:

| | UI themes (`th/theme.h`) | Text schemes (`th/scheme.h`) |
|---|---|---|
| Colours | Motif widgets and window frames | text views: editors, terminals |
| Files | `*.ad`, the IRIX desktop scheme format | `*.scheme`, `key: #rrggbb` lines |
| Shipped | `data/schemes/ttr` (ttr's), `data/schemes/irix` (the 30 IRIX 6.5 schemes) | `data/schemes/text` |
| Built in | Classic | Light, Dark |
| Yours | `~/.config/ttr/themes` | `~/.config/ttr/schemes` |

Files are looked for in this order:
1. the user's folder;
2. `data/` next to the program's directory, so an uninstalled build in
   `build/` finds the source tree's files (`th::set_program_path(argv[0])`);
3. `$(DATADIR)/schemes/...`, where `make install` puts them.

A name found earlier hides the same name later, so a user's file can
replace a shipped one. Names compare without regard to case.

## UI themes

Each theme gives a colour for each of a dozen roles: the background, the
text, buttons, text fields, lists, scroll bars, toggle fill, and the
active and inactive window frames. A theme may leave a role out, and then
it is derived: specific backgrounds follow the background, and text and
shadows follow Motif's own rule (`th::motif_colors`, Motif's
CalculateColorsRGB). IRIX scheme files have one value per display gamma.
th picks `gamma1_0` everywhere except IRIX, where it reads the system's
gamma (usually `gamma1_7`).

There are two ways to apply a theme:
- `th::apply_theme(&db, theme)` puts it in the resource database before
  widgets are created. The window manager and ttrcode do this at startup.
- `th::recolor(widget, theme, skip)` recolours existing widgets in place
  with `XmChangeColor`, including their popups (menus, dialogs). This is
  how ttrcode's UI theme (Tools > Settings > Colors) changes without a
  restart. Widgets of class
  `skip` (TwGrid, which has its own colours) are left alone.

Each program chooses its own theme, as everything in ttr does:
- ttrcode's and ttrt's `uiTheme` settings default to "Same as ttr",
  which reads the `theme` line of ttr's settings file (`tx/ttr.h`).
- Programs never change each other's settings.

## Text schemes

A scheme has:
- text and background;
- cursor;
- selection text and background;
- line numbers (the gutter);
- the search hit;
- control characters;
- five syntax colours (comment, keyword, string, number, preprocessor),
  for ttrcode's coming syntax colouring;
- the terminal's 16 colours.

A scheme file may be as short as one background line. The rest is
derived:
- the text colour from the background's brightness;
- the cursor from the text colour;
- the gutter as a mix of text and background;
- the selection and syntax colours from light or dark defaults;
- the terminal colours from xterm's.

Who uses them:
- ttrcode: the editor and its terminal pane (Tools > Settings > Colors,
  live).
- ttrt: its `colorScheme` setting (Settings > Colors > Scheme). Text,
  background and cursor colours set by hand still win over the scheme.

## Tests

`make th-test`, run from the top of the source tree, covers:
- colour parsing and derivation;
- loading the shipped themes and schemes;
- listing and finding them by name;
- derived scheme colours.
