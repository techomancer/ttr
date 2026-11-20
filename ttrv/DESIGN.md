# ttrv: the ttr image viewer

ttrv is a Motif image browser and viewer for IRIX and Linux. It takes the
layout of ACDSee 3.x / Classic on Windows 95 and its keyboard flow,
which never made you reach for the mouse:

- The browser: a folder tree, a file list (details or thumbnails), a
  preview of the file under the cursor and its information, in panes.
- The viewer: Enter shows the file; Space and Backspace step through
  the folder at once, because the next image is already decoded; Enter
  goes back to the browser, with the cursor on the file you ended on.
- Zip files (and the other archives ttrc enters) are folders.

What it must not be (geeqie): slow to start, a UI that stalls while
images decode, keys that depend on which pane has focus in ways you can't
see, or a dozen settings dialogs.

| Module | Role in ttrv |
|---|---|
| [`ti/`](../ti/DESIGN.md) | decoding: PNG, JPEG, GIF, BMP, SGI, TIFF, PNM; plus XPM and EXIF (new) and libraw (optional, Linux) |
| [`tf/`](../ttrc/DESIGN.md#5-tf-the-virtual-file-system) | every file access, so archives are folders, the same as ttrc's |
| `tx/` | the dock panes (moved from ttrcode), dialogs, Settings pages, the app icon |
| `tw/` | the visual choice and the GL context code ttrt already uses on IRIX |
| Motif | menus, toolbar, the location bar, the tree, dialogs, the status bar |

The language and portability rules are the rest of ttr's: C++98 for
MIPSpro 7.4 and `g++ -std=c++98 -pedantic`, no STL, no exceptions,
Motif 2.1 as the floor. One difference from ttr and ttrcode: **ttrv has
worker threads** (section 4), because decoding a 12-megapixel JPEG takes
seconds on an R4400, and the browser must keep answering while it does.
Xt and Motif are still touched only by the main thread.

---

## 1. The window

```
+--------------------------------------------------------------------+
| File  Edit  View  Go  Tools  Help                                  |
| [<-][->][Up][Refresh] | [Details][Thumbs] | [View][Full] | [Zoom]  |
| Location: [ /home/me/pics/2001.zip/uzip://trip                  v] |
+-------------------+------------------------------------------------+
| Folders           | Files                                          |
|  - /              |  Name           Size  Image        Modified    |
|    - home         |  ..                                            |
|      - me         |  beach.jpg     1.2MB  2048x1536 24  2001-07-04 |
|        + pics     |  boat.png       88KB   640x480 32  2001-07-05 |
|          2001.zip |  ...                                           |
|                   |     (or the thumbnail grid)                    |
+-------------------+------------------------+-----------------------+
| Preview                                    | Information           |
|   the file under the cursor, fitted        | JPEG 2048 x 1536      |
|                                            | Camera, exposure, ... |
+--------------------------------------------+-----------------------+
| 23 files (14.2 MB) | 1.2 MB, 2001-07-04 12:31 | beach.jpg | 2048x1536 |
+--------------------------------------------------------------------+
```

- **Location bar** on top: the current folder as text, editable; Enter
  goes there. Its drop-down holds the recent folders.
- **Panes:** Files is the centre. Folders, Preview and Information are
  dock panes (section 6): Folders on the left, Preview and Information
  side by side at the bottom, which is ACDSee 3's layout. They can be
  moved to another side, stacked as tabs, resized or closed, and the
  layout is saved.
- **Status bar:** ACDSee's fields: the folder's totals, the current
  file's size and time, its name, its dimensions and depth.
- **The viewer** replaces the browser in the same window (Enter), or
  takes the whole screen (F, or Alt+Enter from the browser). Its own
  status bar and toolbar can be hidden.

## 2. The browser

### 2.1 Folders

An XmList of indented names, as ttrcode's workspace tree, for the
reasons given there (fast with thousands of entries, the same keys in
every Motif). Folders are read when opened. Archives appear as folders
with their own icon. Mount points and `~` are the roots offered at the
top, as ACDSee's drive list.

### 2.2 Files

One list, two looks, chosen in View or with Ctrl+1 / Ctrl+2:

- **Details:** name, size, image properties (`2048x1536x24`), type,
  modified. Click a header to sort; again reverses. Images first is an
  option (default off; folders are always first).
- **Thumbnails:** a grid of cells, each with the thumbnail, its
  `WxHxdepth` under it and the name below, as ACDSee drew them. Cell
  sizes 64, 96, 128 (default) and 192 pixels.

Both are drawn by our own widget (an XmPrimitive subclass over one
window), not XmContainer: the grid needs to draw only the visible cells
and the details view needs icons in rows. The image properties column
fills in as the headers are read (section 4.3), without moving the
cursor.

### 2.3 Keys

The browser keys are ACDSee's where it had one:

| Key | Does |
|---|---|
| Arrows, PgUp/PgDn, Home/End | move the cursor (in the grid: by cell, row, page) |
| Typing | jump to the next name starting with what was typed |
| Enter | a folder or archive: open it; an image: show it in the viewer |
| Backspace, Alt+Up | the parent folder, cursor on the one we came from |
| Alt+Left / Alt+Right | back / forward in the folder history |
| Space, Ctrl+Space | mark / unmark the file (for copy, move, delete) |
| Tab, Shift+Tab | the next / previous pane |
| Ctrl+1 / Ctrl+2 | details / thumbnails |
| F5 | read the folder again |
| F2 | rename; Del delete (to the trash on Linux, asked first); Ctrl+C / Ctrl+X / Ctrl+V copy or move files |
| Alt+Enter | the viewer, full screen (F is typing to find a name here) |
| Ctrl+L | the location bar |

Any key the files pane does not use, when another pane has focus, still
reaches it if the other pane has no use for it either (the arrows in the
preview move the file cursor), so "which pane has focus" matters as
little as possible.

## 3. The viewer

### 3.1 Keys

| Key | Does |
|---|---|
| Space, PgDn, Right (fitted) | next image |
| Backspace, PgUp, Left (fitted) | previous image |
| Home / End | first / last image of the folder |
| Enter, Esc | back to the browser, cursor on this file |
| F, Alt+Enter | full screen on / off |
| `+` / `-`, wheel | zoom in / out, around the pointer |
| `*` or `/` | actual size (1:1) |
| `.` or Z | fit to window (the default; shrinks, never enlarges unless set) |
| Arrows (zoomed in) | pan; drag with the button pans too |
| `[` / `]` | rotate left / right (shown only; saving is a separate action) |
| Ctrl+Up / Ctrl+Down | the next / previous folder (ACDSee's), first image there |
| I | the information overlay on / off |
| Del | delete this file, show the next |

The order is the browser's sort. Archive members step like files.

### 3.2 What the viewer shows at once

The viewer must never show an empty window while it decodes. In order:
the full image if the cache has it; else the preview pane's scaled copy
or the thumbnail, scaled up, with a small "decoding" mark; then the full
image when it arrives. JPEGs are first decoded at a reduced scale
(libjpeg's DCT scaling, `reduce_to`) to fit the screen, and in full only
when zoomed past it.

## 4. Decoding: threads and the cache

### 4.1 Threads

- **Main thread:** Xt, Motif, X. It never reads a file or decodes.
- **One reader thread** owns `tf`. `tf` keeps global state (the archive
  cache, spool limits, progress callbacks) and is not thread-safe; one
  thread for it keeps it unchanged, and one reader at a time is also
  what a single disk (and an NFS mount) wants.
- **Decoder threads:** as many as CPUs, minus one, at least one (on IRIX
  `sysmp(MP_NAPROCS)`, on Linux `sysconf(_SC_NPROCESSORS_ONLN)`). They
  take bytes the reader produced and decode them with `ti::decode`, and
  scale with ti. libpng, libjpeg, giflib and stb_image are safe to use
  from several threads with one decoder instance each; `ti` will be
  checked for statics (stb_image's `stbi__vertically_flip_on_load` and
  failure reason are globals: set per thread with
  `STBI_THREAD_LOCAL`, or the builtin decoders get one lock).
- Results come back to the main thread through a pipe that Xt watches
  (`XtAppAddInput`), the same way ttrt's pty output arrives. The main
  thread turns them into XImages or textures.
- pthreads on both systems. IRIX 6.5 has them (`-lpthread`); td already
  uses them there.

### 4.2 Jobs and priorities

Each job is (file, what): header, thumbnail, preview at a size, full
image. One priority queue, ordered:

1. The file being shown (viewer) or under the cursor (preview pane).
2. The next two images in the current direction, then one behind.
3. Thumbnails of the visible grid cells, top to bottom.
4. Headers (dimensions, depth, EXIF) of the visible list rows.
5. Thumbnails and headers of the rest of the folder.

Moving the cursor re-sorts the queue; jobs that fell out of the window
of interest are dropped before they start. A running decode is not
interrupted (ti has no way to stop one), but its result is only cached.

### 4.3 The cache

- **Full images:** the most recently used, up to a memory budget
  (default: a quarter of physical memory, at most 512 MB; on the Indy
  that is 64 MB, about five screens). Read-ahead counts against it.
- **Previews and thumbnails:** a separate, smaller budget, so stepping
  through big images never pushes the visible thumbnails out.
- **Thumbnails on disk** (decided 2026-11-19): the freedesktop
  thumbnail spec on Linux (`~/.cache/thumbnails/normal` and `large`, PNG
  with `Thumb::URI` and `Thumb::MTime`), so ttrv shares them with other
  programs. The same
  layout under `~/.ttrv/thumbnails` on IRIX. Archive members are not
  written to disk (their URIs are not stable).
- Keys are the path plus size and mtime, so an edited file is decoded
  again.

## 5. Display

### 5.1 The visual

ttrv asks for the best visual for images, not the default one:

- The deepest TrueColor (or DirectColor) visual: 24-bit on Linux and
  on Octane VPro; the Indy's 24-bit visual where the board has it.
- On an 8-bit PseudoColor-only screen (Indy XL8, Indigo2 XL), a fixed
  colour cube in its own colormap, and the images ordered-dithered to it
  in the decoder thread.
- The window of the image (viewer, preview, thumbnails) gets that
  visual and its colormap, put on `WM_COLORMAP_WINDOWS`. The rest of the
  window (Motif) stays on the default visual.

`tw` makes the same choice for ttrt's soft renderer; the code moves to a
shared place (section 6).

### 5.2 Renderers

| Renderer | How | Where |
|---|---|---|
| soft | the scaled image in an `XImage`, sent with `XShmPutImage` when the server can attach the segment, else `XPutImage` | everywhere; the floor |
| gl | the image as textures, scaled by the GPU | Linux with direct rendering and a GPU; VPro (and O2) on IRIX |

- **soft:** the decoder thread produces the image already scaled to the
  window (area average when shrinking, as `ti::scale`; bilinear when
  growing), in the server's byte order and pixel layout. The main thread
  only puts it. Panning a zoomed image puts the newly visible strip.
- **gl** (`src/ttrv/glpaint.cpp`, as built): the image goes up once as
  power-of-two tiles (1.1 has no others) of at most
  `GL_MAX_TEXTURE_SIZE` and 2048. Neighbouring tiles overlap by two
  pixels, so filtering across a seam reads the same pixels on both
  sides. Each side is cut into whole tiles, then one at most a quarter
  padding, so a side of 1024 is a 1024 and an 8, not a 2048. Mipmaps
  come from `GL_GENERATE_MIPMAP` (1.4) or from a 2 x 2 box filter on the
  CPU (VPro; no GLU needed). Interior tiles of an opaque image go to GL
  straight from the decoded image (`GL_UNPACK_ROW_LENGTH`). The other
  tiles are copied, with transparent pixels put over the background, so
  the textures are RGB and need no blending.

  Shrinking is `GL_LINEAR_MIPMAP_LINEAR`. Enlarging is bilinear up to
  2x and the nearest pixel from 2x, as the soft renderer does. Where
  there is GLSL (Linux), a Catmull-Rom shader does the enlarging below
  2x. The trilinear filter is close to soft's area average on
  photographs (42 dB against an exact box filter, soft 41 dB), and
  softer on pixel-aligned checkers. No shader for shrinking was needed.

  Two named images stay up, the fitted decode and the full one, so
  going between fit and a zoom uploads neither again. Zoomed out to
  where the fitted decode has every pixel shown, the viewer draws from
  it.

  Textures fit a budget (`textureMegabytes`): a larger image goes up
  reduced, and a zoom past what the reduced one keeps is a soft frame
  drawn 1:1. Text is `glXUseXFont` lists.
- **Measured** (`ttrv_test_gl`, which reads the frames back and compares
  them with `render_view`'s: 1:1 and 2x or more are exact on 24-bit
  visuals). On the Octane (V10, 32 MB, R14000 at 600 MHz, 30-bit visual):
  - A 12-megapixel JPEG takes 1.2 s to upload (1.5 s before the
    zero-copy tiles).
  - At 1:1 and every zoom above, a 1024 x 768 frame takes 16 ms (60 Hz),
    where the soft renderer takes 160 to 420 ms.
  - When the whole image is in sight it takes 290 ms (soft: 880 ms). The
    board is paging textures then, because 67 MB of tiles do not fit in
    its 32 MB, so the budget there is four times the board's memory.
  - A 2400 x 1800 image fits on the board and draws in 16 ms at every
    zoom.
- `-renderer soft|gl` forces one, as in ttrt; `auto` picks gl when the
  context is direct and the renderer is not a software one (llvmpipe,
  softpipe, swrast) or a board that textures in software (Newport, XZ,
  Elan) or from a few megabytes of TRAM (IMPACT).

## 6. What is reused, and what moves

**Dock panes.** ttrcode's `Docks` (`tx/dock.h`, moved from ttrcode, 750 lines)
depends only on `core/` and Motif: areas on three sides, groups of
tabs, sashes, the tab menu to move a pane, and the layout as a line of
text. It is not ttrcode-specific. It moved to `tx/dock.{h,cpp}`
(namespace `tx`) unchanged, ttrcode uses it from there, and ttrv uses
it as is: Files in the centre, Folders on the left, Preview and
Information as two groups in the bottom area.

`CellList` (ttrcode's debugger lists) draws on TwGrid's character grid,
so it cannot show thumbnails or icons; ttrv's file view is its own
widget.

**From tw:** the visual choice, the colormap handling for a 24-bit
window on an 8-bit desktop, the MIT-SHM `XImage` setup with its
byte-order fix, and the GLX context creation (visual order, direct
check). They are pulled out of the text renderers into
`tw/visual.cpp` and a new `tw/ximage.cpp` that both ttrt and ttrv use.

**From ti:** all of decoding and scaling. New in ti:
- XPM: our own reader (the format is text; colours by name through a
  table of the X colour names, so no server round trip in a worker),
  so it works on bytes from an archive as well.
- EXIF: a reader for JPEG's APP1 and TIFF's IFDs: camera, lens,
  exposure, ISO, focal length, date, orientation. Orientation is
  applied when showing.
- Header-only reads (dimensions and depth without decoding) for the
  list's image properties: each format's header is a few bytes.
- libraw (`TTR_HAVE_RAW`, Linux only, optional): the embedded JPEG
  preview for thumbnails and the browser, the full raw decode in the
  viewer.
- GIF: the first frame, as now; animation later.

**From tf:** everything as ttrc uses it. A file's archive members are a
folder; a zip in a zip works.

**From tx:** `TxScale` for UI scale, the Settings pages, the app icon,
the about box.

## 7. Settings

`~/.config/ttrv/settings` (`$XDG_CONFIG_HOME`), the same format as the
other programs; ttrv owns its settings and its Settings dialog
(Tools > Settings), as each ttr program does. In it: the layout (dock
line), view mode and thumbnail size, sort, the cache budgets, the
renderer, enlarge small images in fit (off), the background colour of
the viewer, and the start folder (last one, or a fixed one).

## 8. Milestones

1. **Groundwork.** Move `Docks` to `tx/`, the visual and XImage code to
   shared tw files, ttrcode and ttrt unchanged in behaviour. ti: header
   reads, XPM. A `ti` thread-safety audit and a test that decodes the
   test images from several threads at once.
2. **The browser.** Window, menus, toolbar, location bar, folder tree,
   details list with keys, preview pane (soft renderer), the reader and
   decoder threads with the cache, archives as folders. Linux and IRIX.
3. **The viewer.** Window and full screen, the keys of section 3, zoom
   and pan, read-ahead, the reduced first decode.
4. **Thumbnails.** The grid, the disk cache, priorities by what is
   visible.
5. **Information.** EXIF in ti, the Information pane and overlay,
   orientation.
6. **GL.** The texture renderer on Linux and VPro, the shader filters on
   Linux. Done 2026-11-19 (section 5.2).
7. **libraw** on Linux; file operations (rename, delete, copy, move);
   the IRIX tardist and desktop icon.

Each milestone is tested on Linux, on the Indy guest (8-bit and 24-bit
visuals, one CPU), and from milestone 3 on the Octane (VPro, two CPUs)
for the threads and GL.

## 9. Open questions

- File operations (decided 2026-11-19): ttrv's own and basic: rename,
  delete, copy and move, new folder, through `tf/ops`. Archives are read
  only; nothing is copied into one.
- Animated GIF playback: in the viewer only, later.
- Editing (rotate and save, crop): out of scope for now; lossless JPEG
  rotation would need jpegtran's code.
