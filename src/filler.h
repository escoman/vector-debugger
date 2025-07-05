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
    static int fb_column;      // frame buffer column
    static int fb_row;         // frame buffer row
    static uint32_t * mem32;
    static uint32_t * pixels;
    static uint32_t pixel32;
#if USE_BIT_PERMUTE
    static uint32_t pixel32_grouped;
#endif
    static bool mode512;
    static int raster_pixel;   // horizontal pixel counter
    static int raster_line;    // raster line counter
    static bool vborder;       // vertical border flag
    static bool visible;       // visible area flag
    static int bmpofs;         // bitmap offset for current pixel
    static int border_index;
    static constexpr int center_offset = DEFAULT_CENTER_OFFSET;
    static constexpr int screen_width = DEFAULT_SCREEN_WIDTH;
    static constexpr int first_visible_line = 312 - DEFAULT_SCREEN_HEIGHT;

private:
    static IO * io;
    static TV * tv;

public:
    static bool brk;
    static bool irq;
    static int irq_clk;
    
    auto get_raster_pixel() const -> const int;
    auto get_raster_line() const -> const int;

public:
    PixelFiller(Memory & _mem, IO * _io, TV * _tv);
    static void init();
    static void reset();
    static void fetchPixels();
    static int shiftOutPixels();
    static int getColorIndex(int rpixel, bool border);

    static int fill(int clocks, int commit_time, int commit_time_pal, bool updateScreen);
    static int fill1(int clocks, int commit_time, int commit_time_pal, bool updateScreen);
    static int fill2(int clocks);
    static int fill3(int clocks);
    static int fill4(int clocks);
    static void advanceLine(bool updateScreen);

    static void write_pal(uint8_t adr8, uint32_t rgb);
    static void modechange();
};
