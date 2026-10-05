// A CPU RenderInterface for RmlUi: textured, vertex-coloured triangles with premultiplied alpha
// blending and a scissor rectangle, rasterised into an RGBA buffer (red in the low byte). Used for
// the in-game overlay, which is composited into the game's frame on the host before upload
// (engine Presenter::upload's `decorate`), so no second GPU pipeline is needed.
//
// No shaders, filters, masks or layers (RmlUi features this launcher's stylesheet does not use).
#pragma once

#include <RmlUi/Core/RenderInterface.h>

#include <cstdint>
#include <vector>

namespace dreamcomp::frontend {

class SoftRenderer final : public Rml::RenderInterface {
public:
    // Clears the target to transparent and makes it `w` x `h`. Pixels are premultiplied RGBA.
    void begin(std::uint32_t w, std::uint32_t h);
    const std::vector<std::uint32_t>& pixels() const noexcept { return target_; }
    std::uint32_t width() const noexcept { return w_; }
    std::uint32_t height() const noexcept { return h_; }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;

    void EnableScissorRegion(bool enable) override { scissor_on_ = enable; }
    void SetScissorRegion(Rml::Rectanglei region) override { scissor_ = region; }

private:
    struct Geometry {
        std::vector<Rml::Vertex> vertices;
        std::vector<int> indices;
    };
    struct Texture {
        int w = 0, h = 0;
        std::vector<std::uint32_t> px;  // premultiplied RGBA
    };
    void triangle(const Rml::Vertex& a, const Rml::Vertex& b, const Rml::Vertex& c,
                  Rml::Vector2f t, const Texture* tex);

    std::vector<std::uint32_t> target_;
    std::uint32_t w_ = 0, h_ = 0;
    bool scissor_on_ = false;
    Rml::Rectanglei scissor_;
};

// `overlay` (premultiplied, ow x oh) over `frame` (opaque RGBA, fw wide) at (x0, y0).
void composite(std::uint32_t* frame, std::uint32_t fw, std::uint32_t fh,
               const std::uint32_t* overlay, std::uint32_t ow, std::uint32_t oh, std::uint32_t x0,
               std::uint32_t y0);

}  // namespace dreamcomp::frontend
