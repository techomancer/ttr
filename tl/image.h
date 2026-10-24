/*
 * tl/image.h - images in the terminal (DESIGN.md section 5.4): the store
 * that owns their pixels, and the sixel decoder.
 *
 * A placed image is a block of image cells, one per character cell it
 * covers (Cell::set_image): the image's id and which tile of it. Cells
 * carry their tiles through scrolling, insert and delete, scrollback and
 * the screen switch like any text, and text written over a cell replaces
 * its tile, as on a VT340. The store frees an image when no cell shows it
 * any more (collect()).
 */
#ifndef TL_IMAGE_H
#define TL_IMAGE_H

#include "port.h"

namespace tl {

class Grid;

struct Image {
    int w, h;               /* pixels */
    uint32_t* px;           /* 0xAARRGGBB, rows from the top; A 0: transparent */
    int cell_w, cell_h;     /* the cell size it was placed with */
    uint32_t serial;        /* never reused: tells a renderer's cache apart */
};

class ImageStore {
public:
    enum { MAX_ID = 4095 };

    ImageStore();
    ~ImageStore();

    /* Take px (malloc'd, w * h pixels); 0 if the store is full even after
     * dropping the oldest images. The id is 1..MAX_ID. */
    int add(uint32_t* px, int w, int h, int cell_w, int cell_h);
    const Image* get(int id) const;
    /* Free the images no cell of these grids shows. */
    void collect(const Grid* const* grids, int n);
    void clear();
    size_t bytes() const { return bytes_; }
    int count() const { return count_; }
    /* Pixels kept at most; past it the oldest images go first. */
    void set_budget(size_t bytes) { budget_ = bytes; }

private:
    void drop(int id);
    int oldest() const;

    Image slots_[MAX_ID + 1];
    int count_;
    int next_;              /* where the search for a free id starts */
    uint32_t serial_;
    size_t bytes_;
    size_t budget_;

    ImageStore(const ImageStore&);
    ImageStore& operator=(const ImageStore&);
};

/* Images sent with the kitty graphics protocol, by the client's id, kept
 * until deleted (or dropped, oldest first, past the budget) so they can
 * be placed again. Placing one copies it into the ImageStore. */
class KittyImages {
public:
    enum { MAX = 256 };
    struct Entry {
        uint32_t id;        /* the client's i=, or one we chose */
        uint32_t number;    /* I=, 0: none */
        uint32_t* px;       /* 0xAARRGGBB */
        int w, h;
        uint32_t age;
    };

    KittyImages();
    ~KittyImages();
    /* Take px; an image with the same id is replaced. False if refused. */
    bool add(uint32_t id, uint32_t number, uint32_t* px, int w, int h);
    const Entry* get(uint32_t id) const;
    /* The newest image with this number. */
    const Entry* by_number(uint32_t number) const;
    void remove(uint32_t id);
    void clear();
    /* An id no image has, for images sent with only a number. */
    uint32_t free_id() const;
    size_t bytes() const { return bytes_; }
    int count() const { return n_; }
    void set_budget(size_t b) { budget_ = b; }

private:
    void drop(int i);

    Entry e_[MAX];
    int n_;
    uint32_t age_;
    size_t bytes_, budget_;

    KittyImages(const KittyImages&);
    KittyImages& operator=(const KittyImages&);
};

/* The keys of a kitty graphics command (APC G keys ; payload). */
struct KittyCommand {
    char action;            /* a: t T p d q (and f a c, not supported) */
    char medium;            /* t: d (direct; f, t and s are refused) */
    char compression;       /* o: z or 0 */
    char del;               /* d: what a=d deletes */
    int format;             /* f: 24, 32, 100 */
    uint32_t id, number, placement;     /* i, I, p */
    int src_w, src_h;       /* s, v: size of raw pixels */
    int x, y, w, h;         /* the source rectangle; for a=d, a cell (1-based) */
    int off_x, off_y;       /* X, Y: pixels into the first cell */
    int cols, rows;         /* c, r: the box of cells to show it in */
    int more;               /* m: more chunks follow */
    int quiet;              /* q: 1 no OK, 2 no errors either */
    int cursor;             /* C: 1 leaves the cursor */
    int z;

    void clear();
    /* "a=T,f=32,..." (the bytes before ';'); false if malformed. */
    bool parse(const uint8_t* s, size_t n);
};

/* src (sw x sh), the rectangle x, y, w, h of it scaled to tw x th by area
 * average, placed pad_x, pad_y pixels into a new malloc'd image of
 * (pad_x + tw) x (pad_y + th) with transparency around it. 0 on failure. */
uint32_t* resample(const uint32_t* src, int sw, int sh, int x, int y, int w, int h, int tw,
                   int th, int pad_x, int pad_y);

/* Standard base64 into out (at most n * 3 / 4 bytes); returns the
 * length. Characters outside the alphabet are skipped. */
size_t base64_decode(const uint8_t* s, size_t n, uint8_t* out);

/* DEC sixel graphics, from the bytes of a DCS ... q string. */
class SixelDecoder {
public:
    enum { MAX_SIDE = 4096, REGISTERS = 256 };

    SixelDecoder();
    ~SixelDecoder();
    /* P1 (aspect, ignored: pixels are square), P2 (1: pixels not drawn are
     * transparent; else they are 'bg'). */
    void begin(int p2, uint32_t bg);
    void put(const uint8_t* s, size_t n);
    /* The image: true with a malloc'd 0xAARRGGBB buffer the caller owns,
     * false if nothing was drawn. */
    bool finish(uint32_t** px, int* w, int* h);

private:
    void command(uint8_t c);
    void sixel(int bits, int count);
    bool grow(int w, int h);
    void set_register(int i, int space, int a, int b, int c);

    uint32_t* px_;
    int cap_w_, cap_h_;     /* allocated */
    int w_, h_;             /* used */
    int raster_w_, raster_h_;
    int x_, y_;             /* y_: the top row of the current band */
    uint32_t color_;
    uint32_t bg_;
    bool transparent_;
    uint32_t regs_[REGISTERS];
    /* A command being read: '#', '!', '"' and their numbers. */
    uint8_t cmd_;
    int nums_[5];
    int n_nums_;
    bool failed_;

    SixelDecoder(const SixelDecoder&);
    SixelDecoder& operator=(const SixelDecoder&);
};

} /* namespace tl */

#endif
