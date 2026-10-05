// Strip correspondence between two consecutive display lists (dreamcomp).
//
// Dreamcast games transform vertices on the CPU, so a display list holds screen-space geometry and
// no matrices. To draw a frame between two game frames, each strip of the newer frame is paired
// with the same strip of the older one and their vertices are blended. Strips carry no identity;
// pairing relies on games emitting their scene in a stable order, which docs/INTERPOLATION.md
// measures per game. A strip pairs with one of the same signature -- the parameter words
// (PCW/ISP/TSP/TCW) and vertex count -- in submission order, skipping at most `window` unmatched
// strips on either side (objects appearing, disappearing, or clipped differently).
#pragma once

#include <cstdint>
#include <vector>

#include "dream/render/display_list.h"

namespace dreamcomp {

struct StripPair {
    std::uint32_t list = 0;  // list index (opaque, modifier, translucent, ...)
    std::uint32_t a = 0;     // polygon index in the older frame's list
    std::uint32_t b = 0;     // polygon index in the newer frame's list
};

struct MatchStats {
    std::uint32_t strips_a = 0, strips_b = 0, matched = 0;
    std::uint32_t vertices_b = 0, matched_vertices = 0;
};

MatchStats match_frames(const dream::render::Frame& older, const dream::render::Frame& newer,
                        unsigned window, std::vector<StripPair>& out);

}  // namespace dreamcomp
