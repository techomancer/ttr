# tw: the text window drawing engine

Status: milestone 1 implemented 2026-10-20: `tw.h`, the Surface with
frame scheduling, CoreRender with core fonts and builtin box drawing and
block glyphs, the Palette for PseudoColor and TrueColor, and the TwGrid
widget. Verified on the emulated Indy's 8-bit Xsgi (native IRIX build and
the Linux build over the network) and on Linux. Milestone 2 implemented
the same day: the BDF reader, the glyph cache (combining marks composed
into their base cell, synthetic bold), the bundled misc-fixed fonts
(`third_party/misc-misc`, matched to the cell size), and depth-1 pixmap
pages with XCopyPlane in CoreRender. Not yet: a CJK fallback, and a
pre-indexed font format (parsing a 1 MB BDF takes about 0.25 s on an
emulated R4400 at startup).

`tw` draws a grid of character cells into an X window, fast on an Indy
and good-looking on a current Linux desktop. It knows nothing about
terminals: ttrt feeds it from `tl::View`, and other programs (a log
viewer, a console pane in ttr, an editor) feed it from their own models.
It ships as a C++ core plus a Motif widget, `TwGrid`, that wraps it.

The rules are ttr's (`docs/wm-design.md` section 2): C++98 that MIPSpro
7.4 accepts, no exceptions, RTTI or STL, plain `make`. Like `tl`, it does
not include anything from `src/core/`.

The evidence behind the choices is in
[ttrt/XWSH-NOTES.md](../ttrt/XWSH-NOTES.md): how xwsh draws, why dash's
text is blurry, and what the IRIX X server offers.

---

## 1. Goals and non-goals

Goals, in order:

1. **Crisp text.** Bitmap glyphs are drawn at their native size on whole
   pixels, never resampled. Anti-aliasing comes only from outline fonts
   rasterized at the cell size (FreeType, Linux). Box drawing and block
   elements are drawn by the engine so they join seamlessly at any size.
2. **Fast where it matters.** An Indy with XL or XZ graphics in its 8-bit
   default visual must scroll a full 80x24 screen of colour text without
   visible tearing or lag. Work is proportional to damage: blits for
   scrolls, repaints only of dirty cells, one X request per attribute
   run on the slow paths.
3. **Three renderers behind one interface**, chosen per machine:
   - **Core** for PseudoColor and any visual where server-side drawing
     is fastest: Indy, Indigo2 XZ/Extreme, 8-bit and 12-bit visuals.
   - **Soft**, a client-side TrueColor framebuffer pushed with MIT-SHM:
     Octane, O2, IP35, Linux without GL, remote X.
   - **GL**, OpenGL 1.1 fixed function with a glyph atlas: IMPACT with
     TRAM, VPro, Linux with a GPU.
4. **Unicode on IRIX.** The IRIX server has no ISO 10646 fonts, so the
   engine brings its own glyphs (bundled BDF fonts) and does not depend
   on server fonts for anything beyond Latin-1.
5. **A widget other projects can drop in**: Motif resources and
   callbacks, its own visual and colormap handling, no assumptions about
   the model behind it.

Non-goals: text layout (bidi, shaping beyond grapheme clusters,
proportional text), editing, scrolling logic (the model owns its
viewport), selection logic (the model marks selected cells).

---

## 2. Architecture

```
  model (tl::View via ttrt, or anything else)
      |  tw::Source: size, rows of cells, damage, cursor     (pull)
      v
  +-------------------------------------------------------------+
  | tw::Surface                                                 |
  |   frame scheduler (pacing, sync hold, blink)                |
  |   damage -> work list: blits, dirty spans, cursor, expose   |
  |   run builder: spans -> runs of equal attributes            |
  +---------------------------+---------------------------------+
                              | tw::Renderer (abstract)
        +---------------------+---------------------+
        v                     v                     v
  +-------------+     +---------------+     +---------------+
  | CoreRender  |     | SoftRender    |     | GLRender      |
  | Xlib on the |     | client frame- |     | GLX 1.x,      |
  | window: GC, |     | buffer, SHM   |     | glyph atlas   |
  | core fonts, |     | XImage,       |     | texture,      |
  | glyph pix-  |     | XCopyArea     |     | vertex arrays |
  | maps        |     |               |     |               |
  +------+------+     +-------+-------+     +-------+-------+
         |                    |                     |
         +-------- tw::Fonts (faces, coverage, glyph cache) --------+
         +-------- tw::Palette (RGB -> pixel per visual) -----------+

  TwGrid widget (Xt/Motif): resources, callbacks, visual and colormap
  choice, Realize/Resize/Expose/focus -> tw::Surface
```

`tw::Surface` is plain C++ over Xlib and can be used without Xt (a
window created by hand). `TwGrid` is the Motif face of it: an
`XmPrimitive` subclass, so it gets traversal, highlight and Motif colours
like any other widget.

---

## 3. The model interface

```
namespace tw {

struct CellStyle {               /* resolved, renderer-ready */
    uint32_t fg, bg, deco;       /* 0xRRGGBB */
    uint16_t flags;              /* BOLD, ITALIC, UNDERLINE(style), STRIKE,
                                    OVERLINE, BLINK, INVISIBLE, SELECTED,
                                    WIDE, WIDE_TAIL, DEFAULT_BG */
    uint16_t face;               /* font face index chosen by the engine */
};

struct CellRun {                 /* what a Source hands out per row */
    int x, n;                    /* first column, cells */
    const uint32_t* text;        /* n code points; clusters via cluster() */
    CellStyle style;
};

class Source {
public:
    virtual ~Source() {}
    virtual int cols() const = 0;
    virtual int rows() const = 0;
    /* Damage since the last frame: full, scroll records (rows top..bottom
     * moved by count), dirty column spans per row. */
    virtual bool full_damage() const = 0;
    virtual int scroll_count() const = 0;
    virtual void scroll(int i, int* top, int* bottom, int* count) const = 0;
    virtual bool row_dirty(int y, int* lo, int* hi) const = 0;
    /* Cells lo..hi of row y as runs of equal style; the Source may split
     * runs further, never merge across styles. */
    virtual int runs(int y, int lo, int hi, CellRun* out, int max) = 0;
    /* Code points of a multi-code-point cell (combining marks, ZWJ). */
    virtual int cluster(int y, int x, uint32_t* cps, int max) = 0;
    virtual int line_attr(int y) const = 0;       /* single, DWL, DHL top/bottom */
    virtual Cursor cursor() const = 0;            /* x, y, shape, visible, blink, color */
    virtual bool held() const = 0;                /* synchronized output */
    /* The frame was drawn; damage can be forgotten. */
    virtual void frame_done() = 0;
};

}
```

The interface is deliberately the shape of `tl::View` (scroll records,
dirty spans, cursor, hold) so ttrt's adapter is a few dozen lines, but
it carries **resolved colours** (RGB) rather than palette indexes: the
palette, bold-is-bright and reverse video are the model's business.

---

## 4. Fonts and glyphs

### 4.1 Faces and coverage

A `tw::Fonts` holds up to four faces (regular, bold, italic, bold
italic), each a **chain** of glyph sources tried in order:

1. the configured font (core X font, BDF file, or FreeType face);
2. the bundled Unicode fallback (BDF, see 4.3);
3. **builtin glyphs** drawn by the engine (4.2);
4. a replacement box with the code point's hex digits for anything left.

The cell size comes from the first source of the regular face. Every
other source must match it; a bundled BDF in the same pixel size is
chosen automatically, and FreeType faces are sized to it.

Missing faces are synthesized the way xterm and xwsh do it: bold by
drawing the glyph a second time one pixel to the right (only for bitmap
sources; FreeType has real bold), italic by using the regular face
(bitmaps are never sheared).

### 4.2 Builtin glyphs

Drawn from rectangles and lines at the exact cell size, identical in all
renderers, joining across cells without gaps:

- box drawing U+2500-257F (light, heavy, double, dashed, rounded corners
  as arcs approximated by short lines);
- block elements U+2580-259F and shades (shades as ordered dither, so
  they also work on 8-bit visuals);
- the DEC special graphics scan lines (U+23BA-23BD);
- legacy computing triangles and Powerline separators (U+E0B0-E0B3);
- Braille U+2800-28FF.

They are rasterized into the glyph cache like any other glyph, so the
renderers do not special-case them.

### 4.3 Glyph sources

| Source | Where | How glyphs reach the renderer |
|---|---|---|
| Core X font | IRIX and Linux, Latin-1 server fonts | Core renderer: `XDrawImageString` directly. Soft and GL: rasterized once into the cache by drawing into a 1-bit pixmap and `XGetImage` (exactly the font's pixels, no scaling). |
| BDF file | bundled in `data/fonts/` and user fonts | parsed by a small portable reader (BDF is text); 1-bit masks |
| FreeType | Linux only, optional (`TTR_HAVE_FREETYPE`) | 8-bit coverage masks, hinted, rasterized at the cell size; LCD filtering off by default |

Bundled fallback: the X.org "misc-fixed" ISO 10646 BDF fonts (public
domain; 6x13, 7x14, 8x13, 9x15, 9x18, 10x20 with bold variants) cover
Latin, Greek, Cyrillic, Hebrew, Thai, symbols, box drawing and many
CJK-adjacent blocks. A CJK fallback (unifont, GPL with font exception, or
a smaller 12/14 px subset) is optional and loaded only when a wide
character first needs it.

### 4.4 The glyph cache

Key: (face, code point or cluster id, variant). Variants: normal,
double-width line (pixel-doubled horizontally), double-height top and
bottom (pixel-doubled both ways, clipped). Doubling is exact pixel
replication of the mask, so DEC double-size text stays crisp.

Entry: mask (1-bit or 8-bit coverage), bearing and advance, and a
per-renderer handle. The cache is a set of **atlas pages** of fixed cell
slots (one cell or two for wide glyphs), filled on demand, evicted LRU by
page when a configurable budget is hit. Each renderer uploads pages its
own way (4.5).

Multi-code-point clusters (combining marks, ZWJ sequences) are cached
under a cluster id: the base glyph with marks composed onto it from the
same face, so a combined cell costs one blit like any other.

### 4.5 Sharp text: what current GPU renderers do

Researched 2026-10-20 because dash's text is soft. What ghostty, kitty,
alacritty, foot, Windows Terminal and the GPU font literature agree on,
and what tw takes from it:

1. **Rasterize at the final size; never resample glyphs.** Every
   terminal renders glyphs on the CPU (FreeType, CoreText, DirectWrite)
   at the exact pixel size into an atlas and only *places* them on the
   GPU (Windows Terminal's AtlasEngine: "DirectWrite and Direct2D are
   only used to rasterize glyphs, blending and placement happen in
   Direct3D"). dash's 4x supersample-and-box-filter is the opposite and
   is the whole reason for its blur.
2. **No distance fields for terminal text.** SDF and MSDF scale well
   but round corners and fail at small sizes; Aras Pranckevicius's survey
   and the SDF literature all recommend plain bitmaps for small, fixed
   sizes. A terminal draws one size at a time: bitmap atlas.
3. **Integral cell metrics and integral glyph positions.** Cells are
   whole pixels wide and high (ghostty fixed a one-pixel mismatch between
   its sprite glyphs and its GPU renderer by rounding cell metrics), and
   every glyph quad starts on a pixel boundary with one texel per pixel
   and nearest sampling. With a monospace grid there is no need for
   subpixel positioning, the main reason proportional-text renderers
   cache several horizontal offsets per glyph (stb_truetype's
   oversampling).
4. **Hinting at small sizes.** Hinted rasterization lines stems up with
   the pixel grid; that is what makes 8-14 px text crisp. FreeType is
   used with light hinting by default, full hinting optional, and bitmap
   strikes are used when a font has them (ghostty's load flags default to
   hinting and bitmaps on).
5. **Blend coverage with care for gamma.** Blending antialiased
   coverage in sRGB space makes edges too dark and text heavy and uneven
   between light-on-dark and dark-on-light; blending in linear light is
   correct but makes text look thin (FreeType's own comparison; foot,
   kitty and ghostty all moved to linear blending). The fixes in use:
   - a correction curve on the glyph alpha depending on text brightness
     (ghostty's `linear-corrected`, kitty's contrast and gamma settings,
     after experiments with gamma 1.7 and 1.42);
   - a "slight" gamma of about 1.4-1.5 (GTK discussion; Qt on X11 uses
     1.7) without stem darkening;
   - stem darkening at small sizes (Adobe, macOS).
   tw blends in linear light where the hardware can (sRGB framebuffers on
   Linux GL, a lookup table in the soft renderer, as foot does) and
   applies a precomputed alpha correction so weight stays the same for
   both polarities. Where it cannot (IRIX GL 1.1 has no sRGB textures or
   framebuffers), it uses the same precomputed correction baked into the
   atlas alpha, the "alpha correction to compensate for lack of gamma
   correction" approach, with one atlas variant per text-brightness
   class (light text, dark text). Bitmap fonts have only 0 and 1
   coverage, so none of this applies to them: they are crisp by
   construction, which is why IRIX's own fonts at their native sizes are
   the default there.
6. **Subpixel (LCD) antialiasing is optional and Linux-only.** It needs
   per-channel alpha, done in one pass with dual-source blending
   (`GL_ARB_blend_func_extended`, as alacritty does) or with three passes
   or shader blending otherwise, and it breaks on translucent
   backgrounds. Off by default; never on IRIX (CRT monitors, no
   extension).
7. **Draw procedural glyphs yourself.** Box drawing, blocks and
   Powerline shapes are drawn by the engine at the cell size (ghostty's
   sprite font, kitty's box drawing) so they join seamlessly and stay
   sharp regardless of the font (section 4.2).

What this means for fixing dash, if it lives on: rasterize from the
display font itself at 1:1 (`XGetImage` of the glyph drawn with the
font that sets the cell size, or FreeType at the cell size), drop the
supersampling and the gamma boost, keep quads on integer pixels, and
keep IMPACT's `GL_LUMINANCE_ALPHA` texture format.

Sources:
[kitty PR #5423 (linear blending, alpha contrast)](https://github.com/kovidgoyal/kitty/pull/5423),
[ghostty: blend in linear RGB](https://git.uoc.run.place/Applied-Software/ghostty/commit/e90f667b69a0a6b135d647eab5e96d208bb037af),
[ghostty: integral cell metrics](https://git.uoc.run.place/Applied-Software/ghostty/commit/945a715b08d9955b759e06f0c7ce6726d4e2604f),
[Gamma correction and blending fonts in linear space (foot)](https://whynothugo.nl/journal/2025/02/24/gamma-correction-and-blending-fonts-in-linear-space/),
[FreeType: text rendering and gamma](https://freetype.org/freetype2/docs/hinting/text-rendering-general.html),
[GTK issue 3555: gamma-corrected blending](https://gitlab.gnome.org/GNOME/gtk/-/issues/3555),
[Aras Pranckevicius: Font Rendering is Getting Interesting](https://aras-p.info/blog/2017/02/15/Font-Rendering-is-Getting-Interesting/),
[Red Blob Games: SDF fonts](https://www.redblobgames.com/articles/sdf-fonts),
[Windows Terminal AtlasEngine](https://cdn05042023.gitlink.org.cn/folkslinux/windows-terminal/commit/2353349fe5ad613f6efdb832daef19375acffb6c),
[alacritty: dual-source blending for subpixel text](https://git.causa-arcana.com/PolytreeDE/alacritty/commit/fc658b31d78e791319847eb9069709d6892f6c3c),
[stb_truetype oversampling](https://flutter.googlesource.com/third_party/stb/+/refs/heads/master/tests/oversample/),
[Alpha correction to compensate for lack of gamma correction (patent)](https://patents.justia.com/patent/20050237339),
[Text Rendering Hates You](https://danq.me/2019/10/09/text-rendering-hates-you).

---

## 5. Renderers

```
class Renderer {
public:
    virtual ~Renderer() {}
    virtual bool init(Display*, Window, Visual*, int depth, Colormap) = 0;
    virtual void resize(int width, int height) = 0;
    virtual void begin_frame() = 0;
    virtual void scroll(int top, int bottom, int count) = 0;     /* rows */
    virtual void fill(int x, int y, int n, uint32_t rgb) = 0;    /* cells */
    virtual void text(int x, int y, const CellRun&, const Glyph* const*) = 0;
    virtual void decorate(int x, int y, int n, const CellStyle&) = 0;
    virtual void cursor(const Cursor&, const CellRun& under) = 0;
    virtual void end_frame(const Rect* dirty, int n) = 0;        /* present */
    virtual void expose(const Rect&) = 0;                        /* needs repaint */
};
```

The Surface turns damage into calls: scroll records first, then each
dirty span split into runs (background `fill`, then `text`, then
`decorate` for underline, strike and overline as one rectangle or
polyline per run), then the cursor. Renderers may batch internally.

### 5.1 CoreRender: server-side Xlib

For PseudoColor visuals (CI8 on Indy, Indigo2; 12-bit CI) and any visual
where the server's drawing beats pushing pixels, which is every SGI
graphics board under Xsgi: Newport's REX3 draws spans, glyphs and
screen-to-screen copies in hardware.

- **Scroll**: one `XCopyArea` within the window per scroll record (xwsh
  repainted instead; the copy is a single REX3 operation).
- **Text in a server font** (runs whose glyphs all exist in the core
  font, the usual case for ASCII and Latin-1): one
  `XDrawImageString` per run: background and glyphs in one request,
  no separate fill. Bold uses the bold font or a second
  `XDrawString` one pixel to the right.
- **Other glyphs** (bundled BDF, builtins, clusters): the atlas pages
  live on the server as **depth-1 pixmaps**. A glyph is drawn with
  `XCopyPlane` from the atlas into the window using a GC with the run's
  foreground and background: one request per glyph that fills the whole
  cell, opaque, exactly like image text. Runs of builtin glyphs (a line
  of `─`) collapse into one `XFillRectangle` where the glyph is a pure
  rectangle.
- **GC cache**: one GC per (fg, bg) pair in use this frame, created from
  a small LRU, so a typical frame does no `XChangeGC` at all; xwsh's
  one-GC-with-cached-pixels is the fallback when the LRU is full.
- **Decorations** as one `XFillRectangle` per run (xwsh drew one line
  per character). Curly underline as one `XDrawLines` per run.
- **Cursor**: the cell redrawn with cursor colours (block), or a
  rectangle (bar, underline, hollow when unfocused). No XOR, so it works
  in any visual and colormap.
- No double buffering: every cell is painted once per frame with opaque
  operations, so there is nothing to flicker.

Colour (`tw::Palette`):

- TrueColor/DirectColor: shifts and masks.
- PseudoColor/GrayScale: the 16 base colours, the cursor and selection
  colours, and the default foreground and background are allocated
  exactly with `XAllocColor` (shared, read-only cells, so 4Dwm, ttr and
  other clients share them). Every other RGB maps to the nearest of a
  colour cube allocated on demand: 6x6x6 if 216 cells are free, else
  4x4x4, else nearest of whatever is allocated (the Motif/4Sight
  scheme cells are plentiful). 12-bit PseudoColor has room for the full
  xterm 256-colour palette exactly.
- Optional private colormap (`TwNprivateColormap`), like xwsh's own
  map, for exact colours at the cost of colour flashing on focus. The
  widget then sets `WM_COLORMAP_WINDOWS` on its shell so ICCCM window
  managers (mwm, 4Dwm, ttr) install it.
- StaticColor and 8-bit TrueColor: nearest match, no allocation.

### 5.2 SoftRender: client framebuffer, TrueColor

For 15/16/24/32-bit TrueColor visuals where the server is not the fast
path or where quality matters: Linux (with or without a GPU), O2, Octane
SI/SSI/MXI, Fuel, Tezro, IP35, remote displays.

- The window's contents live in a client-side framebuffer in the
  visual's pixel format (an `XImage`, in MIT-SHM shared memory when the
  extension and a local display allow, plain `XPutImage` otherwise).
- **Scroll**: `memmove` of framebuffer rows and one `XCopyArea` on the
  server, so the scrolled pixels never cross the wire.
- **Text**: glyph masks blended over the run's background in the
  framebuffer (1-bit masks are a select, 8-bit coverage a blend), with
  inner loops specialized per pixel size (16 and 32 bits).
- **Present**: `XShmPutImage` (or `XPutImage`) of the bounding boxes of
  the damaged spans, merged into at most a handful of rectangles per
  frame.
- Expose is a `XShmPutImage` of the exposed area: no model access.
- This is the renderer that gets anti-aliased FreeType text on Linux.

### 5.3 GLRender: OpenGL 1.1

For boards with texture hardware: IMPACT (TRAM), VPro (V6/V8/V10/V12),
and Linux with a GPU. It stays within OpenGL 1.1 plus `EXT_vertex_array`
so the same code runs on IRIX's OpenGL and on Mesa.

- Glyph atlas pages as `GL_ALPHA` textures, except on IMPACT, where
  `GL_ALPHA` does not render and pages are `GL_LUMINANCE_ALPHA` with
  luminance 255 (found in dash); sized to the board's limits (IMPACT
  TRAM: 1 MB or 4 MB; the page size adapts), `GL_NEAREST` filtering,
  `GL_MODULATE`, texturing enabled explicitly per frame.
- Projection with one texel per pixel and quad corners on pixel
  boundaries (`glOrtho(0, w, h, 0, -1, 1)`, vertices at integer
  coordinates, texel centers at +0.5): this is what keeps text crisp
  where dash's resampled atlas blurred.
- Backgrounds as untextured quads, glyphs as textured quads modulated by
  the run colour, decorations as quads, all from client vertex arrays
  built per frame for the damaged rows only.
- Two presentation modes:
  - **single-buffered with damage** (default on IRIX: IMPACT and VPro
    draw to the front buffer fast; scroll records become `glCopyPixels`,
    then only dirty spans are drawn);
  - **double-buffered full redraw** (default on Linux, where the
    compositor wants complete frames; the whole viewport is cheap there).
- Context and visual come from GLX directly (`glXChooseVisual`), not
  from `GLwMDrawingArea`, so the widget does not need libGLw.

### 5.4 Choosing a renderer

`TwNrenderer` is `auto`, `core`, `soft` or `gl`. The renderer decides
the window's visual (`choose_visual`), so TwGrid creates its window with
it:
- **core:** the parent's visual.
- **soft:** a TrueColor visual, the deepest one if the default is not
  TrueColor. An 8-bit Indy desktop gets a 24-bit window with its own
  colormap, put on the shell's `WM_COLORMAP_WINDOWS`.
- **gl:** `glXChooseVisual`, in this order: double-buffered with 8 bits
  per channel; single-buffered with 8 bits (the Indy's 24-bit visual has
  no back buffer); then any RGBA visual.

A renderer that cannot start falls back to soft, then to core.

What is implemented now:

| Situation | `auto` gives |
|---|---|
| An animated background is set (`TwNdynamicBackground`) | gl |
| Otherwise | core |

The design's per-board choice (gl on IMPACT, VPro and Linux with direct
rendering; soft on O2 and remote displays) waits for measurements on
real hardware. Until then, the core renderer is the fastest path on
Newport and on a modern X server alike.

Implementation notes:
- **SoftRender** (`soft_render.cpp`) keeps an `XImage` in the server's
  byte order, so it goes out unconverted: Xlib's own swapping of 32-bit
  images to a big-endian server got the channels wrong. It uses MIT-SHM
  when the server can attach the segment (local displays) and
  `XPutImage` otherwise. Damage goes out as at most eight merged
  rectangles per frame. Glyphs come from the glyph cache: the cache asks
  the server to rasterize the core font's glyph into a depth-1 pixmap
  once (`GlyphCache::set_display`), so text has exactly the core
  renderer's pixels. FreeType is still to come.
- **GLRender** (`gl_render.cpp`) draws each frame as three batches:
  backgrounds, glyph quads from a texture atlas (`GL_ALPHA`, or
  `GL_LUMINANCE_ALPHA` on IMPACT), then lines and builtin glyphs.
  - Vertices sit on whole pixels and texels map one to one, so the text
    is as sharp as the other renderers'. On the IRIX guest, GL and soft
    give pixel-identical text.
  - A double-buffered context draws whole frames and swaps.
  - A single-buffered context draws only damaged rows into the front
    buffer, with `glCopyPixels` for scrolls.
- **Backgrounds** (`backgrounds.cpp`) are dash's night, sunset, ocean,
  aurora, embers and static, ported to C++98 and GL 1.1. With one on,
  every frame is drawn whole and animated: every 33 ms on Linux and every
  100 ms on IRIX. Cells in the window's background colour
  (`S_DEFAULT_BG`) are left transparent over it.

The choice is logged; `ttrt -renderer core` forces one for testing.

---

## 6. Frame scheduling

The Surface decides **when** to draw; models only say "something
changed" (`TwGridDamage(w)` / `Surface::damage()`).

- First change after idle: schedule a frame after a short delay (8 ms
  default) so a burst of small writes (echo, prompt redraw) becomes one
  frame, the trick xwsh uses with its 200-byte threshold.
- Bulk input (the model reports large damage or the delay keeps being
  extended): draw at most every 16 ms (60 Hz) on fast renderers, 33 ms
  on the core renderer of a slow machine, so output speed is never
  limited by drawing.
- `held()` (synchronized output, mode 2026): no frame until released or
  until a timeout (150 ms), then one frame.
- Expose: drawn on the next frame (Soft: immediately from the
  framebuffer).
- Blink: one timer (cursor and blinking text, 500 ms) that redraws only
  the affected cells; stopped when unfocused or when nothing blinks.
- Unmapped or fully obscured windows (`VisibilityNotify`): no drawing,
  damage accumulates, one full frame on becoming visible.

Measured per frame and kept in a ring for `ttrt -stats`: requests sent,
cells drawn, time from damage to `XFlush`.

---

## 7. The TwGrid widget

An `XmPrimitive` subclass (`twGridWidgetClass`, `TwCreateGrid`), in
C++98 with Xt's C class records.

Resources:

| Resource | Type | Meaning |
|---|---|---|
| `TwNsource` | `tw::Source*` | the model |
| `TwNrenderer` | String | `auto`, `core`, `soft`, `gl` |
| `TwNfont`, `TwNboldFont`, `TwNitalicFont`, `TwNboldItalicFont` | String | XLFD, `bdf:path`, or `ft:family:size` |
| `TwNfallbackFonts` | String | extra BDF fallbacks |
| `TwNcolumns`, `TwNrows` | int | preferred size in cells (geometry requests) |
| `TwNcellWidth`, `TwNcellHeight` | int | read-only |
| `TwNmarginWidth`, `TwNmarginHeight` | Dimension | inner padding, filled with the default background |
| `TwNprivateColormap` | Boolean | see 5.1 |
| `TwNblinkInterval`, `TwNframeDelay` | int | ms |
| `TwNresizeCallback` | callback | new size in cells |
| `TwNinputCallback` | callback | key event: keysym, modifiers, UTF-8 text |
| `TwNpointerCallback` | callback | press, release, motion, wheel in cell coordinates (and pixels) |
| `TwNfocusCallback` | callback | focus in/out |

Behaviour:

- **Realize** chooses the visual and colormap for the renderer (GL
  needs a GLX visual; Soft prefers the deepest TrueColor; Core uses the
  parent's), creates the window with them, and sets
  `WM_COLORMAP_WINDOWS` on the shell when they differ from the shell's.
- **Resize** computes cells from pixels, resizes the renderer, calls the
  resize callback; the widget does not resize the model itself.
- **Geometry**: preferred size is columns x rows cells plus margins;
  `XmNresizeIncrements`-style size hints are set on the shell so window
  managers resize in whole cells.
- **Input**: key events become keysym, modifier mask and text (with an
  input method through `XmImGetXIC` where present) and go to the input
  callback; the widget does not encode them for any protocol.
- Focus changes switch the cursor between solid and hollow.

---

## 8. Layout

```
tw/
  DESIGN.md
  tw.h             public C++ API: Source, Surface, CellStyle, CellRun, Cursor
  surface.h/.cpp   scheduling, damage to work, run building
  renderer.h       Renderer interface
  core_render.cpp  CoreRender
  soft_render.cpp  SoftRender (MIT-SHM optional at run time)
  gl_render.cpp    GLRender (built only with TTR_HAVE_GL)
  palette.h/.cpp   RGB to pixel, colormap allocation
  fonts.h/.cpp     faces, chains, coverage
  glyphs.h/.cpp    glyph cache, atlas pages, variants
  builtin.cpp      box drawing, blocks, braille, powerline
  bdf.h/.cpp       BDF reader
  ft.cpp           FreeType source (built only with TTR_HAVE_FREETYPE)
  grid_widget.h/.cpp  TwGrid Motif widget
  test/            unit tests and golden-image tests
data/fonts/        bundled BDF fonts and their licences
```

Optional parts are separate source files chosen by the Makefile's object
lists, not `#ifdef`s inside shared code: `gl_render.o` and `ft.o` are
linked only when their feature is on, and `surface.cpp` asks a small
registry which renderers exist.

---

## 9. Testing

- Unit tests (no display): palette allocation against a fake colormap,
  BDF parsing, builtin glyph rasterization (exact masks for known
  sizes), run building, damage to work list.
- **Golden images**: a scripted `tw::Source` with known screens
  (attributes, wide characters, box drawing, double-size lines, cursor
  shapes) is drawn by every renderer into a window, read back with
  `XGetImage`, and compared with checked-in PNGs, exactly per renderer
  (renderers differ only where anti-aliasing is on). On Linux under Xvfb
  (core and soft) and Mesa (gl); on IRIX in the emulator, screenshots
  taken with `tools/irix-shot.sh`.
- **Damage model**: the same shadow-host check tl uses, at pixel level:
  draw a random session incrementally and compare with a full repaint.
- **Performance**: a replay of recorded sessions (`cat` of a large
  file, `vim` scrolling, `top`) measuring frames, requests and time per
  frame on the emulated Indy and on Linux.

---

## 10. Milestones

1. `tw.h`, Surface with scheduling, CoreRender with core fonts only,
   Palette for PseudoColor and TrueColor, the TwGrid widget. Enough for
   ttrt's first window on the Indy and on Linux.
2. Glyph cache, BDF reader, bundled misc-fixed fonts, builtin glyphs,
   depth-1 atlas pixmaps in CoreRender: Unicode on IRIX.
3. SoftRender with MIT-SHM; FreeType on Linux.
4. GLRender; auto selection per board; golden images for all three.
5. Double-size lines, blink, images (the glyph cache's pages double as
   storage for sixel and kitty image tiles).
