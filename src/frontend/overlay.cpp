// The frontend menu over the running game (docs/FRONTEND.md): MenuUi in InGame mode, drawn by
// the CPU renderer (soft_render.h) into an RGBA buffer that is composited into each presented
// frame through the engine's overlay hooks (host_ext.h). Escape or a pad's Select/Back opens and
// closes it; the guest is paused while it is open.
//
// The menu is laid out in the part of the frame the window actually shows (the fit mode may crop
// it), at 1 dp = 1/720 of that height, so it looks the same as the pre-game window.
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "dream/render/png.h"
#include "dream/render/vk/present.h"
#include "dream/runtime/host_ext.h"
#include "dreamcomp/frontend.h"
#include "dreamcomp/settings.h"
#include "frontend_common.h"
#include "menu_ui.h"
#include "soft_render.h"

namespace dreamcomp::frontend {

namespace {

class OverlaySystem final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return static_cast<double>(SDL_GetTicksNS()) * 1e-9; }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type <= Rml::Log::LT_WARNING)
            std::fprintf(stderr, "overlay: %s\n", message.c_str());
        return true;
    }
};

std::vector<std::string> split(const char* s, char sep) {
    std::vector<std::string> out;
    if (!s)
        return out;
    std::string all = s;
    std::size_t at = 0;
    while (at < all.size()) {
        std::size_t e = all.find(sep, at);
        if (e == std::string::npos)
            e = all.size();
        if (e > at)
            out.push_back(all.substr(at, e - at));
        at = e + 1;
    }
    return out;
}

}  // namespace

struct Overlay::Impl {
    std::mutex mutex;  // on_event (event poll) vs draw (present) vs on_vblank (guest)
    OverlayContext ctx;
    bool open = false, failed = false, ready = false;
    std::unique_ptr<Utf8FileInterface> files;
    std::unique_ptr<OverlaySystem> system;
    std::unique_ptr<SoftRenderer> soft;
    std::unique_ptr<MenuUi> ui;
    Rml::Context* context = nullptr;

    // Where the menu sits: the visible part of the frame, and how the frame is drawn in the window.
    unsigned fw = 0, fh = 0, vis_x = 0, vis_y = 0, vis_w = 0, vis_h = 0;
    float draw_w = 0, draw_h = 0, draw_x = 0, draw_y = 0;  // the frame's rectangle, window pixels
    std::uint64_t last_render = 0;
    // DREAMCOMP_OVERLAY_PROFILE=1: time spent drawing, printed every 2 s while open.
    bool profile = std::getenv("DREAMCOMP_OVERLAY_PROFILE") != nullptr;
    std::uint64_t prof_mark = 0, prof_frames = 0, prof_renders = 0, prof_comp_ns = 0, prof_render_ns = 0;
    bool need_render = true;

    // Test script (DREAMCOMP_OVERLAY_KEYS="@FRAME,esc,shot:F.png,down,...").
    std::vector<std::string> script;
    std::uint64_t script_frame = 0, script_at = 0, script_done_at = 0;
    bool script_started = false;
    std::string pending_shot;

    explicit Impl(const OverlayContext& c) : ctx(c) {
        script = split(std::getenv("DREAMCOMP_OVERLAY_KEYS"), ',');
        if (!script.empty() && script.front().size() > 1 && script.front()[0] == '@') {
            script_frame = std::strtoull(script.front().c_str() + 1, nullptr, 10);
            script.erase(script.begin());
        }
    }

    ~Impl() {
        ui.reset();
        if (ready)
            Rml::Shutdown();
    }

    bool init() {
        if (ready)
            return true;
        if (failed)
            return false;
        files = std::make_unique<Utf8FileInterface>();
        system = std::make_unique<OverlaySystem>();
        soft = std::make_unique<SoftRenderer>();
        Rml::SetFileInterface(files.get());
        Rml::SetSystemInterface(system.get());
        Rml::SetRenderInterface(soft.get());
        Rml::Initialise();
        ready = true;
        std::string err;
        if (!load_fonts(ctx.exe_dir, &err)) {
            std::fprintf(stderr, "overlay: %s\n", err.c_str());
            failed = true;
            return false;
        }
        context = Rml::CreateContext("overlay", Rml::Vector2i(1280, 720));
        UiConfig cfg{ctx.settings, ctx.port, ctx.config, ctx.exe_dir, ctx.version};
        UiHost host;
        host.close_panel = [this] { close(); };
        host.exit_game = [] {
            std::printf("overlay: exit game\n");
            if (dream::host::host_controls().quit)
                dream::host::host_controls().quit();
        };
        host.open_bindings = [this] {
            std::printf("overlay: opening the binding screen\n");
            close();
            if (dream::host::host_controls().open_bindings)
                dream::host::host_controls().open_bindings();
        };
        host.applied = [this] {
            if (ctx.applied)
                ctx.applied();
        };
        ui = std::make_unique<MenuUi>(cfg, Mode::InGame, host);
        if (!context || !ui->load(context, &err)) {
            std::fprintf(stderr, "overlay: %s\n", err.c_str());
            failed = true;
            return false;
        }
        return true;
    }

    void open_menu() {
        if (!init()) {
            // No menu to show: Escape still has to end the game somehow.
            if (dream::host::host_controls().quit)
                dream::host::host_controls().quit();
            return;
        }
        open = true;
        need_render = true;
        ui->refresh_pads();
        ui->open_panel();  // the last tab used
        std::printf("overlay: open\n");
    }

    void close() {
        if (!open)
            return;
        open = false;
        std::printf("overlay: closed\n");
    }

    bool map(float wx, float wy, unsigned window_id, float& cx, float& cy) const {
        float d = 1.0f;
        if (SDL_Window* w = SDL_GetWindowFromID(window_id))
            d = SDL_GetWindowPixelDensity(w);
        const float px = wx * d, py = wy * d;
        if (draw_w <= 0 || draw_h <= 0 || fw == 0)
            return false;
        const float fx = (px - draw_x) / draw_w * static_cast<float>(fw);
        const float fy = (py - draw_y) / draw_h * static_cast<float>(fh);
        cx = fx - static_cast<float>(vis_x);
        cy = fy - static_cast<float>(vis_y);
        return true;
    }

    bool on_event(const SDL_Event& ev) {
        if (!open) {
            const bool esc = ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE &&
                             !ev.key.repeat;
            const bool select = ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                                ev.gbutton.button == SDL_GAMEPAD_BUTTON_BACK;
            if (esc || select) {
                std::printf("overlay: opened by %s\n", esc ? "Escape" : "a pad's Select/Back");
                open_menu();
                return true;
            }
            return false;
        }
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_F1)
            return false;  // the engine's binding screen still answers F1
        const unsigned wid = ev.type == SDL_EVENT_MOUSE_MOTION ? ev.motion.windowID : 0;
        const bool used = ui->handle(ev, [this, wid](float x, float y, float& cx, float& cy) {
            return map(x, y, wid, cx, cy);
        });
        if (used)
            need_render = true;
        return used;
    }

    // The part of the frame the window shows, as the presenter fits it (present.cpp).
    void layout(unsigned w, unsigned h, unsigned vw, unsigned vh) {
        const auto& o = dream::render::vk::present_options();
        const float ia = o.display_aspect > 0.0f ? o.display_aspect
                                                 : static_cast<float>(w) / static_cast<float>(h);
        const float ta = static_cast<float>(vw) / static_cast<float>(std::max(1u, vh));
        float s0 = 1.0f, s1 = 1.0f;
        using Fit = dream::render::vk::PresentOptions::Fit;
        if (o.fit == Fit::Letterbox) {
            if (ta > ia)
                s0 = ia / ta;
            else
                s1 = ta / ia;
        } else if (o.fit == Fit::Crop) {
            if (ta > ia)
                s1 = ta / ia;
            else
                s0 = ia / ta;
        }
        draw_w = static_cast<float>(vw) * s0;
        draw_h = static_cast<float>(vh) * s1;
        draw_x = (static_cast<float>(vw) - draw_w) * 0.5f;
        draw_y = (static_cast<float>(vh) - draw_h) * 0.5f;
        const unsigned nw = std::min(w, static_cast<unsigned>(static_cast<float>(w) / s0 + 0.5f));
        const unsigned nh = std::min(h, static_cast<unsigned>(static_cast<float>(h) / s1 + 0.5f));
        if (nw != vis_w || nh != vis_h || w != fw || h != fh) {
            fw = w;
            fh = h;
            vis_w = std::max(1u, nw);
            vis_h = std::max(1u, nh);
            vis_x = (w - vis_w) / 2;
            vis_y = (h - vis_h) / 2;
            context->SetDimensions(Rml::Vector2i(static_cast<int>(vis_w), static_cast<int>(vis_h)));
            context->SetDensityIndependentPixelRatio(static_cast<float>(vis_h) / 720.0f);
            need_render = true;
        }
    }

    void draw(std::uint32_t* px, unsigned w, unsigned h, unsigned vw, unsigned vh) {
        if (!open || !ready || failed || !px || w == 0 || h == 0)
            return;
        layout(w, h, vw, vh);
        ui->tick();
        const std::uint64_t now = SDL_GetTicks();
        const std::uint64_t t0 = SDL_GetTicksNS();
        if (ui->take_redraw() || need_render || now - last_render > 1000) {
            context->Update();
            soft->begin(vis_w, vis_h);
            context->Render();
            need_render = false;
            last_render = now;
            ++prof_renders;
            prof_render_ns += SDL_GetTicksNS() - t0;
        }
        const std::uint64_t t1 = SDL_GetTicksNS();
        composite(px, w, h, soft->pixels().data(), soft->width(), soft->height(), vis_x, vis_y);
        prof_comp_ns += SDL_GetTicksNS() - t1;
        ++prof_frames;
        if (profile && now - prof_mark > 2000) {
            std::printf("overlay: %llu frames, composite %.2f ms/frame; %llu renders, %.2f ms each\n",
                        static_cast<unsigned long long>(prof_frames),
                        prof_comp_ns / 1e6 / std::max<std::uint64_t>(1, prof_frames),
                        static_cast<unsigned long long>(prof_renders),
                        prof_render_ns / 1e6 / std::max<std::uint64_t>(1, prof_renders));
            prof_mark = now;
            prof_frames = prof_renders = prof_comp_ns = prof_render_ns = 0;
        }
        if (!pending_shot.empty()) {
            if (dream::render::png::write_file(std::filesystem::u8path(pending_shot), px, w, h))
                std::printf("overlay: wrote %s (%ux%u)\n", pending_shot.c_str(), w, h);
            else
                std::fprintf(stderr, "overlay: cannot write %s\n", pending_shot.c_str());
            pending_shot.clear();
        }
    }

    const std::uint32_t* render_window(unsigned vw, unsigned vh, bool& changed) {
        changed = false;
        if (!open || !ready || failed || vw == 0 || vh == 0 || !pending_shot.empty())
            return nullptr;
        if (vw != vis_w || vh != vis_h || fw != vw || fh != vh || vis_x != 0 || vis_y != 0) {
            fw = vw;
            fh = vh;
            vis_w = vw;
            vis_h = vh;
            vis_x = vis_y = 0;
            context->SetDimensions(Rml::Vector2i(static_cast<int>(vw), static_cast<int>(vh)));
            context->SetDensityIndependentPixelRatio(static_cast<float>(vh) / 720.0f);
            need_render = true;
        }
        ui->tick();
        const std::uint64_t now = SDL_GetTicks();
        const std::uint64_t t0 = SDL_GetTicksNS();
        if (ui->take_redraw() || need_render || now - last_render > 1000) {
            context->Update();
            soft->begin(vw, vh);
            context->Render();
            need_render = false;
            last_render = now;
            changed = true;
            ++prof_renders;
            prof_render_ns += SDL_GetTicksNS() - t0;
        }
        ++prof_frames;
        return soft->pixels().data();
    }

    // One scripted input every 150 ms: keys and pad buttons go into SDL's queue, so they take the
    // same path as real ones (engine poll -> event filter -> on_event).
    void step_script() {
        if (!script_started)
            return;
        const std::uint64_t now = SDL_GetTicks();
        if (script.empty()) {
            if (open && script_done_at && now > script_done_at + 5000) {
                std::printf("overlay: script finished with the menu open; resuming\n");
                close();
            }
            return;
        }
        if (now < script_at || !pending_shot.empty())
            return;
        const std::string k = script.front();
        script.erase(script.begin());
        script_at = now + 150;
        if (script.empty())
            script_done_at = now;
        std::printf("overlay: script %s (screen %s, focus %s)\n", k.c_str(),
                    ui && open ? ui->screen_name().c_str() : "-",
                    ui && open ? ui->focus_id().c_str() : "-");
        if (k.rfind("shot:", 0) == 0) {
            pending_shot = k.substr(5);
            need_render = true;
            return;
        }
        if (k.rfind("click:", 0) == 0) {
            Rml::Element* el = ui ? ui->element(k.substr(6)) : nullptr;
            if (!el || draw_w <= 0)
                return;
            const Rml::Vector2f pos = el->GetAbsoluteOffset(Rml::BoxArea::Border);
            const Rml::Vector2f size = el->GetBox().GetSize(Rml::BoxArea::Border);
            const float fx = static_cast<float>(vis_x) + pos.x + size.x * 0.5f;
            const float fy = static_cast<float>(vis_y) + pos.y + size.y * 0.5f;
            SDL_Window* win = SDL_GetKeyboardFocus();
            if (!win) {
                int n = 0;
                SDL_Window** list = SDL_GetWindows(&n);
                if (list && n > 0)
                    win = list[0];
                SDL_free(list);
            }
            const float d = win ? std::max(0.1f, SDL_GetWindowPixelDensity(win)) : 1.0f;
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.windowID = win ? SDL_GetWindowID(win) : 0;
            e.motion.x = (draw_x + fx / static_cast<float>(fw) * draw_w) / d;
            e.motion.y = (draw_y + fy / static_cast<float>(fh) * draw_h) / d;
            SDL_PushEvent(&e);
            e = SDL_Event{};
            e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            e.button.button = SDL_BUTTON_LEFT;
            e.button.down = true;
            SDL_PushEvent(&e);
            e.type = SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.down = false;
            SDL_PushEvent(&e);
            return;
        }
        SDL_Event e;
        if (k == "wait")
            return;  // 150 ms of nothing
        if (!MenuUi::script_event(k, e)) {
            std::fprintf(stderr, "overlay: unknown script step %s\n", k.c_str());
            return;
        }
        SDL_PushEvent(&e);
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            e.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
            e.gbutton.down = false;
        } else {
            e.type = SDL_EVENT_KEY_UP;
            e.key.down = false;
        }
        SDL_PushEvent(&e);
    }

    void on_vblank(std::uint64_t frame) {
        if (!script_started && !script.empty() && frame >= script_frame) {
            script_started = true;
            script_at = SDL_GetTicks();
            std::printf("overlay: script starts at frame %llu\n",
                        static_cast<unsigned long long>(frame));
        }
        step_script();
    }
};

Overlay::Overlay(const OverlayContext& ctx) : impl_(new Impl(ctx)) {}
Overlay::~Overlay() { delete impl_; }

bool Overlay::on_event(const void* sdl_event) {
    if (!sdl_event)
        return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->on_event(*static_cast<const SDL_Event*>(sdl_event));
}
bool Overlay::is_open() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->open;
}
void Overlay::draw(std::uint32_t* rgba, unsigned w, unsigned h, unsigned view_w, unsigned view_h) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->draw(rgba, w, h, view_w, view_h);
}
const std::uint32_t* Overlay::render_window(unsigned view_w, unsigned view_h, bool& changed) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->render_window(view_w, view_h, changed);
}
void Overlay::on_vblank(std::uint64_t frame) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->on_vblank(frame);
}

}  // namespace dreamcomp::frontend
