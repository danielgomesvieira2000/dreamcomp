#include "soft_render.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Vertex.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "dream/render/png.h"

namespace dreamcomp::frontend {

namespace {

inline std::uint32_t pack(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a) {
    return r | g << 8 | b << 16 | a << 24;
}

// src (premultiplied) over dst (premultiplied).
inline std::uint32_t over(std::uint32_t dst, std::uint32_t src) {
    const std::uint32_t sa = src >> 24;
    if (sa == 255)
        return src;
    if (sa == 0 && (src & 0xFFFFFFu) == 0)
        return dst;
    const std::uint32_t k = 255 - sa;
    std::uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        const std::uint32_t s = (src >> sh) & 0xFF, d = (dst >> sh) & 0xFF;
        out |= std::min<std::uint32_t>(255, s + (d * k + 127) / 255) << sh;
    }
    return out;
}

}  // namespace

void SoftRenderer::begin(std::uint32_t w, std::uint32_t h) {
    w_ = w;
    h_ = h;
    target_.assign(static_cast<std::size_t>(w) * h, 0u);
}

Rml::CompiledGeometryHandle SoftRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                          Rml::Span<const int> indices) {
    auto* g = new Geometry;
    g->vertices.assign(vertices.begin(), vertices.end());
    g->indices.assign(indices.begin(), indices.end());
    return reinterpret_cast<Rml::CompiledGeometryHandle>(g);
}

void SoftRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    delete reinterpret_cast<Geometry*>(geometry);
}

void SoftRenderer::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                                  Rml::TextureHandle texture) {
    const auto* g = reinterpret_cast<const Geometry*>(geometry);
    const auto* tex = reinterpret_cast<const Texture*>(texture);
    if (!g || target_.empty())
        return;
    for (std::size_t i = 0; i + 2 < g->indices.size(); i += 3)
        triangle(g->vertices[g->indices[i]], g->vertices[g->indices[i + 1]],
                 g->vertices[g->indices[i + 2]], translation, tex);
}

void SoftRenderer::triangle(const Rml::Vertex& a, const Rml::Vertex& b, const Rml::Vertex& c,
                            Rml::Vector2f t, const Texture* tex) {
    const float ax = a.position.x + t.x, ay = a.position.y + t.y;
    const float bx = b.position.x + t.x, by = b.position.y + t.y;
    const float cx = c.position.x + t.x, cy = c.position.y + t.y;
    float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (std::fabs(area) < 1e-6f)
        return;
    int x0 = static_cast<int>(std::floor(std::min({ax, bx, cx})));
    int x1 = static_cast<int>(std::ceil(std::max({ax, bx, cx})));
    int y0 = static_cast<int>(std::floor(std::min({ay, by, cy})));
    int y1 = static_cast<int>(std::ceil(std::max({ay, by, cy})));
    int clip_x0 = 0, clip_y0 = 0, clip_x1 = static_cast<int>(w_), clip_y1 = static_cast<int>(h_);
    if (scissor_on_) {
        clip_x0 = std::max(clip_x0, scissor_.Left());
        clip_y0 = std::max(clip_y0, scissor_.Top());
        clip_x1 = std::min(clip_x1, scissor_.Right());
        clip_y1 = std::min(clip_y1, scissor_.Bottom());
    }
    x0 = std::max(x0, clip_x0);
    y0 = std::max(y0, clip_y0);
    x1 = std::min(x1, clip_x1);
    y1 = std::min(y1, clip_y1);
    if (x0 >= x1 || y0 >= y1)
        return;

    const float inv = 1.0f / area;
    const auto& ca = a.colour;
    const auto& cb = b.colour;
    const auto& cc = c.colour;
    const bool flat = ca == cb && cb == cc;
    const std::uint32_t flat_px = pack(ca.red, ca.green, ca.blue, ca.alpha);
    if (flat && !tex && ca.alpha == 0 && ca.red == 0 && ca.green == 0 && ca.blue == 0)
        return;

    // Flat colour, no texture (backgrounds, borders, bars: most of the area): fill spans. Each row's
    // span is where the row's centre line crosses the triangle.
    if (flat && !tex) {
        const float vx[3] = {ax, bx, cx}, vy[3] = {ay, by, cy};
        for (int y = y0; y < y1; ++y) {
            const float py = static_cast<float>(y) + 0.5f;
            float lo = 1e30f, hi = -1e30f;
            for (int e = 0; e < 3; ++e) {
                const float px0 = vx[e], py0 = vy[e], px1 = vx[(e + 1) % 3], py1 = vy[(e + 1) % 3];
                if ((py >= py0 && py < py1) || (py >= py1 && py < py0)) {
                    const float x = px0 + (py - py0) * (px1 - px0) / (py1 - py0);
                    lo = std::min(lo, x);
                    hi = std::max(hi, x);
                }
            }
            if (lo > hi)
                continue;
            const int sx0 = std::max(x0, static_cast<int>(std::ceil(lo - 0.5f)));
            const int sx1 = std::min(x1, static_cast<int>(std::ceil(hi - 0.5f)));
            std::uint32_t* row = target_.data() + static_cast<std::size_t>(y) * w_;
            if (ca.alpha == 255)
                std::fill(row + std::max(sx0, 0), row + std::max(sx1, std::max(sx0, 0)), flat_px);
            else
                for (int x = sx0; x < sx1; ++x) row[x] = over(row[x], flat_px);
        }
        return;
    }

    for (int y = y0; y < y1; ++y) {
        const float py = static_cast<float>(y) + 0.5f;
        std::uint32_t* row = target_.data() + static_cast<std::size_t>(y) * w_;
        for (int x = x0; x < x1; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            // Barycentric weights; a pixel centre exactly on a shared edge goes to one side only
            // (>= on one triangle, > on its neighbour would need a full top-left rule; sharing a
            // half-pixel there is invisible in UI geometry made of axis-aligned quads).
            const float w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) * inv;
            const float w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) * inv;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                continue;
            std::uint32_t r, g, bb, al;
            if (flat) {
                r = ca.red, g = ca.green, bb = ca.blue, al = ca.alpha;
            } else {
                r = static_cast<std::uint32_t>(w0 * ca.red + w1 * cb.red + w2 * cc.red + 0.5f);
                g = static_cast<std::uint32_t>(w0 * ca.green + w1 * cb.green + w2 * cc.green + 0.5f);
                bb = static_cast<std::uint32_t>(w0 * ca.blue + w1 * cb.blue + w2 * cc.blue + 0.5f);
                al = static_cast<std::uint32_t>(w0 * ca.alpha + w1 * cb.alpha + w2 * cc.alpha + 0.5f);
            }
            std::uint32_t src;
            if (tex && tex->w > 0 && tex->h > 0) {
                const float u = w0 * a.tex_coord.x + w1 * b.tex_coord.x + w2 * c.tex_coord.x;
                const float v = w0 * a.tex_coord.y + w1 * b.tex_coord.y + w2 * c.tex_coord.y;
                const int tx = std::clamp(static_cast<int>(u * static_cast<float>(tex->w)), 0, tex->w - 1);
                const int ty = std::clamp(static_cast<int>(v * static_cast<float>(tex->h)), 0, tex->h - 1);
                const std::uint32_t texel = tex->px[static_cast<std::size_t>(ty) * tex->w + tx];
                const std::uint32_t tr = texel & 0xFF, tg = (texel >> 8) & 0xFF,
                                    tb = (texel >> 16) & 0xFF, ta = texel >> 24;
                src = pack((r * tr + 127) / 255, (g * tg + 127) / 255, (bb * tb + 127) / 255,
                           (al * ta + 127) / 255);
            } else {
                src = flat ? flat_px : pack(r, g, bb, al);
            }
            row[x] = over(row[x], src);
        }
    }
}

Rml::TextureHandle SoftRenderer::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) {
    Rml::FileInterface* fi = Rml::GetFileInterface();
    Rml::FileHandle f = fi->Open(source);
    if (!f)
        return {};
    fi->Seek(f, 0, SEEK_END);
    const size_t n = fi->Tell(f);
    fi->Seek(f, 0, SEEK_SET);
    std::vector<std::uint8_t> bytes(n);
    fi->Read(bytes.data(), n, f);
    fi->Close(f);
    dream::render::png::Image img;
    if (!dream::render::png::decode(bytes.data(), bytes.size(), img))
        return {};
    auto* t = new Texture;
    t->w = static_cast<int>(img.width);
    t->h = static_cast<int>(img.height);
    t->px = std::move(img.pixels);
    for (auto& p : t->px) {  // to premultiplied
        const std::uint32_t al = p >> 24;
        p = pack(((p & 0xFF) * al + 127) / 255, (((p >> 8) & 0xFF) * al + 127) / 255,
                 (((p >> 16) & 0xFF) * al + 127) / 255, al);
    }
    dimensions = {t->w, t->h};
    return reinterpret_cast<Rml::TextureHandle>(t);
}

Rml::TextureHandle SoftRenderer::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                 Rml::Vector2i dimensions) {
    // RmlUi hands generated textures (the font atlas) over already premultiplied, RGBA bytes.
    auto* t = new Texture;
    t->w = dimensions.x;
    t->h = dimensions.y;
    t->px.resize(static_cast<std::size_t>(t->w) * t->h);
    for (std::size_t i = 0; i < t->px.size(); ++i)
        t->px[i] = pack(source[i * 4], source[i * 4 + 1], source[i * 4 + 2], source[i * 4 + 3]);
    return reinterpret_cast<Rml::TextureHandle>(t);
}

void SoftRenderer::ReleaseTexture(Rml::TextureHandle texture) {
    delete reinterpret_cast<Texture*>(texture);
}

void composite(std::uint32_t* frame, std::uint32_t fw, std::uint32_t fh,
               const std::uint32_t* overlay, std::uint32_t ow, std::uint32_t oh, std::uint32_t x0,
               std::uint32_t y0) {
    for (std::uint32_t y = 0; y < oh && y0 + y < fh; ++y) {
        std::uint32_t* d = frame + static_cast<std::size_t>(y0 + y) * fw + x0;
        const std::uint32_t* s = overlay + static_cast<std::size_t>(y) * ow;
        const std::uint32_t n = std::min(ow, fw - x0);
        for (std::uint32_t x = 0; x < n; ++x) {
            const std::uint32_t o = s[x], a = o >> 24;
            if (a == 255) {
                d[x] = o;
                continue;
            }
            const std::uint32_t k = 255 - a, f = d[x];
            const std::uint32_t r = std::min<std::uint32_t>(255, (o & 0xFF) + ((f & 0xFF) * k + 127) / 255);
            const std::uint32_t g =
                std::min<std::uint32_t>(255, ((o >> 8) & 0xFF) + (((f >> 8) & 0xFF) * k + 127) / 255);
            const std::uint32_t b =
                std::min<std::uint32_t>(255, ((o >> 16) & 0xFF) + (((f >> 16) & 0xFF) * k + 127) / 255);
            d[x] = r | g << 8 | b << 16 | 0xFF000000u;
        }
    }
}

}  // namespace dreamcomp::frontend
