# ti: ttr's images

ti decodes image files into 8-bit RGBA, scales them, and encodes them
for terminals. ttrc's viewer (F3) shows images with it, ttr's icon
loader reads PNG icons through it, and ttrv (ttrv/DESIGN.md) decodes
with it in several threads. C++98, no STL, no exceptions; it
needs only the C library and the image libraries the build names.

## 1. Two decoders a format

The image libraries on IRIX 6.5 are old: libpng 1.2.5, libjpeg 6a,
libtiff 3.4, and no giflib. Linux has libpng 1.6, libjpeg-turbo 3,
giflib 5 and libtiff 4. Each format therefore has two decoders:

| Format | System library (TTR_HAVE_*) | Built-in |
|---|---|---|
| PNG | libpng 1.2 to 1.6 (`TTR_HAVE_PNG`) | stb_image |
| JPEG | libjpeg 6a to libjpeg-turbo 3 (`TTR_HAVE_JPEG`) | stb_image |
| GIF | giflib 4 or 5 (`TTR_HAVE_GIF`) | stb_image |
| TIFF | libtiff 3.4 to 4 (`TTR_HAVE_TIFF`) | none |
| BMP, PNM (P5, P6) | none | stb_image |
| SGI (.rgb, .bw) | none | ti's own |
| XPM 3 | none (libXpm needs a display for names) | ti's own |

The library is used when the build has it, as it is the better
maintained decoder on Linux; the built-in one is used where it is
missing. `DecodeOptions::backend` picks one or the other, and the tests
decode every fixture with both and compare them.

Each system decoder is written once, against the part of the API that
every version from the oldest to the newest has, not once per version:

- libpng: the classic API (`png_create_read_struct`, `png_set_read_fn`,
  `png_set_expand`, `png_set_filler`). The simplified `png_image` API
  is 1.6 only, and `png_set_add_alpha` is 1.2.7.
- libjpeg: our own source manager (6a has no `jpeg_mem_src`) and
  error manager. CMYK, YCCK and grey files are converted by ti, since
  libjpeg 6a turns only YCbCr into RGB. DCT scaling (1/2, 1/4,
  1/8) makes a big photo cheap when the viewer asks for at most 2048
  pixels.
- giflib: the record API (`DGifGetRecordType`, `DGifGetLine`), with
  interlacing done by ti. `DGifSlurp`'s handling of interlace changed
  between versions; only `DGifOpen` and `DGifCloseFile` differ, by an
  error argument (5.0, 5.1), and `GIFLIB_MAJOR`/`MINOR` select it.
- libtiff: `TIFFClientOpen` and `TIFFReadRGBAImage`. The procedures use
  libtiff's own `tsize_t` and `toff_t`, which 4.x widened; the raster is
  bottom-up (the oriented call is newer) and premultiplied, and ti
  undoes both. Files are opened without strip chopping (mode `c`):
  libtiff 3.4 divides by zero chopping a single uncompressed strip whose
  rows are over 8 KB. ti also refuses a first directory whose scanline
  size is 0 or overflows, which 3.4 divides by too.

## 2. The built-in decoders

stb_image (third_party/stb, pinned) is compiled through
`ti/third/stb_image.c`, limited to PNG, JPEG, GIF, BMP and PNM, from
memory only, without SIMD or thread-locals. Its `<stdint.h>` needs
MIPSpro's C99 mode, hence `STB_CFLAGS=-c99` on IRIX. Its IDCT overflows
signed integers on damaged files, so gcc builds it with `-fwrapv`.

SGI images are IRIX's own format and no library reads them, so ti does:
verbatim or RLE, 1 or 2 bytes a sample, 1 to 4 channels.

Every decoder refuses images over `k_max_pixels` (32 Mpixel) before
allocating, and reports errors as messages. The tests cut every fixture
at every length and change random bytes; the sanitizer build
(`-fsanitize=address,undefined`) runs them.

## 3. Terminals

`ti/term.h` turns an image into what a terminal shows:

- **Half blocks**: U+2580 with the top pixel as foreground and the
  bottom one as background, two pixels a cell, drawn by the program in
  its own cells (tu maps the colours to the terminal's tier). Without
  UTF-8 a cell is one pixel, a coloured space. This works everywhere
  there is colour, including winterm and xwsh on IRIX.
- **kitty**: RGBA in base64 chunks (`a=T,f=32`), placed in a box of
  cells (`c`, `r`), no replies (`q=2`), the cursor left where it is.
  Deleted by id before a dialog and when the viewer closes, since kitty
  images lie over the text.
- **Sixel**: a 6x6x6 colour cube with 4x4 ordered dithering, at the
  pixel size the terminal reports for its cells (`TIOCGWINSZ`), else
  8x16.
- **iTerm2**: an inline file (`OSC 1337`), sent as an uncompressed PNG
  ti writes itself, sized in cells.

`detect_protocol()` reads the environment: kitty, ghostty and ttrt
(`TERM_PROGRAM=ttrt`) use kitty's protocol, iTerm2 and WezTerm iTerm2's,
foot, mlterm, konsole, contour and Windows Terminal sixels, and everything else (and anything inside
tmux or screen, which pass none of them through) half blocks. ttrc's
Options > Appearance overrides it.

The program draws its screen first; graphics go to the terminal after
the screen is flushed (`FullScreen::paint_over`), again after anything
covered them. Sixel and iTerm2 pixels are replaced by text written over
them, so leaving the image redraws the whole screen.

## 4. Headers: probe()

`probe()` reads a file's format, size, bits a pixel and palette size from
its header alone, for a file browser's image properties column, where
decoding every file would take minutes on an Indy. Each format's header
is read by ti's own code (`ti/probe.cpp`), bounds-checked against the
bytes it was given, so the first 64 KB of a file are enough for nearly
all of them. `decode()` fills the same `Info::bits` and `Info::colors`.
`bits` counts all channels: 24 for RGB, 32 for RGBA or CMYK, 8 for a
palette or grey image; `colors` is the palette's size for indexed images
(PNG's PLTE, GIF's table, BMP's colours used, an XPM's colours) and 0
otherwise.

## 5. XPM

ti reads XPM 3 itself (`ti/xpm.cpp`), from memory, so files inside
archives and decoder threads need no X connection. Colours are the
visual key's value in the order c, g, g4, m; `None` is transparent;
`#RGB` to `#RRRRGGGGBBBB`; X colour names come from the system's
`rgb.txt` (`/usr/share/X11` on Linux, `/usr/lib/X11` on IRIX), read
once, with a few names and `grayN` built in when it is missing. A colour
ti does not know is black, where libXpm would refuse the file.

## 6. Threads

`decode()` and `probe()` can run in several threads at once once the
program has called `set_lock()` with a mutex (ttr's code stays free of
pthreads; the program brings them). ti holds it only where a decoder
keeps global state:

- libtiff: its error and warning handlers are process-wide, so a TIFF
  decode holds the lock throughout.
- The XPM colour names, loaded on first use.

`set_lock()` also fills the decoder names, which are made on first use,
before any thread can race for them. libpng, libjpeg and giflib keep
their state in the decoder's own structures. stb_image's failure reason
is a global: thread-local where the compiler has `__thread` (GCC,
Clang); with MIPSpro two threads failing at once may get each other's
message. The tests decode every fixture in four threads with both
decoder sets and compare the pixels with a single thread's; the
ThreadSanitizer build reports no races.

## 7. Not done

Animation (GIF shows its first frame), colour profiles, EXIF rotation,
16-bit output, WebP and AVIF, and zooming in the viewer.
