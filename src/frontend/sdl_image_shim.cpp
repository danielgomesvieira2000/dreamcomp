// IMG_LoadTyped_IO for RmlUi's SDL renderer backend, on the engine's PNG decoder (no SDL_image).
#include "SDL3_image/SDL_image.h"

#include <cstring>
#include <string>
#include <vector>

#include "dream/render/png.h"

extern "C" SDL_Surface* IMG_LoadTyped_IO(SDL_IOStream* src, bool closeio, const char* type) {
    if (!src) {
        SDL_SetError("IMG_LoadTyped_IO: no stream");
        return nullptr;
    }
    std::vector<std::uint8_t> bytes;
    const Sint64 size = SDL_GetIOSize(src);
    if (size > 0) {
        bytes.resize(static_cast<std::size_t>(size));
        const std::size_t got = SDL_ReadIO(src, bytes.data(), bytes.size());
        bytes.resize(got);
    }
    if (closeio)
        SDL_CloseIO(src);
    if (type && SDL_strcasecmp(type, "png") != 0) {
        SDL_SetError("IMG_LoadTyped_IO: only PNG is supported (got %s)", type);
        return nullptr;
    }
    dream::render::png::Image img;
    std::string err;
    if (!dream::render::png::decode(bytes.data(), bytes.size(), img, &err)) {
        SDL_SetError("PNG: %s", err.c_str());
        return nullptr;
    }
    // The decoder packs r | g << 8 | b << 16 | a << 24: bytes R, G, B, A in memory on a
    // little-endian host, which SDL calls ABGR8888 (packed, high byte first) = RGBA32.
    SDL_Surface* s = SDL_CreateSurface(static_cast<int>(img.width), static_cast<int>(img.height),
                                       SDL_PIXELFORMAT_ABGR8888);
    if (!s)
        return nullptr;
    for (std::uint32_t y = 0; y < img.height; ++y)
        std::memcpy(static_cast<std::uint8_t*>(s->pixels) + static_cast<std::size_t>(y) * s->pitch,
                    img.pixels.data() + static_cast<std::size_t>(y) * img.width, img.width * 4u);
    return s;
}
