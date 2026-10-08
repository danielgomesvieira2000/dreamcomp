// The pre-game frontend window (docs/FRONTEND.md): SDL3 window + SDL_Renderer showing MenuUi
// (menu_ui.cpp) in Pregame mode. Also the screenshot mode (--launcher-screenshot) and the
// scripted-input test aid (DREAMCOMP_LAUNCHER_KEYS).
#include "dreamcomp/frontend.h"

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "RmlUi_Platform_SDL.h"
#include "RmlUi_Renderer_SDL.h"
#include "dream/render/png.h"
#include "dreamcomp/port.h"
#include "dreamcomp/settings.h"
#include "frontend_common.h"
#include "menu_ui.h"

namespace dreamcomp::frontend {

namespace {

class SystemInterface final : public SystemInterface_SDL {
public:
    using SystemInterface_SDL::SystemInterface_SDL;
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type <= Rml::Log::LT_WARNING)
            std::fprintf(stderr, "launcher: %s\n", message.c_str());
        return true;
    }
};

// SDL may call the dialog callback on another thread; the main loop picks the result up.
struct DialogResult {
    std::mutex m;
    bool ready = false;
    std::string path;  // empty: cancelled
};

void SDLCALL dialog_done(void* user, const char* const* files, int /*filter*/) {
    auto* r = static_cast<DialogResult*>(user);
    std::lock_guard<std::mutex> lock(r->m);
    r->ready = true;
    r->path = (files && files[0]) ? files[0] : "";
}

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

class Launcher {
public:
    explicit Launcher(const LaunchContext& ctx) : ctx_(ctx) {}

    bool open(std::string* error, bool hidden) {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            *error = std::string("SDL video: ") + SDL_GetError();
            return false;
        }
        video_ = true;
        if (SDL_InitSubSystem(SDL_INIT_GAMEPAD))
            gamepad_ = true;
        const char* t = ctx_.port && ctx_.port->title ? ctx_.port->title : "dreamcomp";
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (hidden)
            flags = SDL_WINDOW_HIDDEN;
        window_ = SDL_CreateWindow(t, ctx_.width, ctx_.height, flags);
        if (!window_) {
            *error = std::string("SDL window: ") + SDL_GetError();
            return false;
        }
        apply_port_icon(window_);
        SDL_SetWindowMinimumSize(window_, 640, 360);
        renderer_ = SDL_CreateRenderer(window_, nullptr);
        if (!renderer_) {
            *error = std::string("SDL renderer: ") + SDL_GetError();
            return false;
        }
        SDL_SetRenderVSync(renderer_, 1);

        files_ = std::make_unique<Utf8FileInterface>();
        system_ = std::make_unique<SystemInterface>(window_);
        render_ = std::make_unique<RenderInterface_SDL>(renderer_);
        Rml::SetFileInterface(files_.get());
        Rml::SetSystemInterface(system_.get());
        Rml::SetRenderInterface(render_.get());
        Rml::Initialise();
        rml_ = true;
        if (!load_fonts(ctx_.exe_dir, error))
            return false;

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        if (hidden)
            w = ctx_.width, h = ctx_.height;
        context_ = Rml::CreateContext("launcher", Rml::Vector2i(w, h));
        if (!context_) {
            *error = "RmlUi context";
            return false;
        }
        context_->SetDensityIndependentPixelRatio(static_cast<float>(h) / 720.0f);

        UiConfig cfg{ctx_.settings, ctx_.port, ctx_.config, ctx_.exe_dir, ctx_.version};
        UiHost host;
        host.pick_disc = [this] { pick_disc(); };
        host.start = [this] {
            std::printf("launcher: start game\n");
            outcome_ = Outcome::Start;
            done_ = true;
        };
        host.exit_game = [this] {
            outcome_ = Outcome::Quit;
            done_ = true;
        };
        ui_ = std::make_unique<MenuUi>(cfg, Mode::Pregame, host);
        if (!ui_->load(context_, error))
            return false;
        ui_->show(ctx_.tab);
        return true;
    }

    void close() {
        ui_.reset();
        if (rml_) {
            // RmlUi releases its textures through the render interface: renderer still alive.
            Rml::Shutdown();
            rml_ = false;
        }
        render_.reset();
        system_.reset();
        files_.reset();
        for (SDL_Gamepad* g : open_pads_) SDL_CloseGamepad(g);
        open_pads_.clear();
        if (renderer_)
            SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
        if (window_)
            SDL_DestroyWindow(window_);
        window_ = nullptr;
        if (gamepad_)
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        if (video_)
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        gamepad_ = video_ = false;
    }

    void pick_disc() {
        static const SDL_DialogFileFilter filters[] = {
            {"Dreamcast disc images (*.cue, *.gdi, *.chd)", "cue;gdi;chd"}, {"All files", "*"}};
        std::string start;
        const std::string cur = ctx_.settings->get("disc");
        if (!cur.empty())
            start = u8_path(std::filesystem::u8path(cur).parent_path());
        SDL_ShowOpenFileDialog(dialog_done, &dialog_, window_, filters, 2,
                               start.empty() ? nullptr : start.c_str(), false);
    }

    bool map_mouse(float wx, float wy, float& cx, float& cy) const {
        const float d = SDL_GetWindowPixelDensity(window_);
        cx = wx * d;
        cy = wy * d;
        return true;
    }

    void handle(SDL_Event& ev) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            outcome_ = Outcome::Quit;
            done_ = true;
            return;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            context_->SetDimensions(Rml::Vector2i(ev.window.data1, ev.window.data2));
            context_->SetDensityIndependentPixelRatio(static_cast<float>(ev.window.data2) / 720.0f);
            return;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (SDL_Gamepad* g = SDL_OpenGamepad(ev.gdevice.which))
                open_pads_.push_back(g);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            for (auto it = open_pads_.begin(); it != open_pads_.end(); ++it)
                if (SDL_GetGamepadID(*it) == ev.gdevice.which) {
                    SDL_CloseGamepad(*it);
                    open_pads_.erase(it);
                    break;
                }
            break;
        default: break;
        }
        ui_->handle(ev, [this](float x, float y, float& cx, float& cy) {
            return map_mouse(x, y, cx, cy);
        });
    }

    void draw() {
        context_->Update();
        render_->BeginFrame();
        context_->Render();
        render_->EndFrame();
    }

    // Test aid: DREAMCOMP_LAUNCHER_KEYS="down,enter,pad:a,click:<id>,shot:<png>,..." — one every
    // 150 ms through the same handlers as real input; quits 1.5 s after the last one.
    void scripted_input() {
        if (script_.empty() && script_at_ == 0)
            return;
        const std::uint64_t now = SDL_GetTicks();
        if (now < script_at_)
            return;
        if (script_.empty()) {
            std::printf("launcher: script finished without an outcome; quitting\n");
            outcome_ = Outcome::Quit;
            done_ = true;
            return;
        }
        const std::string k = script_.front();
        script_.erase(script_.begin());
        script_at_ = now + (script_.empty() ? 1500 : 150);
        if (k.rfind("shot:", 0) == 0) {
            std::string err;
            if (!capture(std::filesystem::u8path(k.substr(5)), &err))
                std::fprintf(stderr, "launcher: %s\n", err.c_str());
        } else if (k.rfind("click:", 0) == 0) {
            if (Rml::Element* el = ui_->element(k.substr(6))) {
                const Rml::Vector2f pos = el->GetAbsoluteOffset(Rml::BoxArea::Border);
                const Rml::Vector2f size = el->GetBox().GetSize(Rml::BoxArea::Border);
                const float d = std::max(0.1f, SDL_GetWindowPixelDensity(window_));
                SDL_Event e{};
                e.type = SDL_EVENT_MOUSE_MOTION;
                e.motion.x = (pos.x + size.x * 0.5f) / d;
                e.motion.y = (pos.y + size.y * 0.5f) / d;
                handle(e);
                e = SDL_Event{};
                e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                e.button.button = SDL_BUTTON_LEFT;
                e.button.down = true;
                handle(e);
                e.type = SDL_EVENT_MOUSE_BUTTON_UP;
                e.button.down = false;
                handle(e);
            }
        } else {
            SDL_Event e;
            if (MenuUi::script_event(k, e)) {
                handle(e);
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                    e.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
                    e.gbutton.down = false;
                } else {
                    e.type = SDL_EVENT_KEY_UP;
                    e.key.down = false;
                }
                handle(e);
            }
        }
        ui_->tick();
        std::printf("launcher: key %-12s -> %s, focus %s\n", k.c_str(), ui_->screen_name().c_str(),
                    ui_->focus_id().c_str());
    }

    Outcome run() {
        script_ = split(std::getenv("DREAMCOMP_LAUNCHER_KEYS"), ',');
        if (!script_.empty())
            script_at_ = SDL_GetTicks() + 700;
        while (!done_) {
            SDL_Event ev;
            if (SDL_WaitEventTimeout(&ev, 16)) {
                handle(ev);
                while (!done_ && SDL_PollEvent(&ev)) handle(ev);
            }
            std::string picked;
            bool got = false;
            {
                std::lock_guard<std::mutex> lock(dialog_.m);
                if (dialog_.ready) {
                    got = true;
                    picked = dialog_.path;
                    dialog_.ready = false;
                }
            }
            if (got)
                ui_->disc_picked(picked);
            ui_->tick();
            if (!done_ && !(ui_->dirty()))
                scripted_input();
            draw();
            SDL_RenderPresent(renderer_);
        }
        return outcome_;
    }

    bool capture(const std::filesystem::path& path, std::string* error) {
        const Rml::Vector2i dim = context_->GetDimensions();
        SDL_Texture* target = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888,
                                                SDL_TEXTUREACCESS_TARGET, dim.x, dim.y);
        if (!target) {
            *error = std::string("render target: ") + SDL_GetError();
            return false;
        }
        SDL_SetRenderTarget(renderer_, target);
        for (int pass = 0; pass < 3; ++pass) draw();
        SDL_Surface* shot = SDL_RenderReadPixels(renderer_, nullptr);
        SDL_SetRenderTarget(renderer_, nullptr);
        SDL_DestroyTexture(target);
        if (!shot) {
            *error = std::string("read pixels: ") + SDL_GetError();
            return false;
        }
        SDL_Surface* rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_ABGR8888);
        SDL_DestroySurface(shot);
        if (!rgba) {
            *error = std::string("convert: ") + SDL_GetError();
            return false;
        }
        std::vector<std::uint32_t> px(static_cast<std::size_t>(rgba->w) * rgba->h);
        for (int y = 0; y < rgba->h; ++y)
            std::memcpy(px.data() + static_cast<std::size_t>(y) * rgba->w,
                        static_cast<const std::uint8_t*>(rgba->pixels) +
                            static_cast<std::size_t>(y) * rgba->pitch,
                        static_cast<std::size_t>(rgba->w) * 4);
        for (auto& v : px) v |= 0xFF000000u;
        const bool ok = dream::render::png::write_file(path, px.data(),
                                                       static_cast<std::uint32_t>(rgba->w),
                                                       static_cast<std::uint32_t>(rgba->h));
        SDL_DestroySurface(rgba);
        if (!ok) {
            *error = "cannot write " + path.string();
            return false;
        }
        std::printf("launcher: wrote %s\n", path.string().c_str());
        return true;
    }

    // Screenshot mode: one PNG per screen (or the one asked for), no interaction.
    bool screenshots(std::string* error) {
        // Test aids: DREAMCOMP_LAUNCHER_DRAFT="scale=4;mods=a,b" (unapplied values),
        // DREAMCOMP_LAUNCHER_PADS="Name;Name" (stand-in pads), DREAMCOMP_LAUNCHER_FOCUS=<id>.
        for (const auto& kv : split(std::getenv("DREAMCOMP_LAUNCHER_DRAFT"), ';')) {
            const auto eq = kv.find('=');
            if (eq != std::string::npos)
                ui_->draft_set(kv.substr(0, eq), kv.substr(eq + 1));
        }
        if (const char* pads = std::getenv("DREAMCOMP_LAUNCHER_PADS"))
            ui_->set_pads(split(pads, ';'));
        ui_->verify_now();
        std::vector<std::string> screens;
        if (!ctx_.tab.empty())
            screens.push_back(ctx_.tab);
        else
            screens = MenuUi::all_screens();
        const char* focus_env = std::getenv("DREAMCOMP_LAUNCHER_FOCUS");
        for (const auto& sc : screens) {
            ui_->show(sc);
            ui_->tick();
            if (focus_env && *focus_env)
                if (auto* el = ui_->element(focus_env)) {
                    el->Focus(true);
                    context_->Update();
                    el->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
                }
            auto path = ctx_.screenshot;
            if (ctx_.tab.empty()) {
                std::string name = sc;
                std::replace(name.begin(), name.end(), '/', '-');
                path = path.parent_path() /
                       (path.stem().string() + "-" + name + path.extension().string());
            }
            if (!capture(path, error))
                return false;
        }
        return true;
    }

private:
    const LaunchContext& ctx_;
    std::unique_ptr<MenuUi> ui_;
    DialogResult dialog_;
    std::vector<SDL_Gamepad*> open_pads_;
    std::vector<std::string> script_;
    std::uint64_t script_at_ = 0;
    bool done_ = false;
    Outcome outcome_ = Outcome::Quit;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool video_ = false, gamepad_ = false, rml_ = false;
    std::unique_ptr<Utf8FileInterface> files_;
    std::unique_ptr<SystemInterface> system_;
    std::unique_ptr<RenderInterface_SDL> render_;
    Rml::Context* context_ = nullptr;

};

}  // namespace

Outcome run_launcher(const LaunchContext& ctx, std::string* error) {
    std::string err;
    if (!ctx.settings) {
        if (error)
            *error = "no settings";
        return Outcome::Error;
    }
    Outcome out = Outcome::Error;
    {
        Launcher l(ctx);
        const bool shot = !ctx.screenshot.empty();
        if (l.open(&err, shot)) {
            if (shot)
                out = l.screenshots(&err) ? Outcome::Quit : Outcome::Error;
            else
                out = l.run();
        }
        l.close();
    }
    if (out == Outcome::Error && error)
        *error = err;
    return out;
}

}  // namespace dreamcomp::frontend
