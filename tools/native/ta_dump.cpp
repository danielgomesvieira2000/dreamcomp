// ta_dump: print every polygon of a captured TA display list (an F11 / --capture-at .ta file).
//   ta_dump capture-000.ta [--list N] [--region x0,y0,x1,y1]
// One line per strip: list, index, vertex count, sprite?, PCW/ISP/TSP/TCW, screen bounds, z range.
// Used to find what distinguishes 2D HUD draws from 3D geometry (docs/HUD.md).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "dream/render/display_list.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: ta_dump FILE.ta [--list N] [--region x0,y0,x1,y1]\n");
        return 2;
    }
    int only_list = -1;
    float rx0 = -1e9f, ry0 = -1e9f, rx1 = 1e9f, ry1 = 1e9f;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--list") && i + 1 < argc)
            only_list = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--region") && i + 1 < argc)
            std::sscanf(argv[++i], "%f,%f,%f,%f", &rx0, &ry0, &rx1, &ry1);
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::uint32_t> words(raw.size() / 4);
    std::memcpy(words.data(), raw.data(), words.size() * 4);
    dream::render::DisplayList d;
    d.feed_stream(words.data(), words.size());
    const auto& f = d.frame();
    std::printf("vertices %zu strips %u sprites %u z [%g, %g]\n", f.vertices.size(), f.strips,
                f.sprites, f.min_z, f.max_z);
    for (std::size_t l = 0; l < f.lists.size(); ++l) {
        if (only_list >= 0 && static_cast<int>(l) != only_list)
            continue;
        for (std::size_t i = 0; i < f.lists[l].size(); ++i) {
            const auto& p = f.lists[l][i];
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
            for (std::uint32_t k = 0; k < p.count; ++k) {
                const auto& v = f.vertices[p.first + k];
                x0 = std::min(x0, v.x);
                x1 = std::max(x1, v.x);
                y0 = std::min(y0, v.y);
                y1 = std::max(y1, v.y);
                z0 = std::min(z0, v.z);
                z1 = std::max(z1, v.z);
            }
            if (x1 < rx0 || x0 > rx1 || y1 < ry0 || y0 > ry1)
                continue;
            std::printf("L%zu #%-4zu n=%-3u pcw=%08x isp=%08x tsp=%08x tcw=%08x  x[%7.1f,%7.1f] "
                        "y[%6.1f,%6.1f] z[%.6g,%.6g]\n",
                        l, i, p.count, p.pcw, p.isp, p.tsp, p.tcw, x0, x1, y0, y1, z0, z1);
        }
    }
    return 0;
}
