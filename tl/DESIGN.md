# tl: the ttr terminal emulation library

Status: plan written 2026-10-19. Milestone 1 (parser, UTF-8, Unicode
tables, test harness) is done. Milestone 2 is mostly done: the VT102
screen, styles, charsets, alternate screen, DA/DSR/DECRQM, and a runner
for libvterm's fixtures (`tl/test/test_vterm.cpp`, known differences in
`tl/test/data/vterm-known.txt`). Missing from it: the pty-driven
vttest/esctest runs. Milestone 3 is done: reflow (section 5.10), scroll
records and row damage, anchors, the scrollback viewport, colour
resolution, text extraction and the View (`tl/view.h` is the reference
for the presenter API; section 6 below is the original sketch). A
randomized test drives a simulated host that updates its picture only as
the View reports and checks it against a full repaint every frame. All
of it passes on g++, clang++ (ASan, UBSan, MSan) and MIPSpro.

`tl` is a terminal emulation engine with no dependency on an operating
system, a pty, a toolkit or a display. It turns a byte stream into a
screen model with damage reporting and turns key, mouse and paste events
into bytes. `ttrt`, the ttr terminal emulator, is the first host: a Motif
program that owns a pty and paints the model with Xlib. Other hosts in
ttr (a terminal pane in a dialog, a headless test driver, a fuzzer) use
the same library.

The library follows the ttr rules in `docs/wm-design.md` §2: C++98 that
MIPSpro 7.4 accepts, no exceptions, no RTTI, no STL, plain `make`. It
must run on an Indy under IRIX 6.5 as well as on a current Linux box.

---

## 1. Goals and non-goals

Goals, in order:

1. **Correct VT100/VT102/VT220 and xterm emulation** as measured by
   vttest and esctest, with the xterm behaviour chosen wherever DEC and
   xterm disagree. Applications see `TERM=xterm-256color` compatible
   behaviour and, later, a `ttrt` terminfo entry.
2. **Modern extensions** without breaking the base: truecolor, OSC 8
   hyperlinks, OSC 52 clipboard, OSC 7 cwd, OSC 133 semantic prompts,
   bracketed paste, focus events, the kitty keyboard protocol,
   synchronized output (mode 2026), grapheme clustering (mode 2027),
   in-band resize (mode 2048), XTGETTCAP, DECRQSS, XTVERSION.
3. **A screen model that is more than a grid**: stable row identities,
   tracked positions that survive scrolling and reflow, image placements
   (sixel first, kitty graphics and iTerm2 later), hyperlink and prompt
   zones, and damage reporting that a host can turn into blits instead of
   repaints.
4. **Embeddable**: one abstract `Host` interface in, one pull-style
   `View` out. No callbacks fire except from inside `feed()`. The host
   decides policy (clipboard access, resize requests, image storage).
5. **Fast enough on a 150 MHz R4400** and not wasteful on anything else:
   fixed-size cells, no per-cell allocation, interned styles, a parser
   fast path for plain text.

Non-goals:

- Rendering, fonts, colour allocation, pty handling and signals. These
  are host concerns; `tl/pty.cpp` is an optional helper, not part of the
  engine.
- Tabs, splits, scrollback search UI, configuration files. `ttrt` does
  those with Motif.
- ReGIS, Tektronix, DEC locator, DECUDK beyond ignoring them cleanly.
- Multithreading. Like the rest of ttr, everything runs on one thread.

---

## 2. What the others do

A short survey, so the choices in §3 onward have context. The sources
are listed in §12.

| Engine | Parser | Cell storage | Scrollback | Damage | Images | Why not use it |
|---|---|---|---|---|---|---|
| **libvte** (GNOME) | own | ring of rows, GObject | yes | widget-internal | sixel (recent) | Welded to GTK and GLib. Not embeddable without GTK, not portable to IRIX. |
| **libtsm** | Williams' state machine | flat arrays | simple, no reflow | per-cell age counters | none | Near dormant since 4.0 (2020). No hyperlinks, images, truecolor underline, reflow or modern modes. |
| **libvterm** (neovim fork) | Williams' | screen layer keeps a grid of fixed 6-codepoint cells | none inside; `sb_pushline`/`sb_popline` hand rows to the embedder | `damage` callback with CELL/ROW/SCREEN/SCROLL merge modes | none | Callback soup across three layers, no scrollback, 6 codepoints per cell baked into the ABI, no hyperlinks. The three-layer split itself is right. |
| **alacritty_terminal** | `vte` crate, table-driven, `Perform` trait | `Grid<Cell>` ring buffer of rows; `Storage.zero` rotation makes scroll O(1) | same buffer, negative line indices | `TermDamage::Full` or per-line `LineDamageBounds {left,right}`; viewport-relative | none | Rust. The grid and damage design is simple and proven; we borrow it. |
| **kitty** | own C | `CPUCell` (12 B) + `GPUCell` (20 B) in parallel arrays; multi-codepoint text in a `TextCache` by index | `HistoryBuf` of mmap'd segments, ring indices, separate pager history | per-line dirty, `has_dirty_text` | kitty graphics protocol, Unicode placeholders | Python+C, GPU oriented. The text cache idea (index instead of inline codepoints) is good. |
| **ghostty** / libghostty-vt | Williams' + SIMD ESC scan, CSI fast path | `PageList`: linked list of fixed-size pages, 8-byte `Cell` with `style_id` into a refcounted per-page `StyleSet`, graphemes and hyperlinks out of line | pages are the scrollback; pruned per page; optional compression | row dirty bit + page dirty + screen-level flags; render state is pulled by the host and cleaned after a frame | kitty graphics, placeholders | Zig. The most thought-through model: tracked **pins** that survive reflow, interned styles, pull-based render state, policy pushed to the host through callbacks. Too big to port (PageList alone is 8 k lines). |
| **foot** | own C | 12-byte `cell` (`wc` + 8-byte attrs); combining chars via indices above the Unicode range | ring of `row *`, `offset` and `view` | row `dirty`, cell `clean` bit, and a **scroll damage list** (`DAMAGE_SCROLL` with region and count) so the renderer can blit | sixel, per grid, anchored by cell position | Wayland-only C11. The scroll damage list is exactly what an X11 host wants. |

What we take from each:

- **Parser**: Paul Williams' VT500 state machine, as everyone does, with
  the usual extensions (colon sub-parameters, APC passthrough, UTF-8
  decoded before the machine). From ghostty: a fast path for runs of
  printable bytes.
- **Grid**: alacritty's ring of rows with a rotation offset, foot's small
  fixed cell, ghostty's interned styles and out-of-line graphemes.
- **Damage**: alacritty's per-row column bounds plus foot's scroll
  records.
- **Positions**: ghostty's tracked pins, simplified to absolute row
  numbers (§5.3).
- **Host boundary**: libghostty's model. Side effects are callbacks the
  host may leave unimplemented; render state is pulled, not pushed; the
  host decides whether to honour resize and clipboard requests.
- **Layers**: libvterm's parser → state → screen split, but with one
  engine object and one `Host` interface instead of three callback
  tables.

---

## 3. Architecture

```
   bytes from pty                                    bytes to pty
        |                                                 ^
        v                                                 |
  +-----------+   actions   +---------------+  replies    |
  | Parser    | ----------> | Interpreter   | ------------+
  | (§4)      |             | CSI/ESC/OSC/  |             |
  +-----------+             | DCS/SGR/modes |  events     |     +--------+
                            | charsets      | ------------+---> | Host   |
                            +-------+-------+   (bell, title,   | (§8)   |
                                    | edits    clipboard, ...)  +--------+
                                    v
                            +---------------+
                            | Screen (§5)   |  primary and alternate:
                            | rows, cells,  |  grid ring, scrollback,
                            | styles, links,|  cursor, tab stops, modes,
                            | anchors,      |  image placements, damage
                            | images        |
                            +-------+-------+
                                    | pull
                                    v
                            +---------------+        +---------------+
                            | View (§6)     | -----> | presenter     |
                            | rows in       |        | (ttrt: Xlib   |
                            | viewport,     |        |  on a Motif   |
                            | damage, cursor|        |  DrawingArea) |
                            +---------------+        +---------------+

  key / mouse / paste events --> Input encoders (§7) --> bytes to pty
```

One object, `tl::Terminal`, owns all of this. The host:

1. constructs a `Terminal` with a `Host*` and a size;
2. calls `feed(bytes, n)` whenever the pty has data;
3. after `feed()` returns (or on a timer), asks the terminal for a
   `View`, draws what is dirty, and calls `clear_damage()`;
4. encodes input with `Terminal::key()`, `mouse()`, `paste()`, which
   write to the host through the same `write_pty` callback as replies;
5. calls `resize(cols, rows, width_px, height_px)` when its window
   changes.

Nothing in `tl` calls into the host except from inside these calls, on
the caller's thread.

### 3.1 Layout

```
tl/
  DESIGN.md         this file
  tl.h              umbrella header: the public API, nothing else needed
  port.h            fixed-width ints, static assert, TL_UNUSED (mirror of core/port.h)
  parser.h/.cpp     byte -> action state machine, UTF-8 decoder
  unicode.h/.cpp    width, grapheme break, case tables; generated data in
  unicode_data.cpp  (produced by tools/gen_unicode.py from UCD files, checked in)
  charset.h/.cpp    DEC/ISO 2022 charset designation, Latin-1 and DEC graphics maps
  style.h/.cpp      Style, interned StyleTable
  grid.h/.cpp       Cell, Row, Grid ring buffer, reflow
  screen.h/.cpp     Screen: grid + cursor + modes + anchors + images + damage
  terminal.h/.cpp   Terminal: two screens, dispatch glue, resize, reset
  csi.cpp, esc.cpp, osc.cpp, dcs.cpp, sgr.cpp, modes.cpp
                    interpreter, one file per sequence family
  input.h/.cpp      key, mouse, paste, focus encoders (legacy + kitty keyboard)
  image.h/.cpp      image store and placements
  sixel.cpp         sixel decoder -> image
  view.h            read-only accessors handed to presenters
  host.h            the Host interface
  pty.h/.cpp        optional POSIX pty helper (Linux openpty, IRIX _getpty); not linked into libtl
  test/             unit tests, golden tests, headless driver
src/ttrt/           the Motif terminal emulator (host), later
tools/gen_unicode.py  table generator (Python, dev-time only)
```

Objects build to `build/tl_*.o` and `build/libtl.a`, following the
Makefile rules (explicit rule per object, no pattern rules).

### 3.2 Independence from `core/`

`tl` uses only the C library. It does not include anything from
`src/core/`, so another project can lift `tl/` as a directory. The cost
is a few duplicated helpers (`port.h`, a growable buffer). The gain is
that `tl` is testable and fuzzable with nothing else linked, and that
ttr's own `core/` can change freely.

Namespace `tl`, macros `TL_*`. If a project needs both ttr and tl it
gets `ttr::` and `tl::`, no clash.

---

## 4. Parser

### 4.1 State machine

Paul Williams' DEC ANSI parser (vt100.net), implemented as one switch
per state with range tests (`tl/parser.cpp`); his transition table is the
specification and the unit tests check it. A generated table would be no
faster here, since the printable-run fast path handles the bulk of the
bytes. States: ground, escape, escape
intermediate, csi entry, csi param, csi intermediate, csi ignore, dcs
entry, dcs param, dcs intermediate, dcs passthrough, dcs ignore, osc
string, sos/pm/apc string. Actions: print, execute, clear, collect,
param, esc dispatch, csi dispatch, hook, put, unhook, osc start, osc put,
osc end, ignore.

Deviations from the 1990s original, all of which xterm and every modern
emulator also make:

- **Colon sub-parameters.** `3A` in csi param does not enter csi ignore;
  it starts a sub-parameter of the current parameter (`SGR 38:2::r:g:b`,
  `SGR 4:3`). Parameters are stored as up to 32 values of 16 bits each
  with a "has sub-parameters" bit map, so `38;2;r;g;b` and `38:2::r:g:b`
  both reach the SGR handler and are told apart.
- **Parameter range.** Values clamp at 65535 instead of 16383. Parameter
  count limit is 32; more are dropped, the sequence still dispatches.
- **APC is not ignored.** `ESC _` and `0x9F` collect into a string like
  OSC does, so the kitty graphics protocol (`ESC _ G ...`) can be
  handled. SOS and PM are still swallowed.
- **String length caps.** OSC, DCS and APC payloads are capped (default
  OSC 1 MiB because of OSC 52 and OSC 1337; APC/DCS 16 MiB because of
  images, both host-settable). A string over the cap is discarded but
  the parser stays in sync.
- **OSC terminators.** BEL, ST (`ESC \` or `0x9C`), and the 7-bit ESC
  that starts the next sequence all end an OSC, as in xterm.
- **DCS data is streamed.** `hook` tells the handler which DCS it is;
  `put` delivers chunks; `unhook` ends it. Sixel decodes incrementally
  so a 10 MB image does not need a 10 MB buffer first.
- **8-bit C1 controls** are honoured only when the input decoder is in
  an 8-bit single-byte mode. In UTF-8 mode bytes 0x80 to 0x9F are
  continuation or lead bytes and never controls, as in xterm's UTF-8
  mode.

### 4.2 Text decoding

Text decoding sits in front of the state machine:

- **UTF-8 mode** (default): a small decoder with Unicode-correct error
  handling (overlongs, surrogates and > U+10FFFF each become U+FFFD,
  one per maximal invalid subsequence). Partial sequences at the end of a
  `feed()` buffer are carried over. ESC and other C0 bytes inside a
  partial sequence abort it.
- **8-bit mode**: each byte is one character, mapped through the active
  G0..G3 charset designations (ISO 2022 `ESC ( B`, `ESC ) 0`, SI/SO, SS2,
  SS3, and the GL/GR invocations). Supported sets: ASCII, DEC Special
  Graphics (line drawing), DEC Supplemental, UK, Latin-1 as GR. Everything
  else is treated as ASCII.
- The DEC Special Graphics set also applies in UTF-8 mode to bytes
  0x5F..0x7E when G0 is designated `0`, because that is what ncurses
  programs assume on `xterm`.

The grid always holds Unicode code points. A host that can only draw
Latin-1 (IRIX core fonts) maps on output; that is its concern, and §9
says how ttrt does it.

### 4.3 Fast path

`feed()` first scans for the longest run of bytes in 0x20..0x7E (and
valid UTF-8 above 0x7F when in UTF-8 mode) while in ground state and
hands the run to `Screen::print_run()`, which writes cells in a loop
without going through the per-byte action dispatch. Everything else
goes byte by byte through the table. This is where ghostty gets most of
its throughput; without SIMD a plain loop is still several times faster
than the table per byte.

---

## 5. Screen model

### 5.1 Cell

```
struct Cell {            // 8 bytes, copyable with memcpy
    uint32_t ch;         // bits 0..20  code point, or grapheme index if G set
                         // bit  21     G: ch is an index into the grapheme pool
                         // bits 22..23 width kind: 0 narrow, 1 wide, 2 spacer tail,
                         //             3 spacer head (last column before a wrapped wide char)
                         // bit  24     protected (DECSCA)
                         // bit  25     image cell (section 5.8)
                         // bits 26..31 reserved
    uint16_t style;      // StyleTable id, 0 = default
    uint16_t link;       // hyperlink id, 0 = none; an image cell's tile row
};
```

A zero `Cell` is an empty default cell. Rows are cleared with `memset`.

A cell holds one code point inline. Combining sequences and emoji ZWJ
sequences store their first code point for cheap comparisons and an
index into a per-screen **grapheme pool** (refcounted arrays of up to 32
code points). This is ghostty's and kitty's approach; libvterm's fixed
six-codepoint cell makes every cell 28 bytes for a case that almost
never happens.

Wide characters occupy a `wide` cell followed by a `spacer tail`. The
tail is never drawn. A wide character that does not fit in the last
column leaves a `spacer head` there and wraps, as in ghostty. Erasing
either half erases both.

### 5.2 Style

```
struct Style {           // 16 bytes
    uint32_t fg, bg, ul; // color: 2-bit tag (default, palette, rgb) + 24-bit value
    uint16_t attrs;      // bold, faint, italic, blink, inverse, invisible, strike,
                         // overline, underline style (3 bits: none/single/double/
                         // curly/dotted/dashed)
    uint16_t reserved;
};
```

Styles live in a per-screen refcounted `StyleTable` indexed by the
16-bit `Cell::style`; the cursor caches the id of its current SGR state,
so printing does not touch the table. Interning is a hash lookup only
when SGR changes. If 65535 distinct styles are live at once, new ones
degrade to the closest attribute-only style and a counter records it;
this does not happen on real output. The alternate screen has its own
table, so a flashy full-screen program cannot pollute the primary one.

Colours are stored unresolved (palette index or rgb). The `View` offers
a resolver that applies the current palette, OSC 4/10/11 overrides,
inverse, bold-as-bright, and faint, so every host gets the same
behaviour and the palette can change without rewriting the grid.

### 5.3 Rows, grid and absolute row numbers

```
struct Row {
    Cell*    cells;      // exactly grid.cols cells
    uint16_t dirty_lo, dirty_hi;  // damaged column bounds, lo > hi means clean
    uint8_t  flags;      // WRAPPED (soft wrap continues on next row),
                         // HAS_GRAPHEMES, HAS_LINKS, HAS_PLACEHOLDERS
    uint8_t  line_attr;  // DECSWL/DECDWL/DECDHL top/bottom
    uint8_t  semantic;   // OSC 133: output / prompt / input / continuation
    uint8_t  reserved;
};
```

The `Grid` is one ring buffer of `Row` for scrollback plus active area,
as in alacritty: `zero` marks where the bottom active row lives, scrolling
the whole screen is `zero = (zero + 1) % capacity` and a clear of one
row. Region scrolls, IL and DL move `Row` headers (not cells) with
`memmove`. The buffer starts at `rows` and grows in chunks up to the
scrollback limit; the alternate screen has no scrollback.

Every row has an **absolute row number**: a 64-bit count of rows ever
scrolled off the top plus its index in the ring. `Grid::base` is the
absolute number of the oldest retained row, the active area starts at
`base + history_len`. Scrolling the full screen does not change any
row's absolute number; pruning raises `base`. Region scrolls, insert and
delete line and reflow do move rows, and they adjust the anchors below.

### 5.4 Anchors (tracked positions)

```
struct Anchor { uint64_t row; uint16_t col; uint8_t lost; };
```

Each screen's `Grid` keeps a registry of anchors that the host or the
engine has asked it to track: the viewport top when scrolled back, the two ends of
a selection, every image placement, search hits, the saved cursor. The
registry is a small array; every operation that moves rows (region
scroll, IL/DL, reflow, pruning, clear screen with history erase) walks it
and fixes the entries, in the same way ghostty updates tracked pins.
Anchors whose row is pruned are marked `lost` rather than removed.

This is what makes the model "more than a grid": a selection stays on
the text it was made on while output scrolls under it, a scrolled-back
viewport stays put (`tl` follows ghostty: the viewport is an anchor
unless it is pinned to the bottom), and an image stays on its line.

### 5.5 Cursor, modes, tab stops, charsets

Per screen: cursor position, pending-wrap flag, current `Style` and its
id, current hyperlink id, origin mode, saved cursor (DECSC: position,
style, charsets, origin, pending wrap), tab stops (bit per column), the
G0..G3 designations and GL/GR, protected-mode flag. Per terminal: the
mode bit set, title and icon name plus the XTWINOPS title stack, the
palette and OSC 4/10/11/12 overrides, cursor shape (DECSCUSR), the kitty
keyboard flag stack (one per screen, swapped with 1049), the mouse
tracking mode and encoding.

Modes supported from the start (DECSET/DECRST and ANSI SM/RM):

| Mode | Meaning |
|---|---|
| 1, 66 | DECCKM application cursor keys, DECNKM application keypad |
| 3, 40 | DECCOLM 80/132 columns (reported to the host as a resize request, never applied by `tl`) |
| 4 (ANSI), 20 (ANSI) | IRM insert mode, LNM linefeed/newline |
| 5, 6, 7, 8 | DECSCNM reverse video, DECOM origin, DECAWM autowrap, DECARM auto-repeat (stored, host decides) |
| 12, 25 | cursor blink, DECTCEM cursor visible |
| 45 | reverse wraparound (xterm) |
| 47, 1047, 1048, 1049 | alternate screen variants |
| 69 | DECLRMM left/right margins (DECSLRM) |
| 80, 1070, 8452 | sixel display mode, private palette, cursor after sixel |
| 9, 1000, 1002, 1003 | mouse: X10, click, drag, motion |
| 1005, 1006, 1015, 1016 | mouse encodings: UTF-8, SGR, urxvt, SGR pixels |
| 1004 | focus in/out reporting |
| 1007 | alternate scroll (wheel sends cursor keys on the alternate screen) |
| 1034, 1035, 1036, 1039 | meta key handling |
| 2004 | bracketed paste |
| 2026 | synchronized output |
| 2027 | grapheme cluster width |
| 2048 | in-band resize notifications |

Unknown modes are stored in a sparse set so DECRQM can report them as
"reset" rather than "not recognized" only where that matches xterm.

### 5.6 Hyperlinks (OSC 8)

A per-screen refcounted table of `{id string, uri}` indexed by
`Cell::link`. Links with the same explicit `id=` parameter share one
entry so a host can highlight all cells of one link; links without an id
get a fresh entry per OSC 8 open. The cursor's current link id is applied
to every printed cell until `OSC 8 ; ;` closes it. Entries are released
as rows are cleared or pruned.

### 5.7 Semantic prompts (OSC 133)

`A` (prompt start), `B` (command start), `C` (output start), `D` (command
end, with optional exit code) mark the current row's `semantic` field and
raise a `Host::semantic_prompt()` event with the parsed key/value
parameters (`aid=`, `cl=`, `k=`). From these the `View` can enumerate
**command blocks** (prompt rows, input rows, output rows, exit status) so
a host can offer "jump to previous prompt", "select last output" and
"re-run" without parsing the screen. `redraw=0`/`cl=` handling for
prompt redraw on resize follows the specification as implemented by
VTE and ghostty.

### 5.8 Images

Built: sixel (`tl/image.h`). An image is two things:

- **The store** (`ImageStore`): id (1-4095), pixels (0xAARRGGBB, decoded
  by tl itself), the cell size it was placed under, and a serial that is
  never reused, so a renderer's cache can tell a new image behind an old
  id. A byte budget (256 MB, 32 MB on IRIX) drops the oldest images
  first; their cells then show only their background.
- **Image cells.** Placing an image writes one cell per character cell
  it covers: bit 25 of `ch` set, bits 0-11 the image id, bits 12-20 the
  tile's column (512 at most), and `link` the tile's row. An image cell
  has no text (it reads as a space) and no hyperlink.

The first design kept placements apart from the cells, anchored to rows,
as kitty and foot do. Tiles in the cells won because everything the
grid already does then works for images without a line of image code:
scrolling and scroll regions, insert and delete of characters and lines,
scrollback, the screen switch, reflow, and text written over an image
replacing it cell by cell, which is how a VT340 and xterm behave with
sixels. The cost is that an image cannot lie under or over text. The
store frees an image when no cell of either screen shows it
(`collect()`, run when images pile up and when the scrollback is
cleared).

Sixel (`DCS P1;P2;P3 q ... ST`): 256 colour registers starting as the
VT340's, RGB and DEC HLS colours, raster attributes, repeats, at most
4096 x 4096 pixels. P2 = 1 leaves pixels not drawn transparent (they
show the cell's background), otherwise they are the default background.
The image goes at the cursor, down from its row and scrolling at the
bottom margin; the cursor then goes to the line below, in the image's
first column. Mode 80 (DECSDM) puts it at the top left without moving
anything, mode 8452 leaves the cursor right of the image on its last
row. DA1 reports sixel (4); XTSMGRAPHICS answers the number of colour
registers and the size; XTWINOPS 14, 16 and 18 report the text area and
cell sizes in pixels, which the host gives with `set_cell_pixels()`.

Next, the kitty graphics protocol (APC `G`) on the same store and cells:
chunked transfers, direct medium, placements by id, deletes. Its images
also become image cells, as in WezTerm, so text over a kitty image
replaces it where kitty would draw it on top; z-index, animation and
relative placements are left out. iTerm2's `OSC 1337 File=` would need
the host's decoder (ti).

The `View` gives cells; `tw::Source::image()` gives a renderer an image's
pixels by id, and the renderers draw runs of tiles of one image row
(`Renderer::draw_image`).

### 5.9 Damage

Three records, all reset by `clear_damage()`:

1. `full`: everything must be repainted (resize, alternate screen switch,
   palette change, reverse video, reflow).
2. Per row `dirty_lo..dirty_hi` column bounds, as alacritty does, so a
   blinking cursor or a clock in the corner does not repaint a line.
3. A short list of **scroll records** `{top, bottom, count}` in viewport
   rows, as in foot. A host that can copy pixels (XCopyArea) applies the
   scroll records first, then repaints the dirty rows. A host that cannot
   just repaints the union. Records are merged when consecutive scrolls
   hit the same region and dropped (in favour of `full`) past a small
   limit.

Damage is tracked in grid coordinates and translated to viewport
coordinates when the `View` is built, so scrolling back does not lose
information, and rows outside the viewport are skipped. Cursor movement
damages the old and new cursor cells. While mode 2026 is set,
`View` construction reports `held` and the host is expected to keep its
last frame; a host-side timeout (ghostty and foot use 150 ms to 1 s)
ends the hold by calling `Terminal::end_sync()`.

### 5.10 Resize and reflow

`resize(cols, rows)`:

- Rows only: the active area grows by pulling rows back from scrollback
  (if any) and shrinks by pushing rows into scrollback, keeping the
  cursor on its text. On the alternate screen rows are simply dropped
  or added at the bottom.
- Columns: rows are **reflowed**. Each logical line (a row plus the rows
  that follow it with `WRAPPED` set) is re-cut at the new width into a
  fresh ring. Trailing default cells are not carried over, so a shrunken
  window does not wrap blank padding. Anchors, the cursor and the saved
  cursor are remapped by logical line offset, as ghostty's `ReflowCursor`
  does. Wide characters that no longer fit at a row end get a spacer
  head. Rows marked with `semantic == prompt` and `cl=` cleared are
  truncated instead of reflowed, as the OSC 133 spec asks.
- Reflow is a terminal option; hosts for programs that redraw themselves
  (tmux inside) can turn it off and get xterm's truncating behaviour.

Resize raises mode 2048 reports and XTWINOPS replies through
`write_pty`, and ends any synchronized-output hold.

---

## 6. View: what the presenter gets

The `View` is a thin read-only accessor over the terminal, valid until
the next `feed()` or `resize()`. It does not copy anything, which is
fine for a single-threaded host that draws right after feeding.

```
class View {
public:
    int cols() const; int rows() const;
    bool full_damage() const;
    bool held() const;                      // mode 2026 active
    int  scroll_record_count() const;
    const ScrollRecord& scroll_record(int i) const;
    bool row_dirty(int y, int* lo, int* hi) const;
    const Row&  row(int y) const;           // viewport row
    const Cell& cell(int y, int x) const;
    int  grapheme(const Cell&, uint32_t* out, int max) const;  // code points of a cell
    const Style& style(const Cell&) const;
    void resolve_colors(const Cell&, Rgb* fg, Rgb* bg, Rgb* ul) const;
    const char* hyperlink(const Cell&) const;
    Cursor cursor() const;                  // viewport position, shape, visible, blink
    int  placement_count() const;
    Placement placement(int i) const;       // viewport-relative pixel rect + image id
    const Image& image(uint32_t id) const;
    uint64_t viewport_row() const;          // absolute row of viewport top
    uint64_t history_rows() const;
    int  block_count() const;               // OSC 133 command blocks
    Block block(int i) const;
};
```

Alongside the view: `Terminal::text(Anchor a, Anchor b, Buffer&)`
extracts text between two anchors with soft wraps joined and trailing
blanks trimmed, for selection and copy; `Terminal::scroll_viewport(n)`,
`scroll_to_bottom()`, `viewport_follows_output(bool)`; and
`Terminal::search()` later.

---

## 7. Input encoding

Hosts translate their native key events to `tl::Key {uint32_t keysym_or_codepoint;
uint32_t modifiers; uint8_t event; const char* text;}` using a small table
for non-text keys (arrows, function keys, keypad, editing keys, modifiers)
that mirrors the kitty protocol's private-use assignments. `Terminal::key()`
then encodes according to the active modes:

- Legacy xterm encoding by default: UTF-8 text, ESC prefix for Alt (or
  8th bit when mode 1034 is set), control-key table, `CSI`/`SS3` cursor
  and function keys with xterm's `;modifier` parameter, DECCKM and
  DECNKM, Backspace as DEL or BS (host option).
- The kitty keyboard protocol when its flags are set: `CSI u` with
  disambiguation, event types, alternate keys, all-keys and text
  reporting, the per-screen flag stack with push/pop/query.
- Mouse: X10, normal, button-event and any-event tracking in the
  classic, UTF-8, SGR, urxvt and SGR-pixel encodings; alternate scroll
  (mode 1007); focus in/out (mode 1004).
- Paste: bracketed (mode 2004) with CR/LF normalisation and stripping of
  C0 controls except Tab and CR; kitty's mode 5522 later.

Everything the terminal writes toward the application goes through one
`Host::write_pty()` so replies to queries and key presses stay in order.

---

## 8. The Host interface

An abstract class with a no-op default for every member, so a host
implements only what it supports. Events that need a reply pass a
response object the host fills before returning.

| Member | Triggered by | Host does |
|---|---|---|
| `write_pty(bytes, n)` | replies, key/mouse/paste encoding | write to the pty, buffering as needed |
| `bell()` | BEL | ring, flash, or mark urgent |
| `title_changed(kind, utf8)` | OSC 0/1/2, XTWINOPS 22/23 | set window or icon title |
| `cwd_changed(uri)` | OSC 7 | remember for new windows |
| `clipboard_set(target, bytes, n)` / `clipboard_get(target, Reply&)` | OSC 52 | copy, or paste back (policy: off by default) |
| `color_query(index, Reply&)` | OSC 4/10/11/12 `?` | report actual colours |
| `size_px(Reply&)` | XTWINOPS 14/16, mode 2048 | report cell and window pixel sizes |
| `request_resize(cols, rows)` / `request_window(op)` | XTWINOPS 8, DECCOLM, XTWINOPS 1-6, 9, 10 | honour or ignore |
| `notification(title, body)` | OSC 9, OSC 777 | post through ttr's notifier |
| `progress(state, percent)` | OSC 9;4 | show on the taskbar button |
| `semantic_prompt(kind, params)` | OSC 133 | optional, for shell integration UI |
| `decode_image(format, bytes, n, Image&)` | kitty/iTerm2 PNG | decode with libpng or refuse |
| `image_file_read(path, ...)` | kitty medium `f`/`t`/`s` | refuse unless configured |
| `sync_begin()` / `sync_end()` | mode 2026 | arm or cancel a timeout |
| `reset()` | RIS | reset host-side state |
| `unknown_sequence(kind, bytes, n)` | anything unhandled | log when debugging |
| `terminal_id(Reply&)` | DA1/DA2/DA3, XTVERSION | override the defaults (VT420-class, version string `ttrt(<ver>)`) |

Queries handled entirely inside `tl` with no host involvement: DSR 5/6,
DECRQM, DECRQSS (SGR, DECSCUSR, DECSTBM, DECSLRM), XTGETTCAP (from a
built-in table of the `ttrt`/`xterm-256color` capabilities that
applications actually ask for: `TN`, `RGB`, `Co`, key strings), DECXCPR,
DA1 (`CSI ? 64 ; 1 ; 2 ; 4 ; 6 ; 9 ; 15 ; 18 ; 21 ; 22 c` once sixel
exists), DA2 (`CSI > 41 ; <version> ; 0 c`), XTVERSION.

---

## 9. ttrt, the first host

A Motif application in `src/ttrt/`, using the same `core/` helpers, theme
and settings code as the rest of ttr:

- `XmMainWindow` with a menu bar (File, Edit, View, Settings), an
  `XmDrawingArea` for the terminal and an `XmScrollBar` for scrollback.
  Tabs or splits are not planned; the window manager handles windows.
- Drawing with Xlib on the DrawingArea: `XDrawImageString` with core
  fonts on IRIX, Xft optional on Linux behind `TTR_HAVE_XFT`, both
  behind a small `TermPainter` class. Scroll records become `XCopyArea`;
  dirty rows become runs of same-style cells drawn in one call each.
  Double buffering through a backing pixmap per row run keeps 8-bit
  visuals flicker-free.
- Colours: 16 palette colours plus the 6x6x6 cube and grey ramp allocated
  with `XAllocColor` and shared; on 8-bit PseudoColor visuals the cube is
  reduced and truecolor requests snap to the nearest allocated cell,
  which the `View` colour resolver makes easy because it returns rgb
  values and the painter owns the mapping.
- Code points above Latin-1 on a core-font system fall back to a
  per-character "best effort" map (ASCII box drawing for DEC graphics,
  `?` otherwise). On a Unicode-capable X server the same `View` draws
  with a `-iso10646-1` or Xft font.
- The pty: `tl/pty.cpp` wraps `openpty()`/`forkpty()` on Linux and
  `_getpty()` plus the `/dev/tty*` slave convention on IRIX, with
  `TIOCSWINSZ` on resize and `utmp` left alone. The child's environment
  gets `TERM=ttrt` if the terminfo entry is installed, else
  `TERM=xterm-256color`; `COLORTERM=truecolor` on 24-bit visuals only.
- Selection: on press, release and motion the host converts pixels to
  cells, creates two anchors, and the `View` reports selected ranges per
  row from them. Copy uses `Terminal::text()` and the X `PRIMARY` and
  `CLIPBOARD` selections (`XmClipboard*` for the latter, so Motif apps
  interoperate).
- Settings page in ttr's settings dialog: font, colours (reusing the
  colour theme pickers), scrollback size, bell behaviour, OSC 52 policy,
  reflow on or off.

ttrt's `README.md` section and the `ttrt` terminfo source are user
visible and get written in the same commits as the features.

---

## 10. Testing

- **Unit tests** (`tl/test/`), a tiny assert harness compiled by the same
  Makefile, run under g++, clang++ and MIPSpro: parser transitions
  against Williams' tables, UTF-8 edge cases, width tables against the
  UCD `EastAsianWidth.txt`, style interning, ring buffer rotation,
  reflow invariants (every `WRAPPED` row is full; spacer rules; anchors
  land on the same text).
- **Golden tests**: a byte file in, a text dump of the screen out (cells,
  attributes, cursor, damage summary), compared to a checked-in expected
  dump. Each new sequence family adds cases. Dumps are plain ASCII so
  diffs are readable.
- **Headless driver** (`tl/test/headless`): opens a pty, runs a command,
  feeds its output to a `Terminal`, answers queries, and dumps the screen
  on exit or on `SIGUSR1`. It runs **vttest** scripted (xterm's
  `vttest` menus driven by a key script) and **esctest** (iTerm2's suite
  talks to the terminal through the pty and checks replies). Both run on
  Linux in CI and on the IRIX emulator for the dialect job.
- **Fuzzing**: the headless driver with a stdin mode is a libFuzzer or
  AFL target on Linux. The parser and interpreter must never crash,
  hang or allocate without bound on any input; §4.1's string caps and
  the scrollback and image budgets exist for this.
- **Big-endian**: `tl` reinterprets no bytes as integers and uses no
  bit-fields across type boundaries (bit masks on `uint32_t` instead), so
  the big-endian job is a plain rerun of the unit tests on MIPS.
- **Memory**: Valgrind and ASan runs of the golden tests on Linux; a
  `build-asan/` configuration exists in the tree already.

---

## 11. Milestones

1. **Parser and tables.** `tl/parser`, `tl/unicode` with the generator,
   unit tests, golden harness. Builds with g++ `-std=c++98 -pedantic`,
   clang++ and MIPSpro from the first commit.
2. **VT102 screen.** Grid, cells, styles, cursor, tabs, charsets,
   ESC/CSI/SGR for VT102 plus 256-colour and truecolor SGR, DA, DSR,
   alternate screen. Headless driver passes vttest menus 1, 2, 3, 6 and
   the esctest VT100 groups.
3. **Scrollback, damage, anchors, reflow, View.** Golden tests for
   resize. This is the point where `tl` is usable by a host.
4. **ttrt.** Motif window, Xlib painter with core fonts, pty helper,
   selection and clipboard, scrollbar, settings page. **Runs on the
   Indy** with Latin-1 fonts and on Linux with Xft. README and terminfo.
5. **xterm and modern extensions.** Mouse modes, bracketed paste, focus,
   OSC 0/2/4/7/8/10-12/52/104/110-112, title stack, XTWINOPS, DECRQSS,
   XTGETTCAP, XTVERSION, kitty keyboard, modes 2026/2027/2048, OSC 133
   blocks with "jump to prompt" in ttrt, OSC 9/777 notifications wired to
   ttr's notifier.
6. **Images.** Sixel with modes 80/8452 (done); then the kitty graphics
   protocol (direct medium, placements, deletes, placeholders); then
   iTerm2 inline images through the host PNG decoder.
7. **Embedding elsewhere in ttr.** A terminal pane in the run dialog or
   a "command output" window, exercising the library with a second host.
   API review and freeze of `tl/tl.h`.

---

## 12. Open questions

- **Namespace and directory**: `tl/` at the top level with its own
  `port.h`, or `src/tl/` sharing `core/port.h`? This plan says top level
  and self-contained, for the sake of embedding outside ttr.
- **Default terminal id**: answer DA1 as VT420 (like xterm) or VT220?
  Applications key off DA2's `41` for xterm-like behaviour, so VT420.
- **Reflow default**: on (every modern terminal) or off (xterm)? Plan
  says on, with a setting.
- **OSC 52 default**: write allowed, read denied (kitty's default) seems
  right.
- **Grapheme pool cap**: 32 code points per cell. kitty allows 24,
  ghostty 64, libvterm 6.
- **Scrollback memory on the Indy**: 10 000 rows x 80 cols x 8 bytes is
  6.4 MB plus `Row` headers; acceptable on a 64 MB machine, but the
  default there may want to be 2 000. Trimming trailing blanks in
  scrollback rows (storing a per-row `len`) would halve typical usage
  and is left as a later optimisation.
- **Image storage on 8-bit visuals**: dithering at draw time is the
  host's job, but caching the dithered pixmap per placement belongs
  somewhere; probably in ttrt's painter keyed by (image id, cell size).

---

## 13. Sources

Surveyed 2026-10-19.

- Paul Williams, "A parser for DEC's ANSI-compatible video terminals":
  https://vt100.net/emu/dec_ansi_parser
- xterm control sequences: https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
- DEC VT101 user guide: `docs/ek-vt101-ug-002.pdf`
- ghostty `PageList.zig`, `page.zig`, `Screen.zig`:
  https://github.com/ghostty-org/ghostty/tree/main/src/terminal
- libghostty-vt C API (`terminal.h`, `render.h`):
  https://github.com/ghostty-org/ghostty/tree/main/include/ghostty/vt
- Mitchell Hashimoto, "libghostty is coming":
  https://mitchellh.com/writing/libghostty-is-coming and
  "Ghostty devlog 006" (SIMD parsing, width tables):
  https://mitchellh.com/writing/ghostty-devlog-006
- alacritty `alacritty_terminal/src/term/mod.rs` and `grid/storage.rs`:
  https://github.com/alacritty/alacritty/tree/master/alacritty_terminal/src
- kitty `kitty/line.h`, `kitty/history.h`:
  https://github.com/kovidgoyal/kitty/tree/master/kitty
- kitty graphics protocol: https://sw.kovidgoyal.net/kitty/graphics-protocol/
- kitty keyboard protocol: https://sw.kovidgoyal.net/kitty/keyboard-protocol/
- foot `terminal.h`: https://codeberg.org/dnkl/foot/src/branch/master/terminal.h
- libvterm (neovim fork) `include/vterm.h`:
  https://github.com/neovim/libvterm/blob/master/include/vterm.h
- Synchronized output, mode 2026:
  https://gist.github.com/christianparpart/d8a62cc1ab659194337d73e399004036
- Semantic prompts, OSC 133 (Per Bothner):
  https://gitlab.freedesktop.org/Per_Bothner/specifications/-/blob/master/proposals/semantic-prompts.md
- libtsm: https://www.freedesktop.org/wiki/Software/kmscon/libtsm/
