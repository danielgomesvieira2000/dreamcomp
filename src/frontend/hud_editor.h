// The in-game HUD editor (F1; docs/HUD.md): outlines every HUD element over the running game,
// coloured by how it is anchored -- left, centre, right, or stretched -- with the 2D pieces the
// correction leaves alone in grey. Left click cycles an element's anchor, right click returns it to
// automatic, a click on a grey piece adds it to the HUD; S (or Save) writes the player's overrides.
// Runs on the overlay's worker thread like MenuUi; edits and saving are posted to the game thread.
#pragma once

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <string>

#include "dreamcomp/frontend.h"
#include "dreamcomp/hud.h"

namespace dreamcomp::frontend {

class HudEditor final : public Rml::EventListener {
public:
    using Post = std::function<void(std::function<void()>)>;  // runs on the game thread
    HudEditor(const OverlayContext& ctx, Post post, std::function<void()> close);
    ~HudEditor() override;

    bool load(Rml::Context* ctx, std::string* error);
    void show();
    void hide();
    bool visible() const noexcept { return visible_; }

    // The picture's place in the context, in context pixels: the whole window (the GPU path, which
    // letterboxes by the snapshot's aspect) or the frame itself (screenshots).
    void set_window(float w, float h);
    void set_frame(float x, float y, float w, float h);

    using MouseMap = std::function<bool(float wx, float wy, float& cx, float& cy)>;
    bool handle(const SDL_Event& ev, const MouseMap& map);  // true: it used the event
    void tick();  // pulls a fresh snapshot now and then
    bool take_redraw() {
        const bool r = redraw_;
        redraw_ = false;
        return r;
    }

    void ProcessEvent(Rml::Event& ev) override;
    Rml::Element* element(const std::string& id) const { return doc_ ? doc_->GetElementById(id) : nullptr; }

private:
    void rebuild();
    void image_rect(float& x, float& y, float& w, float& h) const;

    OverlayContext ctx_;
    Post post_;
    std::function<void()> close_;
    Rml::Context* rml_ = nullptr;
    Rml::ElementDocument* doc_ = nullptr;
    bool visible_ = false, redraw_ = false;
    hud::Snapshot snap_;
    bool have_snap_ = false;
    std::uint64_t last_pull_ms_ = 0;
    float win_w_ = 0, win_h_ = 0;
    bool frame_mode_ = false;
    float fx_ = 0, fy_ = 0, fw_ = 0, fh_ = 0;
    int corner_ = 2;  // the panel: 0 top left, 1 top right, 2 bottom left, 3 bottom right
    void place_panel();
    // The panel dragged with the mouse (by any part but its buttons): its top-left corner in
    // context pixels once moved (dragged_), the grab offset while dragging.
    bool dragging_ = false, dragged_ = false;
    float panel_x_ = 0, panel_y_ = 0, grab_x_ = 0, grab_y_ = 0, mouse_x_ = 0, mouse_y_ = 0;
    bool paused_ = false;
    void set_paused(bool on);
    void show_pause();
};

}  // namespace dreamcomp::frontend
