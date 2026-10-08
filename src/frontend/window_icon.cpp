// The port's icon on SDL windows (include/dreamcomp/frontend.h, docs/FRONTEND.md "Icon").
#include <SDL3/SDL.h>

#include <cstdio>

#include "dreamcomp/frontend.h"

namespace dreamcomp::frontend {

void apply_port_icon(void* sdl_window) {
    const unsigned char* png = nullptr;
    std::size_t size = 0;
    if (!sdl_window || !port_icon(&png, &size))
        return;
    SDL_IOStream* io = SDL_IOFromConstMem(png, size);
    SDL_Surface* surface = io ? SDL_LoadPNG_IO(io, true) : nullptr;
    if (!surface) {
        std::printf("dreamcomp: window icon: %s\n", SDL_GetError());
        return;
    }
    if (!SDL_SetWindowIcon(static_cast<SDL_Window*>(sdl_window), surface))
        std::printf("dreamcomp: window icon: %s\n", SDL_GetError());
    SDL_DestroySurface(surface);
}

}  // namespace dreamcomp::frontend
