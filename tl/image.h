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
