#include "SDL.h"

// stb_image — single-header image decoding library
// Define implementation in this translation unit
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Embedded PNG data (linked via objcopy from icon64.png)
extern "C" uint8_t _binary_icon64_png_start[];
extern "C" uint8_t _binary_icon64_png_end[];
extern "C" uint8_t _binary_icon64_png_size[];

void icon_set(SDL_Window * w)
{
    // Decode embedded PNG using stb_image
    uint8_t * png_data = _binary_icon64_png_start;
    size_t png_size = (size_t)_binary_icon64_png_size;

    int width, height, channels;
    uint8_t * rgba = stbi_load_from_memory(png_data, (int)png_size,
                                            &width, &height, &channels, 4);
    if (!rgba) return;

    // Create SDL surface from decoded RGBA data
    // On little-endian x86, SDL_PIXELFORMAT_ABGR8888 stores bytes as [R,G,B,A]
    SDL_Surface * surface = SDL_CreateRGBSurfaceWithFormatFrom(
        rgba, width, height, 32, width * 4, SDL_PIXELFORMAT_ABGR8888);

    if (surface) {
        SDL_SetWindowIcon(w, surface);
        SDL_FreeSurface(surface);
    }

    // Free stb_image allocated memory
    stbi_image_free(rgba);
}
