#include <cstdint>

#include "graphics/Graphics.h"

// uint32_t ARGB driver for Graphics 
// works on externally allocated buffers

class Graphics32: public Graphics<uint32_t> 
{
    typedef uint32_t Color;
private:
    uint32_t * pixels;
public:
    static constexpr Color RMASK = 0x00ff0000;
    static constexpr Color GMASK = 0x0000ff00;
    static constexpr Color BMASK = 0x000000ff;
    static constexpr Color AMASK = 0xff000000;

    Graphics32() : Graphics32(0, 0, nullptr) {}

    Graphics32(int xres, int yres, uint32_t * pixels) :
        Graphics<uint32_t>(xres, yres), pixels(pixels)
    {}

    virtual void setPixelBuffer(Color * pixels) override
    {
        this->pixels = pixels;
    }

    virtual Color** allocateFrameBuffer(int xres, int yres, uint32_t value) override
    {
        uint32_t ** frame = (uint32_t **)malloc(yres * sizeof(uint32_t *));
        for (int y = 0; y < yres; y++)
        {
            frame[y] = &this->pixels[y * xres];
            for (int x = 0; x < xres; x++)
                frame[y][x] = value;
        }

        return frame;
    };

    virtual Color** allocateFrameBuffer() override
    {
        return allocateFrameBuffer(xres, yres, (Color)0);
    }

    inline bool in_cliprect(int x, int y) const
    {
        return (x >= clip_rect.x && x < clip_rect.x + clip_rect.w &&
                y >= clip_rect.y && y < clip_rect.y + clip_rect.h);
    }

    virtual void dotFast(int x, int y, Color color) override
    {
        backBuffer[y][x] = color;
    }
    virtual void dot(int x, int y, Color color) override
    {
        if (in_cliprect(x, y)) 
            backBuffer[y][x] = color;
    }

    virtual void dotAdd(int x, int y, Color color) override
    {
        if (in_cliprect(x, y))
        {
            int c0 = backBuffer[y][x];
            int c1 = color;

            Color r = (c0 & RMASK) + (c1 & RMASK);
            if (r > RMASK) r = RMASK;

            Color g = (c0 & GMASK) + (c1 & GMASK);
            if (g > GMASK) g = GMASK;

            Color b = (c0 & BMASK) + (c1 & BMASK);
            if (b > BMASK) g = BMASK;

            backBuffer[y][x] = r | g | b;
        }
    }

    virtual void dotMix(int x, int y, Color color) override
    {
        dot(x, y, color); // TODO: alpha mix
    }

    virtual Color get(int x, int y) override
    {
        if ((unsigned)x < (unsigned)xres && (unsigned)y < (unsigned)yres)
            return backBuffer[y][x];
        return 0;
    }

    virtual void clear(Color color = 0) override
    {
        for (int y = clip_rect.y; y < clip_rect.y + clip_rect.h; y++)
            for (int x = clip_rect.x; x < clip_rect.x + clip_rect.w; x++)
                backBuffer[y][x] = color;
    }

    virtual Color RGBA(int r, int g, int b, int a = 255) const override
    {
        return ((a & 255) << 24) | ((r & 255) << 16) | ((g & 255) << 8) | (b & 255);
    }

    virtual int R(Color c) const override
    {
        return c & 255;
    }

    virtual int G(Color c) const override
    {
        return (c >> 8) & 255;
    }

    virtual int B(Color c) const override
    {
        return (c >> 16) & 255;
    }

    virtual int A(Color c) const override
    {
        return c >> 24;
    }

    virtual Color mix(Color x, Color y, float a)
    {
        int xr = R(x);
        int xg = G(x);
        int xb = B(x);
        int xa = A(x);

        int yr = R(y);
        int yg = G(y);
        int yb = B(y);
        int ya = A(y);

        int mr = round(xr * (1.0f - a) + yr * a);
        int mg = round(xg * (1.0f - a) + yg * a);
        int mb = round(xb * (1.0f - a) + yb * a);
        int ma = round(xa * (1.0f - a) + ya * a);

        return RGBA(mr, mg, mb, ma); 
    }
};

