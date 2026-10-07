// Unit checks for the HUD correction and its overrides (src/hud.cpp), on synthetic frames: no
// game, no window. Build: target dreamcomp_hud_tests.  Run: dreamcomp_hud_tests <scratch dir>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "dream/render/display_list.h"
#include "dreamcomp/hud.h"
#include "dreamcomp/sharp2d.h"

using namespace dreamcomp;

static int g_fail = 0;
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)

namespace {

// A sprite (PCW type 5) at depth z covering x0..x1, y0..y1, with texture word tcw.
void sprite(dream::render::Frame& f, float x0, float y0, float x1, float y1, float z, std::uint32_t tcw,
            std::uint32_t type = 5) {
    dream::render::Polygon p;
    p.first = static_cast<std::uint32_t>(f.vertices.size());
    p.count = 4;
    p.pcw = (type << 29) | 0x8u;  // textured: the texture word counts for overrides
    p.tcw = tcw;
    for (auto [x, y] : {std::pair{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}}) {
        dream::render::Vertex v;
        v.x = x;
        v.y = y;
        v.z = z;
        f.vertices.push_back(v);
    }
    f.lists[4].push_back(p);  // the translucent list, where HUDs usually are
}

float left_x(const dream::render::Frame& f, std::size_t prim) {
    const auto& p = f.lists[4][prim];
    float x = 1e30f;
    for (std::uint32_t i = 0; i < p.count; ++i) x = std::min(x, f.vertices[p.first + i].x);
    return x;
}

bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <scratch dir>\n", argv[0]);
        return 2;
    }
    const auto dir = std::filesystem::path(argv[1]) / "hud_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    PortInfo::HudRule rule;
    rule.enabled = true;
    const float aspect = 16.0f / 9.0f, k = (4.0f / 3.0f) / aspect;

    // Anchoring by thirds: left piece about x=0, centre about 320, right about 640. Two sprites
    // share each depth so the rule takes them (min_shared 2).
    {
        dream::render::Frame f;
        sprite(f, 20, 20, 120, 40, 0.15f, 1);   // left third
        sprite(f, 300, 20, 340, 40, 0.15f, 2);  // middle
        sprite(f, 520, 20, 620, 40, 0.15f, 3);  // right third
        hud::Overrides none;
        hud::Snapshot snap;
        hud::Stats st;
        hud::correct(f, rule, aspect, true, true, none, &snap, st);
        CHECK(st.corrected == 3);
        CHECK(near(left_x(f, 0), 20 * k));
        CHECK(near(left_x(f, 1), 320 + (300 - 320) * k));
        CHECK(near(left_x(f, 2), 640 + (520 - 640) * k));
        CHECK(snap.boxes.size() == 3);
        CHECK(snap.boxes[0].anchor == hud::Anchor::Left);
        CHECK(snap.boxes[1].anchor == hud::Anchor::Center);
        CHECK(snap.boxes[2].anchor == hud::Anchor::Right);
        CHECK(snap.boxes[0].hud && snap.boxes[0].override_index == -1);
    }
    // apply = false (a 4:3 picture): the frame is not touched, the snapshot still classifies.
    {
        dream::render::Frame f;
        sprite(f, 20, 20, 120, 40, 0.15f, 1);
        sprite(f, 520, 20, 620, 40, 0.15f, 3);
        hud::Overrides none;
        hud::Snapshot snap;
        hud::Stats st;
        hud::correct(f, rule, 4.0f / 3.0f, true, false, none, &snap, st);
        CHECK(st.corrected == 0);
        CHECK(left_x(f, 0) == 20.0f && left_x(f, 1) == 520.0f);
        CHECK(snap.boxes.size() == 2 && snap.boxes[1].anchor == hud::Anchor::Right);
    }
    // Overrides: force the left piece to the right edge, stretch the right one, add a lone
    // polygon the rule ignores (type 4 without rule.polygons).
    {
        dream::render::Frame f;
        sprite(f, 20, 20, 120, 40, 0.15f, 1);
        sprite(f, 520, 20, 620, 40, 0.15f, 3);
        sprite(f, 300, 400, 340, 420, 0.5f, 9, 4);
        hud::Overrides ov;
        hud::Override a;
        a.x0 = 18; a.y0 = 18; a.x1 = 122; a.y1 = 42; a.tcws = {1}; a.anchor = hud::Anchor::Right;
        hud::Override b;
        b.x0 = 500; b.y0 = 0; b.x1 = 640; b.y1 = 60; b.anchor = hud::Anchor::Stretch;  // any texture
        hud::Override c;
        c.x0 = 290; c.y0 = 390; c.x1 = 350; c.y1 = 430; c.tcws = {9}; c.anchor = hud::Anchor::Auto;
        ov.items = {a, b, c};
        hud::Snapshot snap;
        hud::Stats st;
        hud::correct(f, rule, aspect, true, true, ov, &snap, st);
        CHECK(near(left_x(f, 0), 640 + (20 - 640) * k));  // forced right
        CHECK(left_x(f, 1) == 520.0f);                     // stretched: untouched
        CHECK(near(left_x(f, 2), 320 + (300 - 320) * k));  // added, centre third
        CHECK(st.corrected == 2);
        int stretched = 0, forced = 0;
        for (const auto& bx : snap.boxes) {
            stretched += bx.anchor == hud::Anchor::Stretch && bx.hud && bx.override_index == 1;
            forced += bx.anchor == hud::Anchor::Right && bx.override_index == 0;
        }
        CHECK(stretched == 1 && forced == 1);
        // A texture that is not listed does not match.
        CHECK(ov.match(70, 30, 2) == -1);
        CHECK(ov.match(70, 30, 1) == 0);
        CHECK(ov.match(600, 30, 12345) == 1);  // empty tcw list: any
    }
    // Pieces the rule ignores show up for adding; a backdrop does not.
    {
        dream::render::Frame f;
        sprite(f, 100, 100, 140, 120, 0.3f, 5, 4);  // lone polygon: not HUD
        sprite(f, 0, 0, 640, 480, 0.9f, 6, 4);      // full screen: too big to offer
        hud::Overrides none;
        hud::Snapshot snap;
        hud::Stats st;
        hud::correct(f, rule, aspect, true, true, none, &snap, st);
        CHECK(st.corrected == 0);
        CHECK(snap.boxes.size() == 1 && !snap.boxes[0].hud && snap.boxes[0].tcws.size() == 1);
    }
    // Save and load round trip; a malformed line is an error, comments are fine.
    {
        hud::Overrides ov;
        hud::Override a;
        a.x0 = 1; a.y0 = 2; a.x1 = 3; a.y1 = 4; a.tcws = {0xABCDEF01u, 7}; a.anchor = hud::Anchor::Center;
        ov.items = {a};
        const auto file = dir / "hud_overrides.ini";
        std::string err;
        CHECK(ov.save(file, &err));
        hud::Overrides back;
        CHECK(back.load(file, &err));
        CHECK(back.items.size() == 1 && back.items[0].anchor == hud::Anchor::Center &&
              back.items[0].tcws.size() == 2 && back.items[0].tcws[0] == 0xABCDEF01u &&
              back.items[0].x1 == 3.0f);
        hud::Overrides missing;
        CHECK(missing.load(dir / "none.ini") && missing.items.empty());
        {
            FILE* fp = std::fopen((dir / "bad.ini").string().c_str(), "w");
            std::fputs("# comment\nrect=1,2,3,4 anchor=sideways\n", fp);
            std::fclose(fp);
        }
        hud::Overrides bad;
        CHECK(!bad.load(dir / "bad.ini", &err));
    }

    // Overrides and full-width pieces: a fade (640 wide) under a large override stays out of
    // the HUD unless the override says full=1.
    {
        hud::Overrides ov;
        hud::Override big;
        big.x0 = 0; big.y0 = 0; big.x1 = 640; big.y1 = 480; big.anchor = hud::Anchor::Center;
        ov.items = {big};
        CHECK(ov.match(320, 240, 0, /*wide=*/true) == -1);
        CHECK(ov.match(320, 240, 0, /*wide=*/false) == 0);
        ov.items[0].full = true;
        CHECK(ov.match(320, 240, 0, /*wide=*/true) == 0);
        const auto file = dir / "full.ini";
        std::string err;
        CHECK(ov.save(file, &err));
        hud::Overrides back;
        CHECK(back.load(file, &err) && back.items.size() == 1 && back.items[0].full);
    }

    // An untextured piece matches an override saved with 0, whatever its TCW slot holds (it
    // changes every frame in Jet Grind Radio).
    {
        hud::Overrides ov;
        hud::Override o;
        o.x0 = 430; o.y0 = 320; o.x1 = 460; o.y1 = 360; o.tcws = {0}; o.anchor = hud::Anchor::Center;
        ov.items = {o};
        for (std::uint32_t junk : {0x3cbaef63u, 0x3cbae343u}) {
            dream::render::Frame f;
            sprite(f, 440, 334, 452, 354, 0.5f, junk, 4);
            f.lists[4][0].pcw &= ~0x8u;  // untextured
            hud::Snapshot snap;
            hud::Stats st;
            hud::correct(f, rule, aspect, true, true, ov, &snap, st);
            CHECK(snap.pieces.size() == 1 && snap.pieces[0].override_index == 0);
        }
    }

    // A word saved from an untextured piece (float data, pixel format 7) loads as 0.
    {
        const auto file = dir / "floatword.ini";
        FILE* fp = std::fopen(file.string().c_str(), "w");
        std::fputs("rect=1,2,3,4 anchor=center tcw=0x3f800000,0x3cbaef63,0x500e79a4\n", fp);
        std::fclose(fp);
        hud::Overrides ov;
        std::string err;
        CHECK(ov.load(file, &err) && ov.items.size() == 1);
        CHECK(ov.items[0].tcws.size() == 2 && ov.items[0].tcws[0] == 0u && ov.items[0].tcws[1] == 0x500e79a4u);
    }

    // Sharp 2D: a 12x24 glyph quad mapping 12x24 texels of a 512x512 texture, bilinear.
    {
        auto quad = [](dream::render::Frame& f, float x0, float y0, float x1, float y1, float u0,
                       float v0, float u1, float v1, float z1) {
            dream::render::Polygon p;
            p.first = static_cast<std::uint32_t>(f.vertices.size());
            p.count = 4;
            p.pcw = 0x8u;                          // textured
            p.tsp = (1u << 13) | (6u << 3) | 6u;   // bilinear, 512x512
            p.tcw = 0x500E79A4u;
            const float xs[4] = {x0, x0, x1, x1}, ys[4] = {y0, y1, y0, y1};
            const float us[4] = {u0, u0, u1, u1}, vs[4] = {v0, v1, v0, v1};
            for (int i = 0; i < 4; ++i) {
                dream::render::Vertex v;
                v.x = xs[i];
                v.y = ys[i];
                v.z = i == 3 ? z1 : 5.0f;
                v.u = us[i];
                v.v = vs[i];
                f.vertices.push_back(v);
            }
            f.lists[2].push_back(p);
        };
        dream::render::Frame f;
        quad(f, 170, 394, 182, 418, 324 / 512.f, 48 / 512.f, 336 / 512.f, 72 / 512.f, 5.0f);  // 1:1
        quad(f, 170, 394, 194, 418, 324 / 512.f, 48 / 512.f, 336 / 512.f, 72 / 512.f, 5.0f);  // 2:1
        quad(f, 170.5f, 394, 182.5f, 418, 324 / 512.f, 48 / 512.f, 336 / 512.f, 72 / 512.f, 5.0f);
        quad(f, 170, 394, 182, 418, 324.5f / 512, 48 / 512.f, 336.5f / 512, 72 / 512.f, 5.0f);
        quad(f, 170, 394, 182, 418, 324 / 512.f, 48 / 512.f, 336 / 512.f, 72 / 512.f, 4.0f);  // not flat
        quad(f, 170, 394, 182, 418, 336 / 512.f, 48 / 512.f, 324 / 512.f, 72 / 512.f, 5.0f);  // u flipped
        CHECK(sharpen_2d(f) == 2);
        CHECK(((f.lists[2][0].tsp >> 13) & 3u) == 0);
        CHECK(((f.lists[2][1].tsp >> 13) & 3u) == 1);
        CHECK(((f.lists[2][2].tsp >> 13) & 3u) == 1);
        CHECK(((f.lists[2][3].tsp >> 13) & 3u) == 1);
        CHECK(((f.lists[2][4].tsp >> 13) & 3u) == 1);
        CHECK(((f.lists[2][5].tsp >> 13) & 3u) == 0);
    }

    if (g_fail)
        std::fprintf(stderr, "%d check(s) failed\n", g_fail);
    else
        std::printf("hud tests: all passed\n");
    return g_fail ? 1 : 0;
}
