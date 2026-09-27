#pragma once

#include "globaldefs.h"

#include "memory.h"
#include "vio.h"
#include "tv.h"


#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

class PixelFiller
{
private:
    bool mode512;
    int raster_pixel;   // horizontal pixel counter
    int raster_line;    // raster line counter
    int fb_column;      // frame buffer column
    int fb_row;         // frame buffer row
    bool vborder;       // vertical border flag
    bool visible;       // visible area flag
    int bmpofs;         // bitmap offset for current pixel
    int border_index;
    int first_visible_line;
    int center_offset;
    int screen_width;

    uint32_t pixel32;
#if USE_BIT_PERMUTE
    uint32_t pixel32_grouped;
#endif
    uint32_t * mem32;
    uint32_t * pixels;

    Memory & memory;
    IO & io;
    TV & tv;

    int fill1_count, fill2_count;

public:
    bool brk;
    bool irq;
    int irq_clk;

public:
    PixelFiller(Memory & _mem, IO & _io, TV & _tv);
    void init();
    void reset();
    void fetchPixels();
    int shiftOutPixels();
    int getColorIndex(int rpixel, bool border);

    int fill(int clocks, int commit_time, int commit_time_pal, bool updateScreen);
    int fill1(int clocks, int commit_time, int commit_time_pal, bool updateScreen);
    int fill2(int clocks);
    int fill3(int clocks);
    int fill4(int clocks);
    void advanceLine(bool updateScreen);

#ifdef V06C_DEBUGGER
    // Debugger-only READ-ONLY accessors (Stage 6.27: raster/beam debugging).
    // These expose state that PixelFiller has ALREADY computed during the normal
    // rasterization loop. They never mutate anything and do not touch the fill
    // path, so the vanilla emulator (macro undefined) is completely unaffected.
    int  rasterLine() const        { return this->raster_line; }
    int  rasterPixel() const       { return this->raster_pixel; }
    int  fbRow() const             { return this->fb_row; }
    int  fbColumn() const          { return this->fb_column; }
    bool vBorder() const           { return this->vborder; }
    bool isVisible() const         { return this->visible; }
    int  borderIndex() const       { return this->border_index; }
    bool isMode512() const         { return this->mode512; }
    int  firstVisibleLine() const  { return this->first_visible_line; }
    int  centerOffset() const      { return this->center_offset; }
    int  scrWidth() const          { return this->screen_width; }

    // The 4-bit color/palette index the video path uses at the CURRENT beam
    // position -- i.e. the entry that an OUT 0Ch committing right now would
    // write (vio.h::commit_palette(index)). For border/blanking it is the
    // border index; inside the picture it is the pixel group that the next
    // shiftOutPixels() would emit, read WITHOUT shifting. Purely const.
    int currentColorIndex() const {
        const int rpixel = this->raster_pixel - 24;
        const bool hb = this->vborder ||
            (rpixel < (768 - 512) / 2) || (rpixel >= (768 - (768 - 512) / 2));
        if (hb) {
            return this->border_index;
        }
#if USE_BIT_PERMUTE
        return static_cast<int>(this->pixel32_grouped >> 28);
#else
        const uint32_t p = this->pixel32;
        return static_cast<int>((p >> 4 & 8) | (p >> 13 & 4) |
                                (p >> 22 & 2) | (p >> 31 & 1));
#endif
    }
#endif
};
