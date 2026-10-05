// ta_match: how well do two consecutive display lists correspond, strip for strip?
//   ta_match A.ta B.ta [--window N]
// The feasibility measurement for geometry interpolation (docs/INTERPOLATION.md): per list,
// strips are aligned in submission order by their signature (PCW, ISP, TSP, TCW, vertex count),
// skipping at most N unmatched strips on either side. Reports matched strips and vertices and
// the displacement of matched vertices in pixels.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "dream/render/display_list.h"

#include "dreamcomp/geometry_match.h"

namespace {

bool load(const char* path, dream::render::DisplayList& d) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::uint32_t> words(raw.size() / 4);
    std::memcpy(words.data(), raw.data(), words.size() * 4);
    d.feed_stream(words.data(), words.size());
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: ta_match A.ta B.ta [--window N]\n");
        return 2;
    }
    unsigned window = 8;
    for (int i = 3; i < argc; ++i)
        if (!std::strcmp(argv[i], "--window") && i + 1 < argc)
            window = static_cast<unsigned>(std::atoi(argv[++i]));
    dream::render::DisplayList a, b;
    if (!load(argv[1], a) || !load(argv[2], b)) {
        std::fprintf(stderr, "cannot read input\n");
        return 2;
    }
    const auto& fa = a.frame();
    const auto& fb = b.frame();
    std::vector<dreamcomp::StripPair> pairs;
    const auto st = dreamcomp::match_frames(fa, fb, window, pairs);
    std::vector<float> disp;
    for (const auto& p : pairs) {
        const auto& pa = fa.lists[p.list][p.a];
        const auto& pb = fb.lists[p.list][p.b];
        for (std::uint32_t k = 0; k < pa.count; ++k) {
            const auto& va = fa.vertices[pa.first + k];
            const auto& vb = fb.vertices[pb.first + k];
            disp.push_back(std::hypot(vb.x - va.x, vb.y - va.y));
        }
    }
    std::sort(disp.begin(), disp.end());
    auto pct = [&](double q) { return disp.empty() ? 0.0 : disp[static_cast<std::size_t>(q * (disp.size() - 1))]; };
    std::printf("strips   A %u  B %u  matched %u (%.1f%% of B)\n", st.strips_a, st.strips_b,
                st.matched, st.strips_b ? 100.0 * st.matched / st.strips_b : 0.0);
    std::printf("vertices B %u  matched %u (%.1f%%)\n", st.vertices_b, st.matched_vertices,
                st.vertices_b ? 100.0 * st.matched_vertices / st.vertices_b : 0.0);
    std::printf("displacement px: median %.2f  p90 %.2f  p99 %.2f  max %.2f\n", pct(0.5), pct(0.9),
                pct(0.99), disp.empty() ? 0.0 : disp.back());
    return 0;
}
