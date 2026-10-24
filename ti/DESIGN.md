# ti: ttr's images

ti decodes image files into 8-bit RGBA, scales them, and encodes them
for terminals. ttrc's viewer (F3) shows images with it, and ttr's icon
loader reads PNG icons through it. C++98, no STL, no exceptions; it
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

`detect_protocol()` reads the environment: kitty and ghostty use kitty's
protocol, iTerm2 and WezTerm iTerm2's, foot, mlterm, konsole, contour
and Windows Terminal sixels, and everything else (and anything inside
tmux or screen, which pass none of them through) half blocks. ttrc's
Options > Appearance overrides it.

The program draws its screen first; graphics go to the terminal after
the screen is flushed (`FullScreen::paint_over`), again after anything
covered them. Sixel and iTerm2 pixels are replaced by text written over
them, so leaving the image redraws the whole screen.

## 4. Not done

Animation (GIF shows its first frame), colour profiles, EXIF rotation,
16-bit output, WebP and AVIF, and zooming in the viewer.
