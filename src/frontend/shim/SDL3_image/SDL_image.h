// Stand-in for SDL3_image's header, so RmlUi's SDL renderer backend builds without SDL_image.
// The launcher only loads PNGs; sdl_image_shim.cpp decodes them with the engine's own PNG codec
// (dream/render/png.h). Not SDL_image: only the one function the backend calls.
#pragma once

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// Decodes a PNG from `src` (closed when `closeio` is true) into an RGBA surface. `type` is the
// file extension; anything but "png" fails with SDL_SetError.
SDL_Surface* IMG_LoadTyped_IO(SDL_IOStream* src, bool closeio, const char* type);

#ifdef __cplusplus
}
#endif
