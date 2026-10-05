// Geometry interpolation between two consecutive display lists (dreamcomp addition).
//
// Dreamcast games transform vertices on the CPU, so a display list holds screen-space geometry and
// no matrices. To draw a frame between two game frames, each strip of the newer frame is paired
// with the same strip of the older one and their vertex positions are blended. Strips carry no
// identity; pairing relies on games emitting their scene in a stable order, which
// docs/INTERPOLATION.md (dreamcomp) measures per game. A strip pairs with one of the same signature
// -- the parameter words (PCW/ISP/TSP/TCW) and vertex count -- in submission order, skipping at most
// `window` unmatched strips on either side (objects appearing, disappearing or clipped differently).
#pragma once

#include <cstdint>
#include <vector>

#include "dream/render/display_list.h"

namespace dream::render {

struct StripPair {
    std::uint32_t list = 0;  // list index
    std::uint32_t a = 0;     // polygon index in the older frame's list
    std::uint32_t b = 0;     // polygon index in the newer frame's list
};

struct MatchStats {
    std::uint32_t strips_a = 0, strips_b = 0, matched = 0;
    std::uint32_t vertices_b = 0, matched_vertices = 0;
};

MatchStats match_frames(const Frame& older, const Frame& newer, unsigned window,
                        std::vector<StripPair>& out);

struct BlendOptions {
    float t = 0.5f;               // 0 = older, 1 = newer
    unsigned window = 8;          // match lookahead
    float max_jump = 64.0f;       // a paired vertex moving further (guest px) is not blended
    float cut_median = 40.0f;     // median movement above this: a camera cut, no blending at all
};

struct BlendStats {
    MatchStats match;
    std::uint32_t blended_strips = 0, jumped_strips = 0;
    float median_motion = 0.0f;
    bool cut = false;
};

// `out` becomes `newer` with every paired, plausibly moving strip's positions (x, y and 1/w)
// blended towards `older`. Texture coordinates and colours come from `newer`. Returns false (and
// leaves `out` equal to `newer`) on a camera cut. `pairs` is scratch storage, kept by the caller.
bool blend_frames(const Frame& older, const Frame& newer, const BlendOptions& opt,
                  std::vector<StripPair>& pairs, Frame& out, BlendStats& stats);

}  // namespace dream::render
