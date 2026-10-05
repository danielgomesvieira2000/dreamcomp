#include "dream/render/interpolate.h"

#include <algorithm>
#include <cmath>

namespace dream::render {

namespace {

bool same(const Polygon& a, const Polygon& b) {
    return a.count == b.count && a.pcw == b.pcw && a.isp == b.isp && a.tsp == b.tsp &&
           a.tcw == b.tcw && a.tsp1 == b.tsp1 && a.tcw1 == b.tcw1;
}

}  // namespace

MatchStats match_frames(const Frame& older, const Frame& newer, unsigned window,
                        std::vector<StripPair>& out) {
    out.clear();
    MatchStats st;
    for (std::uint32_t l = 0; l < newer.lists.size(); ++l) {
        const auto& A = older.lists[l];
        const auto& B = newer.lists[l];
        st.strips_a += static_cast<std::uint32_t>(A.size());
        st.strips_b += static_cast<std::uint32_t>(B.size());
        for (const auto& p : B) st.vertices_b += p.count;
        std::size_t i = 0, j = 0;
        while (i < A.size() && j < B.size()) {
            if (same(A[i], B[j])) {
                out.push_back({l, static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(j)});
                ++st.matched;
                st.matched_vertices += B[j].count;
                ++i;
                ++j;
                continue;
            }
            // The nearest resynchronisation: older has extra strips, or newer has new ones.
            std::size_t skip_a = 0, skip_b = 0;
            for (unsigned d = 1; d <= window; ++d) {
                if (i + d < A.size() && same(A[i + d], B[j])) {
                    skip_a = d;
                    break;
                }
                if (j + d < B.size() && same(A[i], B[j + d])) {
                    skip_b = d;
                    break;
                }
            }
            if (skip_a)
                i += skip_a;
            else if (skip_b)
                j += skip_b;
            else {
                ++i;
                ++j;
            }
        }
    }
    return st;
}

bool blend_frames(const Frame& older, const Frame& newer, const BlendOptions& opt,
                  std::vector<StripPair>& pairs, Frame& out, BlendStats& stats) {
    out = newer;
    stats = BlendStats{};
    stats.match = match_frames(older, newer, opt.window, pairs);
    // Camera cut test: the median movement of paired, on-screen first vertices.
    std::vector<float> motion;
    motion.reserve(pairs.size());
    for (const auto& p : pairs) {
        const Vertex& va = older.vertices[older.lists[p.list][p.a].first];
        const Vertex& vb = newer.vertices[newer.lists[p.list][p.b].first];
        if (vb.x >= 0.0f && vb.x <= 640.0f && vb.y >= 0.0f && vb.y <= 480.0f)
            motion.push_back(std::hypot(vb.x - va.x, vb.y - va.y));
    }
    if (!motion.empty()) {
        std::nth_element(motion.begin(), motion.begin() + motion.size() / 2, motion.end());
        stats.median_motion = motion[motion.size() / 2];
    }
    if (stats.median_motion > opt.cut_median) {
        stats.cut = true;
        return false;
    }
    const float t = opt.t;
    for (const auto& p : pairs) {
        const Polygon& pa = older.lists[p.list][p.a];
        const Polygon& pb = out.lists[p.list][p.b];
        bool ok = true;
        for (std::uint32_t k = 0; k < pb.count && ok; ++k) {
            const Vertex& va = older.vertices[pa.first + k];
            const Vertex& vb = out.vertices[pb.first + k];
            ok = std::isfinite(va.x) && std::isfinite(va.y) && std::isfinite(va.z) &&
                 std::isfinite(vb.x) && std::isfinite(vb.y) && std::isfinite(vb.z) &&
                 std::abs(vb.x - va.x) <= opt.max_jump && std::abs(vb.y - va.y) <= opt.max_jump;
        }
        if (!ok) {
            ++stats.jumped_strips;
            continue;
        }
        for (std::uint32_t k = 0; k < pb.count; ++k) {
            const Vertex& va = older.vertices[pa.first + k];
            Vertex& vb = out.vertices[pb.first + k];
            vb.x = va.x + (vb.x - va.x) * t;
            vb.y = va.y + (vb.y - va.y) * t;
            vb.z = va.z + (vb.z - va.z) * t;
        }
        ++stats.blended_strips;
    }
    // Blended depths lie between the two frames': keep the projection range covering both.
    out.min_z = std::min(older.min_z, newer.min_z);
    out.max_z = std::max(older.max_z, newer.max_z);
    return true;
}

}  // namespace dream::render
