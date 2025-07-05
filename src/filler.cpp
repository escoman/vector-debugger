#include "globaldefs.h"
#include "filler.h"

int PixelFiller::fb_column = 0;
int PixelFiller::fb_row = 0;
uint32_t * PixelFiller::mem32 = 0;
uint32_t * PixelFiller::pixels = 0;

uint32_t PixelFiller::pixel32;
#if USE_BIT_PERMUTE
uint32_t PixelFiller::pixel32_grouped;
#endif

bool PixelFiller::mode512;
int  PixelFiller::raster_pixel;   // horizontal pixel counter
int  PixelFiller::raster_line;    // raster line counter
bool PixelFiller::vborder;       // vertical border flag
bool PixelFiller::visible;       // visible area flag
int  PixelFiller::bmpofs;         // bitmap offset for current pixel
int  PixelFiller::border_index;
//int  PixelFiller::first_visible_line;
//int  PixelFiller::center_offset;
//int  PixelFiller::screen_width;

bool PixelFiller::brk;
bool PixelFiller::irq;
int PixelFiller::irq_clk;

IO * PixelFiller::io;
TV * PixelFiller::tv;

// TODO: fix neon shimmering
//#undef __ARM_NEON

// palette ram
// index:4 -> {rgb,rgb}
uint32_t py2[32];

uint32_t py2_512[32];       // mode512 pairs of pixels
uint32_t py2_256[32];       // mode256 pairs of pixels

void PixelFiller::write_pal(uint8_t adr8, uint32_t rgb)
{
    // write 256-pixel pal
    py2_256[adr8<<1] = rgb;
    py2_256[(adr8<<1) + 1] = rgb;

    // 512-pixel pal (different odd/even columns)
    if (adr8 <= 3) {
        // even columns 0, 2, ...
        uint8_t adr = adr8 & 0x03;
        // replicate for every combo of msb in lsb pixel
        py2_512[(adr + 0x0) << 1] = rgb;
        py2_512[(adr + 0x4) << 1] = rgb;
        py2_512[(adr + 0x8) << 1] = rgb;
        py2_512[(adr + 0xc) << 1] = rgb;
    }
    if ((adr8 & 3) == 0) {
        // odd columns 1, 3, ...
        uint8_t adr = adr8 & 0x0c;
        // replicate for every combo of lsb in msb pixel
        py2_512[((adr + 0x0) << 1) + 1] = rgb;
        py2_512[((adr + 0x1) << 1) + 1] = rgb;
        py2_512[((adr + 0x2) << 1) + 1] = rgb;
        py2_512[((adr + 0x3) << 1) + 1] = rgb;
    }
}

void PixelFiller::modechange()
{
    if (PixelFiller::mode512) {
        memcpy(py2, py2_512, sizeof(py2));
    }
    else {
        memcpy(py2, py2_256, sizeof(py2));
    }
}


PixelFiller::PixelFiller(Memory & _mem, IO * _io, TV * _tv)
{
    PixelFiller::mode512 = false;
    PixelFiller::mem32 = ((uint32_t *) _mem.buffer());
    PixelFiller::pixel32 = 0;  // 4 bytes of bit planes
    PixelFiller::border_index = 0;
    PixelFiller::raster_pixel = 0;
    PixelFiller::io = _io;
    PixelFiller::tv = _tv;

    PixelFiller::reset();

    PixelFiller::io->onborderchange = [this](int border) {
        //printf("onborderchange: %x\n", border);
        PixelFiller::border_index = border;
    };

    PixelFiller::io->onmodechange = [this](bool mode) {
        PixelFiller::mode512 = mode;
        modechange();
    };

    PixelFiller::io->on_commit_palette = [this](uint8_t adr8, uint32_t rgb) {
        PixelFiller::write_pal(adr8, rgb);
        PixelFiller::modechange();
    };
}

void PixelFiller::init()
{
    //PixelFiller::first_visible_line = 312 - Options.screen_height;
    //PixelFiller::center_offset = Options.center_offset;
    //PixelFiller::screen_width = Options.screen_width;
}

void PixelFiller::reset()
{
    // It is tempting to reset the pixel count but the beam is reset in 
    // advanceLine(), don't do that here.
    //PixelFiller::raster_pixel = 0;   // horizontal pixel counter

    PixelFiller::raster_line = 0;    // raster line counter
    PixelFiller::fb_column = 0;      // frame buffer column
    PixelFiller::fb_row = 0;         // frame buffer row
    PixelFiller::vborder = true;     // vertical border flag
    PixelFiller::visible = false;    // visible area flag
    PixelFiller::bmpofs = 0;         // bitmap offset for current pixel
    PixelFiller::brk = false;
    PixelFiller::irq = false;
    PixelFiller::pixels = PixelFiller::tv->pixels();
}

#if USE_BIT_PERMUTE
static uint32_t bit_permute_step(uint32_t x, uint32_t m, uint32_t shift) {
    uint32_t t;
    t = ((x >> shift) ^ x) & m;
    x = (x ^ t) ^ (t << shift);
    return x;
}
#endif

void PixelFiller::fetchPixels() 
{
    size_t addr = ((PixelFiller::fb_column & 0xff) << 8) | (PixelFiller::fb_row & 0xff);
    PixelFiller::pixel32 = PixelFiller::mem32[0x2000 + addr];

#if USE_BIT_PERMUTE
    // h/t Code generator for bit permutations
    // http://programming.sirrida.de/calcperm.php
    // Input:
    // 31 23 15 7 30 22 14 6 29 21 13 5 28 20 12 4 27 19 11 3 26 18 10 2 25 17 9 1  24 16 8 0
    // LSB, indices refer to source, Beneš/BPC
    uint32_t x = PixelFiller::pixel32;
    x = bit_permute_step(x, 0x00550055, 9);  // Bit index swap+complement 0,3
    x = bit_permute_step(x, 0x00003333, 18);  // Bit index swap+complement 1,4
    x = bit_permute_step(x, 0x000f000f, 12);  // Bit index swap+complement 2,3
    x = bit_permute_step(x, 0x000000ff, 24);  // Bit index swap+complement 3,4

    PixelFiller::pixel32_grouped = x;
#endif
}

int PixelFiller::shiftOutPixels()
{
//        uint32_t p = PixelFiller::pixel32;
//        // msb of every byte in p stands for bit plane
//        uint32_t modeless = (p >> 4 & 8) | (p >> 13 & 4) | (p >> 22 & 2) | (p >> 31 & 1);
//        // shift left
//        PixelFiller::pixel32 = (p << 1);// & 0xfefefefe; -- unnecessary
//        return modeless;
#if USE_BIT_PERMUTE
    uint32_t modeless = PixelFiller::pixel32_grouped >> 28;
    PixelFiller::pixel32_grouped <<= 4;
#else
    uint32_t p = PixelFiller::pixel32;
    // msb of every byte in p stands for bit plane
    uint32_t modeless = (p >> 4 & 8) | (p >> 13 & 4) | (p >> 22 & 2) | (p >> 31 & 1);
    // shift left
    PixelFiller::pixel32 = (p << 1);// & 0xfefefefe; -- unnecessary
#endif
    return modeless;
}

int PixelFiller::getColorIndex(int rpixel, bool border) {
    if (border) {
        PixelFiller::fb_column = 0;
        return PixelFiller::border_index;
    } else {
        if ((rpixel & 0x0f) == 0) {
            PixelFiller::fetchPixels();
            ++PixelFiller::fb_column;
        }
        return PixelFiller::shiftOutPixels();
    }
}

#define TESTTABLE 0

int PixelFiller::fill(int clocks, int commit_time, 
        int commit_time_pal, bool updateScreen) 
{
    if (TESTTABLE || commit_time || commit_time_pal || 
            PixelFiller::raster_line == 22 + 18 || 
            PixelFiller::raster_line == 0 ||
            PixelFiller::raster_line == 311 ||
            PixelFiller::raster_pixel <= (768-512)/2 + clocks ||
            PixelFiller::raster_pixel + clocks >= 768-(768-512)/2)
    {
        //fill1_count += clocks;
        return fill1(clocks, commit_time, commit_time_pal, updateScreen);
    } else {
        //fill2_count += clocks;
        if (!PixelFiller::visible) {
            PixelFiller::raster_pixel += clocks;
            return 0;
        }
        else if (PixelFiller::vborder) {
            return fill4(clocks);
        }
        else if (PixelFiller::mode512) {
            return fill3(clocks);
        } 
        else {
            return fill2(clocks);
        }
    }
}

int PixelFiller::fill1(int clocks, int commit_time, int commit_time_pal, bool updateScreen) {
    uint32_t * bmp = PixelFiller::pixels;
    int clk;
    int afterbrk = 0;
    int index = 0;

    for (clk = 0; clk < clocks; clk += 2, afterbrk += PixelFiller::brk ? 2 : 0) {
        // offset for matching border/palette writes and the raster -- test:bord2
        const int rpixel = PixelFiller::raster_pixel - 24;
        bool border = PixelFiller::vborder || 
            /* hborder */ (rpixel < (768-512)/2) || (rpixel >= (768 - (768-512)/2));

        index = PixelFiller::getColorIndex(rpixel, border);
        if (clk == commit_time) {
            PixelFiller::io->commit(); // regular i/o writes (border index); test: bord2
        }
        if (clk == commit_time_pal) {
            PixelFiller::io->commit_palette(index); // palette writes; test: bord2
        }
        if (PixelFiller::visible) {
            const int bmp_x = PixelFiller::raster_pixel - PixelFiller::center_offset;
            if (bmp_x >= 0 && bmp_x < PixelFiller::screen_width) {
                //if (PixelFiller::mode512) {// && !border -- border A/B alternation, see Cherezov page 7
                //    bmp[PixelFiller::bmpofs++] = PixelFiller::io->Palette(index & 0x03);
                //    bmp[PixelFiller::bmpofs++] = PixelFiller::io->Palette(index & 0x0c);
                //} else {
                //    uint32_t p = PixelFiller::io->Palette(index);
                //    bmp[PixelFiller::bmpofs++] = p;
                //    bmp[PixelFiller::bmpofs++] = p;
                //}

                bmp[PixelFiller::bmpofs++] = py2[index << 1];
                bmp[PixelFiller::bmpofs++] = py2[(index << 1) + 1];
            }
        }
        // 22 vsync + 18 border + 256 picture + 16 border = 312 lines
        PixelFiller::raster_pixel += 2;
        if (PixelFiller::raster_pixel == 768) {
            PixelFiller::advanceLine(updateScreen);
        }
        // load scroll register at this precise moment -- test:scrltst2
        if (PixelFiller::raster_line == 22 + 18 && PixelFiller::raster_pixel == 150) {
            PixelFiller::fb_row = PixelFiller::io->ScrollStart();
        }
        // irq time -- test:bord2, vst (MovR=1d37, MovM=1d36)
        else if (PixelFiller::raster_line == 0 && PixelFiller::raster_pixel == 174) {
            PixelFiller::irq = true;
            PixelFiller::irq_clk = clk;
        }
    } 

    if (clk == commit_time) {
        PixelFiller::io->commit(); // regular i/o writes (border index); test: bord2
    }
    if (clk == commit_time_pal) {
        PixelFiller::io->commit_palette(index); // palette writes; test: bord2
    }

    if (afterbrk) {
        afterbrk -= 2;
    }
    return afterbrk;
}

void PixelFiller::advanceLine(bool updateScreen) {
    PixelFiller::raster_pixel = 0;
    PixelFiller::raster_line += 1;
    PixelFiller::fb_row -= 1;
    if (!PixelFiller::vborder && PixelFiller::fb_row < 0) {
        PixelFiller::fb_row = 0xff;
    }
    // update vertical border only when line changes
    PixelFiller::vborder = (PixelFiller::raster_line < 40) || (PixelFiller::raster_line >= (40 + 256));
    // turn on pixel copying after blanking area
    PixelFiller::visible = PixelFiller::visible || 
        (updateScreen && PixelFiller::raster_line == PixelFiller::first_visible_line);
    if (PixelFiller::raster_line == 312) {
        PixelFiller::raster_line = 0;
        PixelFiller::visible = false; // blanking starts
        PixelFiller::brk = true;
    }
}

/* simple fill, no out instructions underway, mode 256 */
int PixelFiller::fill2(int clocks)
{
    uint32_t * const bmp = PixelFiller::pixels;
    int clk;

    int ofs = PixelFiller::bmpofs;

    // clocks=16/32/48/64/80/96..

    int rpixel = PixelFiller::raster_pixel - 24;
    PixelFiller::raster_pixel += clocks;

    for (clk = 0; clk < clocks; clk += 16) {
        uint32_t p0 = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        uint32_t p1 = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        uint32_t p2 = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        uint32_t p3 = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        uint32_t p4 = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        uint32_t p5 = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        uint32_t p6 = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        uint32_t p7 = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;

#if __ARM_NEON
        uint32x4_t d0,d1,d2,d3;

        p0 = PixelFiller::io->Palette(p0);
        p1 = PixelFiller::io->Palette(p1);
        d0 = vsetq_lane_u32(p0, d0, 0);
        d0 = vsetq_lane_u32(p0, d0, 1);
        d0 = vsetq_lane_u32(p1, d0, 2);
        d0 = vsetq_lane_u32(p1, d0, 3);

        p2 = PixelFiller::io->Palette(p2);
        p3 = PixelFiller::io->Palette(p3);
        d1 = vsetq_lane_u32(p2, d1, 0);
        d1 = vsetq_lane_u32(p2, d1, 1);
        d1 = vsetq_lane_u32(p3, d1, 2);
        d1 = vsetq_lane_u32(p3, d1, 3);

        p4 = PixelFiller::io->Palette(p4);
        p5 = PixelFiller::io->Palette(p5);
        d2 = vsetq_lane_u32(p4, d2, 0);
        d2 = vsetq_lane_u32(p4, d2, 1);
        d2 = vsetq_lane_u32(p5, d2, 2);
        d2 = vsetq_lane_u32(p5, d2, 3);

        p6 = PixelFiller::io->Palette(p6);
        p7 = PixelFiller::io->Palette(p7);
        d3 = vsetq_lane_u32(p6, d3, 0);
        d3 = vsetq_lane_u32(p6, d3, 1);
        d3 = vsetq_lane_u32(p7, d3, 2);
        d3 = vsetq_lane_u32(p7, d3, 3);


        vst1q_u32(&bmp[ofs], d0); ofs+= 4;
        vst1q_u32(&bmp[ofs], d1); ofs+= 4;
        vst1q_u32(&bmp[ofs], d2); ofs+= 4;
        vst1q_u32(&bmp[ofs], d3); ofs+= 4;
#else
        p0 = PixelFiller::io->Palette(p0);
        p1 = PixelFiller::io->Palette(p1);
        p2 = PixelFiller::io->Palette(p2);
        p3 = PixelFiller::io->Palette(p3);
        p4 = PixelFiller::io->Palette(p4);
        p5 = PixelFiller::io->Palette(p5);
        p6 = PixelFiller::io->Palette(p6);
        p7 = PixelFiller::io->Palette(p7);

        bmp[ofs++] = p0; bmp[ofs++] = p0;
        bmp[ofs++] = p1; bmp[ofs++] = p1;
        bmp[ofs++] = p2; bmp[ofs++] = p2;
        bmp[ofs++] = p3; bmp[ofs++] = p3;
        bmp[ofs++] = p4; bmp[ofs++] = p4;
        bmp[ofs++] = p5; bmp[ofs++] = p5;
        bmp[ofs++] = p6; bmp[ofs++] = p6;
        bmp[ofs++] = p7; bmp[ofs++] = p7;
#endif

    } 

    PixelFiller::bmpofs = ofs;
    return 0;
}

/* simple fill, no out instructions underway, mode 512 */
int PixelFiller::fill3(int clocks)
{
    uint32_t * const bmp = PixelFiller::pixels;
    int clk;

    int ofs = PixelFiller::bmpofs;

    // clocks=16/32/48/64/80/96..

    int rpixel = PixelFiller::raster_pixel - 24;
    PixelFiller::raster_pixel += clocks;

    int index;
    for (clk = 0; clk < clocks; clk += 16) {
#if __ARM_NEON
        uint32x4_t d0,d1,d2,d3;

        index = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        d0 = vsetq_lane_u32(PixelFiller::io->Palette(index & 0x03), d0, 0);
        d0 = vsetq_lane_u32(PixelFiller::io->Palette(index & 0x0c), d0, 1);
        index = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        d0 = vsetq_lane_u32(PixelFiller::io->Palette(index & 0x03), d0, 2);
        d0 = vsetq_lane_u32(PixelFiller::io->Palette(index & 0x03), d0, 3);

        index = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        d1 = vsetq_lane_u32(PixelFiller::io->Palette(index & 0x03), d1, 0);
        d1 = vsetq_lane_u32(PixelFiller::io->Palette(index & 0x0c), d1, 1);
        index = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        d1 = vsetq_lane_u32(PixelFiller::io->Palette(index & 0x03), d1, 2);
        d1 = vsetq_lane_u32(PixelFiller::io->Palette(index & 0x03), d1, 3);


        index = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        d2 = vsetq_lane_u32(PixelFiller::io->Palette(index & 0x03), d2, 0);
        d2 = vsetq_lane_u32(PixelFiller::io->Palette(index & 0x0c), d2, 1);
        index = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        d2 = vsetq_lane_u32(PixelFiller::io->Palette(index & 0x03), d2, 2);
        d2 = vsetq_lane_u32(PixelFiller::io->Palette(index & 0x03), d2, 3);


        index = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        d3 = vsetq_lane_u32(PixelFiller::io->Palette(index & 0x03), d3, 0);
        d3 = vsetq_lane_u32(PixelFiller::io->Palette(index & 0x0c), d3, 1);
        index = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        d3 = vsetq_lane_u32(PixelFiller::io->Palette(index & 0x03), d3, 2);
        d3 = vsetq_lane_u32(PixelFiller::io->Palette(index & 0x03), d3, 3);

        vst1q_u32(&bmp[ofs], d0); ofs+= 4;
        vst1q_u32(&bmp[ofs], d1); ofs+= 4;
        vst1q_u32(&bmp[ofs], d2); ofs+= 4;
        vst1q_u32(&bmp[ofs], d3); ofs+= 4;
#else
        //
        index = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        bmp[ofs++] = PixelFiller::io->Palette(index & 0x03);
        bmp[ofs++] = PixelFiller::io->Palette(index & 0x0c);
        //
        index = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        bmp[ofs++] = PixelFiller::io->Palette(index & 0x03);
        bmp[ofs++] = PixelFiller::io->Palette(index & 0x0c);
        //
        index = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        bmp[ofs++] = PixelFiller::io->Palette(index & 0x03);
        bmp[ofs++] = PixelFiller::io->Palette(index & 0x0c);
        //
        index = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        bmp[ofs++] = PixelFiller::io->Palette(index & 0x03);
        bmp[ofs++] = PixelFiller::io->Palette(index & 0x0c);
        //
        index = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        bmp[ofs++] = PixelFiller::io->Palette(index & 0x03);
        bmp[ofs++] = PixelFiller::io->Palette(index & 0x0c);
        //
        index = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        bmp[ofs++] = PixelFiller::io->Palette(index & 0x03);
        bmp[ofs++] = PixelFiller::io->Palette(index & 0x0c);
        //
        index = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        bmp[ofs++] = PixelFiller::io->Palette(index & 0x03);
        bmp[ofs++] = PixelFiller::io->Palette(index & 0x0c);
        //
        index = PixelFiller::getColorIndex(rpixel, false); rpixel += 2;
        bmp[ofs++] = PixelFiller::io->Palette(index & 0x03);
        bmp[ofs++] = PixelFiller::io->Palette(index & 0x0c);
#endif
    } 

    PixelFiller::bmpofs = ofs;
    return 0;
}

int PixelFiller::fill4(int clocks)
{
    uint32_t * const bmp = PixelFiller::pixels;
    int clk;

    int ofs = PixelFiller::bmpofs;
    PixelFiller::raster_pixel += clocks;

    uint32_t p = PixelFiller::io->Palette(PixelFiller::getColorIndex(0, true));
    uint64_t p64 = p | (uint64_t)p<<32;
    for (clk = 0; clk < clocks; clk += 16) {
        *(uint64_t*)&bmp[ofs] = p64; ofs += 2;
        *(uint64_t*)&bmp[ofs] = p64; ofs += 2;
        *(uint64_t*)&bmp[ofs] = p64; ofs += 2;
        *(uint64_t*)&bmp[ofs] = p64; ofs += 2;
        *(uint64_t*)&bmp[ofs] = p64; ofs += 2;
        *(uint64_t*)&bmp[ofs] = p64; ofs += 2;
        *(uint64_t*)&bmp[ofs] = p64; ofs += 2;
        *(uint64_t*)&bmp[ofs] = p64; ofs += 2;
    } 

    PixelFiller::bmpofs = ofs;
    return 0;
}


auto PixelFiller::get_raster_pixel() const -> const int
{
    return raster_pixel;
}
auto PixelFiller::get_raster_line() const -> const int
{
    return raster_line;
}
