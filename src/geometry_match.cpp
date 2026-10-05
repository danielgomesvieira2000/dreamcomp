#include "dreamcomp/geometry_match.h"

namespace dreamcomp {

namespace {

bool same(const dream::render::Polygon& a, const dream::render::Polygon& b) {
    return a.count == b.count && a.pcw == b.pcw && a.isp == b.isp && a.tsp == b.tsp &&
           a.tcw == b.tcw && a.tsp1 == b.tsp1 && a.tcw1 == b.tcw1;
}

}  // namespace

MatchStats match_frames(const dream::render::Frame& older, const dream::render::Frame& newer,
                        unsigned window, std::vector<StripPair>& out) {
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

}  // namespace dreamcomp
