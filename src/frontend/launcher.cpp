// The launcher window (docs/FRONTEND.md): SDL3 window + SDL_Renderer, RmlUi for layout and text.
//
// The page skeleton is frontend/launcher.rml + frontend/dreamcomp.rcss; the tab contents are
// generated here from the launcher's state (launcher_model.h) and rebuilt after every change, with
// the focused element restored by id. Every interactive element carries an `act` attribute that
// names what it does; one listener on the document handles clicks and keys for all of them.
//
// Input: mouse through RmlUi's SDL platform backend; keyboard arrows/Tab/Enter through RmlUi's
// own spatial navigation (`nav: auto`) and click emulation; gamepads are translated into the same
// key presses here (d-pad / left stick -> arrows with repeat, A -> Enter, B -> back, LB/RB ->
// tabs, Start -> Start Game).
#include "dreamcomp/frontend.h"

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "RmlUi_Platform_SDL.h"
#include "RmlUi_Renderer_SDL.h"
#include "dream/render/png.h"
#include "dream/translator/config/game_config.h"
#include "dreamcomp/port.h"
#include "dreamcomp/settings.h"
#include "launcher_model.h"

#ifdef _WIN32
#include <cwchar>
#endif

namespace dreamcomp::frontend {

namespace {

constexpr const char* kTabs[][2] = {{"play", "Play"},
                                    {"graphics", "Graphics"},
                                    {"controls", "Controls"},
                                    {"enhancements", "Enhancements"},
                                    {"about", "About"}};
constexpr int kTabCount = 5;

// PromptFont code points (PromptFont 1.10 "xbox" set and keyboard keys).
constexpr const char* kGlyphA = "\xE2\x87\x93";       // U+21D3
constexpr const char* kGlyphB = "\xE2\x87\x92";       // U+21D2
constexpr const char* kGlyphLB = "\xE2\x86\x98";      // U+2198
constexpr const char* kGlyphRB = "\xE2\x86\x99";      // U+2199
constexpr const char* kGlyphEnter = "\xE2\x90\xAE";   // U+242E
constexpr const char* kGlyphEsc = "\xE2\x90\xAF";     // U+242F

std::string esc(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

std::string id_safe(const std::string& s) {
    std::string out;
    for (unsigned char c : s) out += std::isalnum(c) ? static_cast<char>(c) : '_';
    return out;
}

std::string file_url(const std::filesystem::path& p) {
    const auto g = p.generic_u8string();
    std::string s(reinterpret_cast<const char*>(g.data()), g.size());
    std::string out = "file:///";
    if (!s.empty() && s[0] == '/')
        s.erase(0, 1);
    for (unsigned char c : s) {
        if (std::isalnum(c) || std::strchr("/-_.~:", c))
            out += static_cast<char>(c);
        else {
            char buf[4];
            std::snprintf(buf, sizeof buf, "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

std::string u8(const std::filesystem::path& p) {
    const auto s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

// RmlUi takes URLs: forward slashes, or "C:\..." reads as a malformed protocol.
std::string url_path(const std::filesystem::path& p) {
    const auto s = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

// ---- RmlUi interfaces -------------------------------------------------------------------------

// Paths are UTF-8; on Windows fopen would read them as the ANSI code page.
class FileInterface final : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override {
#ifdef _WIN32
        const std::filesystem::path p = std::filesystem::u8path(path);
        std::FILE* f = _wfopen(p.c_str(), L"rb");
#else
        std::FILE* f = std::fopen(path.c_str(), "rb");
#endif
        return reinterpret_cast<Rml::FileHandle>(f);
    }
    void Close(Rml::FileHandle f) override { std::fclose(reinterpret_cast<std::FILE*>(f)); }
    size_t Read(void* buf, size_t size, Rml::FileHandle f) override {
        return std::fread(buf, 1, size, reinterpret_cast<std::FILE*>(f));
    }
    bool Seek(Rml::FileHandle f, long offset, int origin) override {
        return std::fseek(reinterpret_cast<std::FILE*>(f), offset, origin) == 0;
    }
    size_t Tell(Rml::FileHandle f) override {
        return static_cast<size_t>(std::ftell(reinterpret_cast<std::FILE*>(f)));
    }
};

class SystemInterface final : public SystemInterface_SDL {
public:
    using SystemInterface_SDL::SystemInterface_SDL;
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type <= Rml::Log::LT_WARNING)
            std::fprintf(stderr, "launcher: %s\n", message.c_str());
        return true;
    }
};

// ---- file dialog hand-off -----------------------------------------------------------------------
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

// ---- the launcher -------------------------------------------------------------------------------

class Launcher final : public Rml::EventListener {
public:
    Launcher(const LaunchContext& ctx) : ctx_(ctx), draft_(*ctx.settings) {
        config_dir_ = ctx.settings->file().parent_path();
        if (!ctx.config.empty()) {
            std::string err;
            cfg_ok_ = dream::translator::load_game_config(ctx.config, cfg_, err);
            if (!cfg_ok_)
                cfg_error_ = err;
        } else {
            cfg_error_ = "the game's configuration file was not found next to the executable";
        }
        for (int i = 0; i < kTabCount; ++i)
            if (ctx.tab == kTabs[i][0])
                tab_ = i;
    }

    std::string title() const {
        if (ctx_.port && ctx_.port->title)
            return ctx_.port->title;
        if (cfg_ok_ && !cfg_.title.empty())
            return cfg_.title;
        return "dreamcomp";
    }

    // ---- setup / teardown ----
    bool open(std::string* error, bool hidden) {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            *error = std::string("SDL video: ") + SDL_GetError();
            return false;
        }
        video_ = true;
        if (SDL_InitSubSystem(SDL_INIT_GAMEPAD))
            gamepad_ = true;
        const std::string wtitle = title() + " \xE2\x80\x94 Launcher";
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (hidden)
            flags = SDL_WINDOW_HIDDEN;
        window_ = SDL_CreateWindow(wtitle.c_str(), ctx_.width, ctx_.height, flags);
        if (!window_) {
            *error = std::string("SDL window: ") + SDL_GetError();
            return false;
        }
        SDL_SetWindowMinimumSize(window_, 640, 360);
        renderer_ = SDL_CreateRenderer(window_, nullptr);
        if (!renderer_) {
            *error = std::string("SDL renderer: ") + SDL_GetError();
            return false;
        }
        SDL_SetRenderVSync(renderer_, 1);

        files_ = std::make_unique<FileInterface>();
        system_ = std::make_unique<SystemInterface>(window_);
        render_ = std::make_unique<RenderInterface_SDL>(renderer_);
        Rml::SetFileInterface(files_.get());
        Rml::SetSystemInterface(system_.get());
        Rml::SetRenderInterface(render_.get());
        Rml::Initialise();
        rml_ = true;

        const auto fe = ctx_.exe_dir / "frontend";
        const auto fonts = fe / "fonts";
        struct Face {
            const char* file;
            const char* family;
            Rml::Style::FontWeight weight;
        } faces[] = {{"Inter-Regular.ttf", "Inter", Rml::Style::FontWeight::Normal},
                     {"Inter-SemiBold.ttf", "Inter", Rml::Style::FontWeight(600)},
                     {"Inter-Bold.ttf", "Inter", Rml::Style::FontWeight::Bold},
                     {"promptfont.ttf", "promptfont", Rml::Style::FontWeight::Normal}};
        for (const auto& f : faces) {
            if (!Rml::LoadFontFace(url_path(fonts / f.file), f.family, Rml::Style::FontStyle::Normal,
                                   f.weight, false)) {
                *error = "cannot load font " + u8(fonts / f.file);
                return false;
            }
        }

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
        if (std::getenv("DREAMCOMP_LAUNCHER_KEYS")) {
            int ww = 0, wh = 0;
            SDL_GetWindowSize(window_, &ww, &wh);
            std::printf("launcher: window %dx%d, pixels %dx%d, density %.2f\n", ww, wh, w, h,
                        SDL_GetWindowPixelDensity(window_));
        }
        // The skeleton, plus a port's own stylesheet and art when it ships them (LAUNCHER_DIR in
        // dreamcomp_add_port): port.rcss is linked after dreamcomp.rcss so it overrides it.
        std::string rml;
        {
            std::ifstream in(fe / "launcher.rml", std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            rml = ss.str();
        }
        if (rml.empty()) {
            *error = "cannot read " + u8(fe / "launcher.rml");
            return false;
        }
        std::error_code ec;
        if (std::filesystem::exists(fe / "port.rcss", ec)) {
            const auto at = rml.find("</head>");
            if (at != std::string::npos)
                rml.insert(at, "<link type=\"text/rcss\" href=\"port.rcss\"/>\n");
        }
        // RmlUi parses a document's source as a URL, where "C:" would be a protocol; it spells
        // drive letters "C|" itself and turns them back when resolving relative links.
        std::string source = url_path(fe / "launcher.rml");
        std::replace(source.begin(), source.end(), ':', '|');
        doc_ = context_->LoadDocumentFromMemory(rml, source);
        if (!doc_) {
            *error = "cannot load " + u8(fe / "launcher.rml");
            return false;
        }
        if (std::filesystem::exists(fe / "art.png", ec))
            if (auto* art = doc_->GetElementById("art"))
                art->SetInnerRML("<img src=\"art.png\"/>");
        doc_->AddEventListener(Rml::EventId::Click, this);
        doc_->AddEventListener(Rml::EventId::Keydown, this);
        doc_->AddEventListener(Rml::EventId::Focus, this, true);
        doc_->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);

        if (auto* t = doc_->GetElementById("game-title"))
            t->SetInnerRML(esc(title()));
        std::string sub;
        if (cfg_ok_) {
            if (!cfg_.product.empty())
                sub += cfg_.product;
            if (!cfg_.region.empty())
                sub += (sub.empty() ? "" : "  \xC2\xB7  ") + cfg_.region;
        }
        sub += (sub.empty() ? "" : "  \xC2\xB7  ") + std::string("Dreamcast \xC2\xB7 native PC port");
        if (auto* s = doc_->GetElementById("game-sub"))
            s->SetInnerRML(esc(sub));
        refresh_pads();
        return true;
    }

    void close() {
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

    // ---- disc verification ----
    void start_verify(bool sync) {
        const std::string path = draft_.get("disc");
        if (path.empty()) {
            disc_ = DiscCheck{};
            verifying_ = false;
            return;
        }
        const std::string expected = cfg_ok_ ? cfg_.sha1_1st_read : std::string();
        const auto p = std::filesystem::u8path(path);
        if (sync) {
            disc_ = verify_disc(p, expected);
            verifying_ = false;
            return;
        }
        verifying_ = true;
        verify_ = std::async(std::launch::async, [p, expected] { return verify_disc(p, expected); });
    }

    void poll_background() {
        if (verifying_ && verify_.valid() &&
            verify_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            disc_ = verify_.get();
            verifying_ = false;
            dirty_ = true;
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
        if (got) {
            dialog_open_ = false;
            if (!picked.empty()) {
                draft_.set("disc", picked);
                start_verify(false);
                dirty_ = true;
            }
        }
    }

    bool playable() const { return !verifying_ && disc_playable(disc_) && cfg_ok_; }

    // ---- content ----
    std::string row_cycle(const std::string& key, const std::string& name, const std::string& help,
                          const std::string& value) const {
        std::string r = "<div id=\"r-" + key + "\" class=\"row focusable" +
                        (draft_.pending(key) ? " pending" : "") + "\" act=\"cycle:" + key +
                        "\"><div class=\"label\"><div class=\"name\">" + esc(name) + "</div>";
        if (!help.empty())
            r += "<div class=\"help\">" + help + "</div>";
        r += "</div><div class=\"value\"><span class=\"chev\" act=\"prev:" + key +
             "\">\xE2\x80\xB9</span><span class=\"val\">" + esc(value) +
             "</span><span class=\"chev\" act=\"next:" + key +
             "\">\xE2\x80\xBA</span></div><span class=\"pend\"></span></div>";
        return r;
    }

    std::string row_toggle(const std::string& key, const std::string& name, const std::string& help,
                           bool on) const {
        std::string r = "<div id=\"r-" + key + "\" class=\"row focusable" +
                        (draft_.pending(key) ? " pending" : "") + "\" act=\"toggle:" + key +
                        "\"><div class=\"label\"><div class=\"name\">" + esc(name) + "</div>";
        if (!help.empty())
            r += "<div class=\"help\">" + help + "</div>";
        r += std::string("</div><div class=\"value\"><span class=\"switch-label\">") +
             (on ? "On" : "Off") + "</span><span class=\"switch" + (on ? " on" : "") +
             "\"><span class=\"knob\"></span></span></div><span class=\"pend\"></span></div>";
        return r;
    }

    std::string row_button(const std::string& id, const std::string& act, const std::string& name,
                           const std::string& help, const std::string& button) const {
        std::string r = "<div id=\"" + id + "\" class=\"row focusable\" act=\"" + act +
                        "\"><div class=\"label\"><div class=\"name\">" + esc(name) + "</div>";
        if (!help.empty())
            r += "<div class=\"help\">" + help + "</div>";
        r += "</div><div class=\"value\"><span class=\"inline-button\">" + esc(button) +
             "</span></div><span class=\"pend\"></span></div>";
        return r;
    }

    std::string tab_play() const {
        std::string s = "<div class=\"section\">Disc</div>";
        std::string cls, mark, status, detail, extra;
        const std::string disc_path = draft_.get("disc");
        if (!cfg_ok_) {
            cls = "bad";
            mark = "\xE2\x9C\x97";
            status = "Game files incomplete";
            detail = "This build cannot start: " + cfg_error_ + ".";
        } else if (verifying_) {
            cls = "wait";
            status = "Checking disc\xE2\x80\xA6";
            detail = "Reading the boot file and comparing it with this build.";
        } else if (disc_path.empty() || disc_.status == DiscStatus::None) {
            status = "No disc selected";
            detail = "Choose an image of your own " + title() +
                     " disc (.cue, .gdi or .chd). The game's data is never included with this "
                     "port.";
        } else {
            switch (disc_.status) {
            case DiscStatus::Verified:
                cls = "ok";
                mark = "\xE2\x9C\x93";
                status = "Disc verified";
                detail = disc_.boot_name + " matches the release this port was built from" +
                         (cfg_.product.empty() ? std::string(".")
                                               : " (" + cfg_.product +
                                                     (cfg_.region.empty() ? "" : ", " + cfg_.region) +
                                                     ").");
                break;
            case DiscStatus::NoReference:
                cls = "ok";
                mark = "\xE2\x9C\x93";
                status = "Disc readable";
                detail = "This build has no reference checksum to compare against.";
                break;
            case DiscStatus::WrongRevision:
                cls = "bad";
                mark = "\xE2\x9C\x97";
                status = "Wrong release or revision";
                detail = "This disc is a different release or revision of the game. The port is "
                         "built for one exact version and cannot run another.";
                extra = "<div class=\"hashes\">disc&#160;&#160;" + esc(disc_.found_sha1) +
                        "<br/>expected&#160;&#160;" + esc(disc_.expected_sha1) + "</div>";
                break;
            default:
                cls = "bad";
                mark = "\xE2\x9C\x97";
                status = "Cannot read this image";
                detail = disc_.detail.empty() ? std::string("Not a readable disc image.")
                                              : disc_.detail;
                if (!detail.empty())
                    detail[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(detail[0])));
                if (detail.back() != '.')
                    detail += ".";
            }
        }
        s += "<div class=\"disc-card " + cls + "\"><div class=\"status\">";
        if (!mark.empty())
            s += "<span class=\"mark\">" + mark + "</span>";
        s += esc(status) + "</div><div class=\"status-detail\">" + esc(detail) + "</div>";
        if (!disc_path.empty())
            s += "<div class=\"path\">" + esc(disc_path) + "</div>";
        s += extra + "</div>";
        s += row_button("r-disc", "disc", "Select disc image\xE2\x80\xA6",
                        "A dump of your own disc: .cue, .gdi or .chd",
                        dialog_open_ ? "Waiting\xE2\x80\xA6" : "Browse");
        if (draft_.pending("disc"))
            s += "<div class=\"note\">The new disc is saved when you press Apply or Start "
                 "Game.</div>";
        s += "<div class=\"section spaced\">Memory card</div>";
        // Same default as the core's (dreamcomp's per-user directory, not --settings').
        const std::string vmu = draft_.get(
            "vmu", u8(default_config_dir(ctx_.port && ctx_.port->id ? ctx_.port->id : "dreamcomp") /
                      "vmu_a1.bin"));
        s += "<div class=\"row\"><div class=\"label\"><div class=\"name\">Saves</div><div "
             "class=\"help\">" +
             esc(vmu) + "</div></div></div>";
        if (cfg_ok_ && disc_path.empty())
            s += "<div class=\"note\">Start Game becomes available once a disc is verified.</div>";
        return s;
    }

    float draft_aspect() const {
        if (!ctx_.port || !ctx_.port->widescreen)
            return 4.0f / 3.0f;
        return std::min(parse_aspect(draft_.get("aspect", "16:9")), ctx_.port->max_aspect);
    }

    std::string tab_graphics() const {
        const bool ws = ctx_.port && ctx_.port->widescreen;
        const bool anam = ctx_.port && ctx_.port->widescreen_anamorphic;
        std::string s = "<div class=\"section\">Picture</div>";
        const int scale = std::clamp(draft_.get_int("scale", 2), 1, 8);
        s += row_cycle("scale", "Internal resolution",
                       "Draws the 3D scene at a multiple of the console's 640\xC3\x97" "480",
                       scale_label(scale, draft_aspect(), anam));
        if (ws) {
            const auto choices = aspect_choices(true, ctx_.port->max_aspect);
            const std::string v = draft_.get("aspect", "16:9");
            s += row_cycle("aspect", "Aspect ratio", "A wider view of the scene, not a stretch",
                           choices[choice_index(choices, v)].label);
        } else {
            s += "<div class=\"row dim\"><div class=\"label\"><div class=\"name\">Aspect "
                 "ratio</div><div class=\"help\">This game has no widescreen support "
                 "yet</div></div><div class=\"value\"><span class=\"val\">4:3</span></div></div>";
        }
        const auto fits = fit_choices();
        s += row_cycle("fit", "Fit to window",
                       "Crop fills the screen without bars; Letterbox shows the whole picture",
                       fits[choice_index(fits, draft_.get("fit", "crop"))].label);
        s += "<div class=\"section spaced\">Display</div>";
        const auto fps = fps_choices();
        s += row_cycle("fps", "Frame rate",
                       "Auto blends in-between frames on 120 Hz+ displays; the game still runs at 60",
                       fps[choice_index(fps, draft_.get("fps", "auto"))].label);
        s += row_toggle("fullscreen", "Fullscreen", "Alt+Enter also switches while playing",
                        draft_.get_bool("fullscreen", false));
        return s;
    }

    std::string tab_controls() const {
        std::string s = "<div class=\"section\">Feedback</div>";
        const int rumble = std::clamp(draft_.get_int("rumble", 100), 0, 100);
        s += "<div id=\"r-rumble\" class=\"row focusable" +
             std::string(draft_.pending("rumble") ? " pending" : "") +
             "\" act=\"slider:rumble\"><div class=\"label\"><div class=\"name\">Rumble "
             "strength</div><div class=\"help\">Vibration on controllers with motors; 0 % turns "
             "it off</div></div><div class=\"value\"><div class=\"slider\" "
             "id=\"rumble-slider\"><div class=\"track\"><div class=\"fill\" style=\"width: " +
             std::to_string(rumble) + "%;\"></div></div></div><span class=\"slider-val\">" +
             (rumble == 0 ? std::string("Off") : std::to_string(rumble) + " %") +
             "</span></div><span class=\"pend\"></span></div>";
        s += "<div class=\"note\">Button layout: press <span class=\"key\">F1</span> or a pad's "
             "<b>Select</b>/<b>Back</b> button in game to rebind.</div>";
        s += "<div class=\"section spaced\">Players</div>";
        s += "<div class=\"row\"><span class=\"pad-player\">P1</span><div class=\"label\"><div "
             "class=\"name\">Keyboard</div><div class=\"help\">Arrows, Z X A S, Return = "
             "Start</div></div></div>";
        if (pads_.empty()) {
            s += "<div class=\"note\">No controllers connected. Plug one in at any time; the "
                 "first one plays as P1 together with the keyboard.</div>";
        }
        for (std::size_t i = 0; i < pads_.size(); ++i) {
            const std::string player = i < 4 ? "P" + std::to_string(i + 1) : "\xE2\x80\x94";
            const std::string help = i == 0 ? "Plays together with the keyboard"
                                     : i < 4 ? "Player " + std::to_string(i + 1)
                                             : "Not used (four players at most)";
            s += "<div class=\"row\"><span class=\"pad-player\">" + player +
                 "</span><div class=\"label\"><div class=\"name\">" + esc(pads_[i]) +
                 "</div><div class=\"help\">" + esc(help) + "</div></div></div>";
        }
        return s;
    }

    std::string tab_enhancements() const {
        std::string s = "<div class=\"section\">Texture packs</div>";
        const std::string pack = draft_.get("texture_pack");
        const auto dir = texture_pack_dir(pack, config_dir_);
        s += row_toggle("texture_pack", "Texture pack",
                        "Replacement textures from " + esc(u8(dir)), texture_pack_on(pack));
        s += row_button("r-opentex", "open-textures", "Open texture folder",
                        "Put a pack's files here; created if missing", "Open");
        s += row_toggle("dump_textures", "Dump textures",
                        "For pack authors: writes every new texture to " +
                            esc(u8(config_dir_ / "texture_dump")),
                        draft_.get_bool("dump_textures", false));
        s += "<div class=\"section spaced\">Mods</div>";
        const auto mods_dir = config_dir_ / "mods";
        const auto mods = list_mods(mods_dir, draft_.get("mods"));
        s += "<div class=\"note\">Each folder in the mods folder mirrors the disc's files. "
             "Enabled mods load top to bottom; the first one providing a file wins.</div>";
        int order = 0;
        for (std::size_t i = 0; i < mods.size(); ++i) {
            const auto& m = mods[i];
            const std::string id = id_safe(m.name);
            const bool can_up = m.enabled && i > 0;
            const bool can_down = m.enabled && i + 1 < mods.size() && mods[i + 1].enabled;
            s += "<div class=\"mod-row\"><span class=\"mod-order\">" +
                 (m.enabled ? std::to_string(++order) : std::string()) + "</span>";
            s += "<div id=\"m-" + id + "\" class=\"row focusable" +
                 std::string(draft_.pending("mods") ? " pending" : "") + "\" act=\"mod:" +
                 esc(m.name) + "\"><div class=\"label\"><div class=\"name\">" + esc(m.name) +
                 "</div></div><div class=\"value\"><span class=\"switch-label\">" +
                 (m.enabled ? "On" : "Off") + "</span><span class=\"switch" +
                 (m.enabled ? " on" : "") + "\"><span class=\"knob\"></span></span></div></div>";
            s += "<div id=\"mu-" + id + "\" class=\"mini" +
                 std::string(can_up ? " focusable" : " off") + "\" act=\"" +
                 (can_up ? "modup:" + esc(m.name) : std::string()) + "\">\xE2\x96\xB2</div>";
            s += "<div id=\"md-" + id + "\" class=\"mini" +
                 std::string(can_down ? " focusable" : " off") + "\" act=\"" +
                 (can_down ? "moddown:" + esc(m.name) : std::string()) + "\">\xE2\x96\xBC</div>";
            s += "</div>";
        }
        if (mods.empty())
            s += "<div class=\"row\"><div class=\"label\"><div class=\"name\">No mods "
                 "installed</div><div class=\"help\">" +
                 esc(u8(mods_dir)) + "</div></div></div>";
        s += row_button("r-openmods", "open-mods", "Open mods folder",
                        "Add a mod as a folder here, then come back to this tab", "Open");
        return s;
    }

    std::string tab_about() const {
        std::string s = "<div class=\"about\">";
        s += "<div class=\"big\">" + esc(title()) + "</div>";
        s += "<p>A native PC port built with <span class=\"accent\">dreamcomp</span>, a framework "
             "for Dreamcast recompilation ports. The game's SH-4 code is translated ahead of time "
             "into C++; the console's graphics and sound hardware are emulated by the "
             "dream-recomp engine.</p>";
        s += "<p><b>You need your own disc.</b> No game data is included: the port reads your "
             "disc image (.cue, .gdi or .chd) at start-up and checks it is the exact release it "
             "was built from.</p>";
        s += "<p>dreamcomp by Daniel Gomes Vieira. dream-recomp engine by phobos665, with "
             "portions of Flycast (flyinghead and contributors). Licensed under the GNU General "
             "Public License v2.0.</p>";
        s += "<p>Interface: RmlUi (MIT), FreeType (FTL / GPL-2.0), SDL3 (zlib). Fonts: Inter by "
             "Rasmus Andersson and PromptFont by Yukari Hafner, both SIL Open Font License "
             "1.1.</p>";
        s += "</div>";
        s += row_button("r-readme", "readme", "Read me",
                        "Installation, controls and known issues", "Open");
        return s;
    }

    void rebuild() {
        dirty_ = false;
        Rml::Element* focused = context_->GetFocusElement();
        std::string focus_id = focused ? focused->GetId() : std::string();
        if (auto* tabs = doc_->GetElementById("tabs")) {
            std::string t;
            for (int i = 0; i < kTabCount; ++i)
                t += std::string("<div id=\"tab-") + kTabs[i][0] + "\" class=\"tab" +
                     (i == tab_ ? " active" : "") + "\" act=\"tab:" + kTabs[i][0] + "\">" +
                     kTabs[i][1] + "</div>";
            tabs->SetInnerRML(t);
        }
        std::string body;
        switch (tab_) {
        case 0: body = tab_play(); break;
        case 1: body = tab_graphics(); break;
        case 2: body = tab_controls(); break;
        case 3: body = tab_enhancements(); break;
        default: body = tab_about(); break;
        }
        auto* content = doc_->GetElementById("content");
        content->SetInnerRML(body);
        if (focus_first_) {
            focus_first_ = false;
            if (auto* first = content->QuerySelector(".focusable"))
                focus_id = first->GetId();
        }
        if (focus_tab_) {
            focus_tab_ = false;
            focus_id = std::string("tab-") + kTabs[tab_][0];
        }

        // Tab-switch hints either side of the tabs, and the footer.
        if (auto* e = doc_->GetElementById("tab-prev"))
            e->SetInnerRML(pads_.empty() ? "<span class=\"tabkey\">Q</span>"
                                         : std::string("<span class=\"glyph tabglyph\">") +
                                               kGlyphLB + "</span>");
        if (auto* e = doc_->GetElementById("tab-next"))
            e->SetInnerRML(pads_.empty() ? "<span class=\"tabkey\">E</span>"
                                         : std::string("<span class=\"glyph tabglyph\">") +
                                               kGlyphRB + "</span>");
        if (auto* hints = doc_->GetElementById("hints")) {
            std::string h;
            if (!pads_.empty())
                h = glyph(kGlyphA) + "<span class=\"hint-text\">Select</span>" + glyph(kGlyphB) +
                    "<span class=\"hint-text\">Back</span>" + glyph(kGlyphLB) + glyph(kGlyphRB) +
                    "<span class=\"hint-text\">Switch tab</span>";
            else
                h = glyph(kGlyphEnter) + "<span class=\"hint-text\">Select</span>" +
                    glyph(kGlyphEsc) + "<span class=\"hint-text\">Back</span>" +
                    "<span class=\"key\">Q</span><span class=\"key\">E</span><span "
                    "class=\"hint-text\">Switch tab</span>";
            // With a pad, the mouse and keyboard still work; the hints follow the pad.
            hints->SetInnerRML(h);
        }
        if (auto* p = doc_->GetElementById("pending")) {
            std::string t;
            if (!toast_.empty())
                t = esc(toast_);
            else if (draft_.any_pending())
                t = "\xE2\x97\x8F  Unsaved changes";
            p->SetInnerRML(t);
        }
        if (auto* b = doc_->GetElementById("btn-apply"))
            b->SetClass("attention", draft_.any_pending());
        if (auto* b = doc_->GetElementById("btn-start"))
            b->SetClass("disabled", !playable());

        if (!focus_id.empty())
            if (auto* el = doc_->GetElementById(focus_id)) {
                el->Focus(true);
                el->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
                last_focus_ = focus_id;
            }
        // Lay the new content out now: spatial navigation on the next key press needs positions.
        context_->Update();
    }

    static std::string glyph(const char* g) { return std::string("<span class=\"glyph\">") + g + "</span>"; }

    // ---- actions ----
    void set_tab(int t, bool keep_on_tabs) {
        tab_ = (t % kTabCount + kTabCount) % kTabCount;
        if (keep_on_tabs)
            focus_tab_ = true;
        else
            focus_first_ = true;
        dirty_ = true;
    }

    void adjust(const std::string& key, int step) {
        if (key == "scale") {
            const int v = std::clamp(draft_.get_int("scale", 2), 1, 8) + step;
            draft_.set("scale", std::to_string(((v - 1) % 8 + 8) % 8 + 1));
        } else if (key == "aspect" && ctx_.port && ctx_.port->widescreen) {
            draft_.set("aspect", cycle(aspect_choices(true, ctx_.port->max_aspect),
                                       draft_.get("aspect", "16:9"), step));
        } else if (key == "fit") {
            draft_.set("fit", cycle(fit_choices(), draft_.get("fit", "crop"), step));
        } else if (key == "fps") {
            draft_.set("fps", cycle(fps_choices(), draft_.get("fps", "auto"), step));
        } else if (key == "rumble") {
            const int v = std::clamp(draft_.get_int("rumble", 100) + step * 10, 0, 100);
            draft_.set("rumble", std::to_string(v));
        }
        dirty_ = true;
    }

    void toggle(const std::string& key, int dir) {  // dir: 0 flip, -1 off, +1 on
        if (key == "texture_pack") {
            const bool on = texture_pack_on(draft_.get("texture_pack"));
            const bool want = dir == 0 ? !on : dir > 0;
            if (want != on) {
                // On: back to the saved directory if there was one, else the default folder.
                const std::string saved = ctx_.settings->get("texture_pack");
                draft_.set("texture_pack", want ? (texture_pack_on(saved) ? saved : "") : "off");
            }
        } else {
            const bool on = draft_.get_bool(key, false);
            const bool want = dir == 0 ? !on : dir > 0;
            draft_.set(key, want ? "true" : "false");
        }
        dirty_ = true;
    }

    void mod_action(const std::string& kind, const std::string& name) {
        auto mods = list_mods(config_dir_ / "mods", draft_.get("mods"));
        for (std::size_t i = 0; i < mods.size(); ++i) {
            if (mods[i].name != name)
                continue;
            if (kind == "mod")
                mods[i].enabled = !mods[i].enabled;
            else if (kind == "modup" && i > 0)
                std::swap(mods[i], mods[i - 1]);
            else if (kind == "moddown" && i + 1 < mods.size())
                std::swap(mods[i], mods[i + 1]);
            break;
        }
        draft_.set("mods", mods_setting(mods));
        dirty_ = true;
    }

    void open_folder(const std::filesystem::path& dir) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!SDL_OpenURL(file_url(dir).c_str()))
            std::fprintf(stderr, "launcher: cannot open %s: %s\n", u8(dir).c_str(), SDL_GetError());
    }

    void apply() {
        if (!draft_.any_pending())
            return;
        toast_ = draft_.apply() ? "Settings saved" : "Could not write the settings file";
        toast_until_ = SDL_GetTicks() + 2500;
        std::printf("launcher: apply: %s (%s)\n", toast_.c_str(),
                    u8(ctx_.settings->file()).c_str());
        dirty_ = true;
    }

    void start() {
        if (!playable())
            return;
        if (draft_.any_pending() && !draft_.apply())
            std::fprintf(stderr, "launcher: could not write %s\n",
                         u8(ctx_.settings->file()).c_str());
        std::printf("launcher: start game\n");
        outcome_ = Outcome::Start;
        done_ = true;
    }

    void back() {
        Rml::Element* f = context_->GetFocusElement();
        const std::string id = f ? f->GetId() : std::string();
        Rml::Element* target = nullptr;
        if (id.rfind("tab-", 0) == 0)
            target = doc_->GetElementById("btn-quit");
        else
            target = doc_->GetElementById(std::string("tab-") + kTabs[tab_][0]);
        if (target)
            target->Focus(true);
    }

    void pick_disc() {
        if (dialog_open_)
            return;
        static const SDL_DialogFileFilter filters[] = {
            {"Dreamcast disc images (*.cue, *.gdi, *.chd)", "cue;gdi;chd"}, {"All files", "*"}};
        std::string start;
        const std::string cur = draft_.get("disc");
        if (!cur.empty())
            start = u8(std::filesystem::u8path(cur).parent_path());
        dialog_open_ = true;
        SDL_ShowOpenFileDialog(dialog_done, &dialog_, window_, filters, 2,
                               start.empty() ? nullptr : start.c_str(), false);
        dirty_ = true;
    }

    void run_act(const std::string& act, Rml::Event* ev, Rml::Element* el) {
        const auto colon = act.find(':');
        const std::string kind = act.substr(0, colon);
        const std::string arg = colon == std::string::npos ? std::string() : act.substr(colon + 1);
        if (kind == "tabstep") {
            set_tab(tab_ + std::atoi(arg.c_str()), tab_focused());
        } else if (kind == "tab") {
            for (int i = 0; i < kTabCount; ++i)
                if (arg == kTabs[i][0])
                    set_tab(i, true);
        } else if (kind == "cycle" || kind == "next")
            adjust(arg, +1);
        else if (kind == "prev")
            adjust(arg, -1);
        else if (kind == "toggle")
            toggle(arg, 0);
        else if (kind == "slider") {
            // A click on the bar sets the value there; Enter steps by 10 and wraps.
            float mx = ev ? ev->GetParameter<float>("mouse_x", -1.0f) : -1.0f;
            Rml::Element* bar = doc_->GetElementById("rumble-slider");
            if (mx >= 0.0f && bar) {
                const float x0 = bar->GetAbsoluteLeft() + bar->GetClientLeft();
                const float w = bar->GetClientWidth();
                if (w > 0.0f && mx >= x0 - 20.0f && mx <= x0 + w + 20.0f) {
                    const int v = static_cast<int>(std::clamp((mx - x0) / w, 0.0f, 1.0f) * 10.0f +
                                                   0.5f) * 10;
                    draft_.set(arg, std::to_string(v));
                    dirty_ = true;
                }
            } else {
                const int v = draft_.get_int(arg, 100);
                draft_.set(arg, std::to_string(v >= 100 ? 0 : v + 10));
                dirty_ = true;
            }
        } else if (kind == "mod" || kind == "modup" || kind == "moddown")
            mod_action(kind, arg);
        else if (kind == "disc")
            pick_disc();
        else if (kind == "open-textures")
            open_folder(texture_pack_dir(draft_.get("texture_pack"), config_dir_));
        else if (kind == "open-mods")
            open_folder(config_dir_ / "mods");
        else if (kind == "readme") {
            std::error_code ec;
            const auto local = ctx_.exe_dir / "README.md";
            const std::string url = std::filesystem::exists(local, ec)
                                         ? file_url(local)
                                         : "https://github.com/danielgomesvieira2000/dreamcomp#readme";
            SDL_OpenURL(url.c_str());
        } else if (kind == "apply")
            apply();
        else if (kind == "start")
            start();
        else if (kind == "quit") {
            outcome_ = Outcome::Quit;
            done_ = true;
        }
        (void)el;
    }

    static Rml::Element* with_act(Rml::Element* e, std::string& act) {
        for (; e; e = e->GetParentNode()) {
            act = e->GetAttribute<Rml::String>("act", "");
            if (!act.empty())
                return e;
        }
        return nullptr;
    }

    void ProcessEvent(Rml::Event& ev) override {
        if (ev == Rml::EventId::Focus) {
            if (Rml::Element* t = ev.GetTargetElement(); t && !t->GetId().empty() &&
                                                         t != doc_)
                last_focus_ = t->GetId();
            return;
        }
        if (ev == Rml::EventId::Click) {
            std::string act;
            if (Rml::Element* e = with_act(ev.GetTargetElement(), act)) {
                run_act(act, &ev, e);
                ev.StopPropagation();
            }
            return;
        }
        if (ev == Rml::EventId::Keydown) {
            const auto key = static_cast<Rml::Input::KeyIdentifier>(
                ev.GetParameter<int>("key_identifier", 0));
            Rml::Element* focus = context_->GetFocusElement();
            // Nothing focused (a click on empty space): arrows bring back the last focus.
            if (!focus || focus == doc_ || focus->GetTagName() == "body") {
                if (key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN ||
                    key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT) {
                    Rml::Element* back = last_focus_.empty() ? nullptr
                                                             : doc_->GetElementById(last_focus_);
                    if (!back)
                        back = doc_->GetElementById("content")->QuerySelector(".focusable");
                    if (back)
                        back->Focus(true);
                    ev.StopPropagation();
                }
                return;
            }
            if (key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT) {
                const int step = key == Rml::Input::KI_LEFT ? -1 : +1;
                const std::string act = focus->GetAttribute<Rml::String>("act", "");
                const auto colon = act.find(':');
                const std::string kind = act.substr(0, colon);
                const std::string arg =
                    colon == std::string::npos ? std::string() : act.substr(colon + 1);
                if (kind == "cycle" || kind == "slider") {
                    adjust(arg, step);
                    ev.StopPropagation();
                    return;
                }
                if (kind == "toggle") {
                    toggle(arg, step);
                    ev.StopPropagation();
                    return;
                }
            }
            if (key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN ||
                key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT) {
                navigate(focus, key);
                ev.StopPropagation();
            }
        }
    }

    // ---- spatial navigation ----
    // RmlUi's own `nav: auto` search does not enter or leave a scroll container, and the tab
    // contents scroll. So the launcher moves focus itself, with the same distance heuristic, in
    // three regions: tab bar, content, footer. Left/right stay within a region; up/down prefer
    // the content, then the neighbouring region.
    static Rml::Element* region_of(Rml::Element* e) {
        for (; e; e = e->GetParentNode()) {
            const auto& id = e->GetId();
            if (id == "tabbar" || id == "content" || id == "footer")
                return e;
        }
        return nullptr;
    }

    static long nav_score(const Rml::Rectanglef& from, const Rml::Rectanglef& to,
                          Rml::Input::KeyIdentifier key) {
        // Distance along the direction, plus a heavy penalty for lying outside the strip the
        // source projects in that direction.
        const bool vertical = key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN;
        const int ax = vertical ? 1 : 0, cx = vertical ? 0 : 1;
        float main = 0.0f;
        if (key == Rml::Input::KI_DOWN || key == Rml::Input::KI_RIGHT)
            main = to.p0[ax] - from.p1[ax];
        else
            main = from.p0[ax] - to.p1[ax];
        if (main < -1.0f)
            return -1;
        const float cross = std::max(0.0f, to.p0[cx] - from.p1[cx]) +
                            std::max(0.0f, from.p0[cx] - to.p1[cx]);
        return static_cast<long>(std::max(main, 0.0f)) + 10000L * static_cast<long>(cross);
    }

    static Rml::Rectanglef box_of(Rml::Element* e) {
        const Rml::Vector2f pos = e->GetAbsoluteOffset(Rml::BoxArea::Border);
        return Rml::Rectanglef::FromPositionSize(pos, e->GetBox().GetSize(Rml::BoxArea::Border));
    }

    static Rml::Element* nav_search(Rml::Element* region, Rml::Element* from,
                                    Rml::Input::KeyIdentifier key) {
        if (!region)
            return nullptr;
        Rml::ElementList list;
        region->QuerySelectorAll(list, ".focusable, .tab");
        const Rml::Rectanglef src = box_of(from);
        Rml::Element* best = nullptr;
        long best_score = -1;
        for (Rml::Element* e : list) {
            if (e == from)
                continue;
            const long sc = nav_score(src, box_of(e), key);
            if (sc >= 0 && (!best || sc < best_score)) {
                best = e;
                best_score = sc;
            }
        }
        return best;
    }

    void navigate(Rml::Element* focus, Rml::Input::KeyIdentifier key) {
        Rml::Element* region = region_of(focus);
        Rml::Element* tabbar = doc_->GetElementById("tabbar");
        Rml::Element* content = doc_->GetElementById("content");
        Rml::Element* footer = doc_->GetElementById("footer");
        Rml::Element* active_tab = doc_->GetElementById(std::string("tab-") + kTabs[tab_][0]);
        Rml::Element* target = nav_search(region, focus, key);
        if (!target && key == Rml::Input::KI_DOWN) {
            if (region == tabbar)
                target = nav_search(content, focus, key);
            if (!target && region != footer)
                target = playable() ? doc_->GetElementById("btn-start")
                                    : nav_search(footer, focus, key);
        } else if (!target && key == Rml::Input::KI_UP) {
            if (region == footer)
                target = nav_search(content, focus, key);
            if (!target && region != tabbar)
                target = active_tab;
        }
        if (!target)
            return;
        target->Focus(true);
        target->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
    }

    // ---- gamepads ----
    void refresh_pads() {
        pads_.clear();
        int count = 0;
        if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
            for (int i = 0; i < count; ++i) {
                const char* n = SDL_GetGamepadNameForID(ids[i]);
                pads_.push_back(n && *n ? n : "Controller");
            }
            SDL_free(ids);
        }
        dirty_ = true;
    }

    void key(Rml::Input::KeyIdentifier k) {
        context_->ProcessKeyDown(k, 0);
        context_->ProcessKeyUp(k, 0);
    }

    void pad_dir(Rml::Input::KeyIdentifier k) {
        held_ = k;
        repeat_at_ = SDL_GetTicks() + 380;
        key(k);
    }

    void pad_tick() {
        if (held_ != Rml::Input::KI_UNKNOWN && SDL_GetTicks() >= repeat_at_) {
            repeat_at_ = SDL_GetTicks() + 95;
            key(held_);
        }
        if (!toast_.empty() && SDL_GetTicks() >= toast_until_) {
            toast_.clear();
            dirty_ = true;
        }
    }

    void handle(SDL_Event& ev) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            outcome_ = Outcome::Quit;
            done_ = true;
            return;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            // RmlUi's backend would set the desktop scale as the dp ratio; the launcher scales
            // with the window height instead (1 dp = 1 px at 720 px), so drop it.
            return;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            context_->SetDimensions(Rml::Vector2i(ev.window.data1, ev.window.data2));
            context_->SetDensityIndependentPixelRatio(static_cast<float>(ev.window.data2) / 720.0f);
            return;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (SDL_Gamepad* g = SDL_OpenGamepad(ev.gdevice.which))
                open_pads_.push_back(g);
            refresh_pads();
            return;
        case SDL_EVENT_GAMEPAD_REMOVED:
            for (auto it = open_pads_.begin(); it != open_pads_.end(); ++it)
                if (SDL_GetGamepadID(*it) == ev.gdevice.which) {
                    SDL_CloseGamepad(*it);
                    open_pads_.erase(it);
                    break;
                }
            refresh_pads();
            return;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            switch (ev.gbutton.button) {
            case SDL_GAMEPAD_BUTTON_DPAD_UP: pad_dir(Rml::Input::KI_UP); break;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN: pad_dir(Rml::Input::KI_DOWN); break;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT: pad_dir(Rml::Input::KI_LEFT); break;
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: pad_dir(Rml::Input::KI_RIGHT); break;
            case SDL_GAMEPAD_BUTTON_SOUTH: key(Rml::Input::KI_RETURN); break;
            case SDL_GAMEPAD_BUTTON_EAST: back(); break;
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: set_tab(tab_ - 1, tab_focused()); break;
            case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: set_tab(tab_ + 1, tab_focused()); break;
            case SDL_GAMEPAD_BUTTON_START: start(); break;
            default: break;
            }
            return;
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            held_ = Rml::Input::KI_UNKNOWN;
            return;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            if (ev.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTX && ev.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTY)
                return;
            const float v = static_cast<float>(ev.gaxis.value) / 32767.0f;
            const bool x = ev.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX;
            int& state = x ? stick_x_ : stick_y_;
            int now = v > 0.6f ? 1 : v < -0.6f ? -1 : (std::abs(v) < 0.3f ? 0 : state);
            if (now != state) {
                state = now;
                if (now == 0)
                    held_ = Rml::Input::KI_UNKNOWN;
                else if (x)
                    pad_dir(now > 0 ? Rml::Input::KI_RIGHT : Rml::Input::KI_LEFT);
                else
                    pad_dir(now > 0 ? Rml::Input::KI_DOWN : Rml::Input::KI_UP);
            }
            return;
        }
        case SDL_EVENT_KEY_DOWN:
            if (ev.key.key == SDLK_Q) {
                set_tab(tab_ - 1, tab_focused());
                return;
            }
            if (ev.key.key == SDLK_E) {
                set_tab(tab_ + 1, tab_focused());
                return;
            }
            if (ev.key.key == SDLK_ESCAPE || ev.key.key == SDLK_BACKSPACE) {
                back();
                return;
            }
            break;
        default: break;
        }
        RmlSDL::InputEventHandler(context_, window_, ev);
    }

    bool tab_focused() const {
        Rml::Element* f = context_->GetFocusElement();
        return f && f->GetId().rfind("tab-", 0) == 0;
    }

    void draw() {
        context_->Update();
        render_->BeginFrame();
        context_->Render();
        render_->EndFrame();
    }

    // Test aid: DREAMCOMP_LAUNCHER_KEYS="down,right,enter,pad:rb,pad:a,..." feeds key and gamepad
    // presses through the same handlers as real input, one every 150 ms once the disc check is
    // done; the launcher quits by itself 1.5 s after the last one if nothing ended it. Keys: up
    // down left right enter esc tab q e; pads: pad:up/down/left/right/a/b/lb/rb/start.
    void scripted_input() {
        if (script_.empty() && script_at_ == 0)
            return;
        const std::uint64_t now = SDL_GetTicks();
        if (verifying_ || now < script_at_)
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
        SDL_Event e{};
        if (k.rfind("shot:", 0) == 0) {
            std::string err;
            if (!capture(std::filesystem::u8path(k.substr(5)), &err))
                std::fprintf(stderr, "launcher: %s\n", err.c_str());
        } else if (k.rfind("click:", 0) == 0) {
            // A mouse click at the centre of the element with that id.
            if (Rml::Element* el = doc_->GetElementById(k.substr(6))) {
                const Rml::Rectanglef b = box_of(el);
                const float density = std::max(0.1f, SDL_GetWindowPixelDensity(window_));
                e.type = SDL_EVENT_MOUSE_MOTION;
                e.motion.x = (b.p0.x + b.p1.x) * 0.5f / density;
                e.motion.y = (b.p0.y + b.p1.y) * 0.5f / density;
                handle(e);
                Rml::Element* hover = context_->GetHoverElement();
                std::printf("launcher: click %s at %.0f,%.0f (density %.2f) hits <%s id=%s>\n",
                            k.c_str() + 6, e.motion.x, e.motion.y, density,
                            hover ? hover->GetTagName().c_str() : "-",
                            hover ? hover->GetId().c_str() : "");
                e = SDL_Event{};
                e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                e.button.button = SDL_BUTTON_LEFT;
                e.button.down = true;
                handle(e);
                e.type = SDL_EVENT_MOUSE_BUTTON_UP;
                e.button.down = false;
                handle(e);
            }
        } else if (k.rfind("pad:", 0) == 0) {
            const std::string b = k.substr(4);
            const SDL_GamepadButton btn =
                b == "up" ? SDL_GAMEPAD_BUTTON_DPAD_UP
                : b == "down" ? SDL_GAMEPAD_BUTTON_DPAD_DOWN
                : b == "left" ? SDL_GAMEPAD_BUTTON_DPAD_LEFT
                : b == "right" ? SDL_GAMEPAD_BUTTON_DPAD_RIGHT
                : b == "a" ? SDL_GAMEPAD_BUTTON_SOUTH
                : b == "b" ? SDL_GAMEPAD_BUTTON_EAST
                : b == "lb" ? SDL_GAMEPAD_BUTTON_LEFT_SHOULDER
                : b == "rb" ? SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER
                : b == "start" ? SDL_GAMEPAD_BUTTON_START
                               : SDL_GAMEPAD_BUTTON_INVALID;
            e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            e.gbutton.button = static_cast<Uint8>(btn);
            e.gbutton.down = true;
            handle(e);
            e.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
            e.gbutton.down = false;
            handle(e);
        } else {
            const SDL_Keycode code = k == "up" ? SDLK_UP
                                     : k == "down" ? SDLK_DOWN
                                     : k == "left" ? SDLK_LEFT
                                     : k == "right" ? SDLK_RIGHT
                                     : k == "enter" ? SDLK_RETURN
                                     : k == "esc" ? SDLK_ESCAPE
                                     : k == "tab" ? SDLK_TAB
                                     : k == "q" ? SDLK_Q
                                     : k == "e" ? SDLK_E
                                                : SDLK_UNKNOWN;
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.key = code;
            e.key.down = true;
            handle(e);
            e.type = SDL_EVENT_KEY_UP;
            e.key.down = false;
            handle(e);
        }
        Rml::Element* f = context_->GetFocusElement();
        std::printf("launcher: key %-10s -> tab %s, focus %s\n", k.c_str(), kTabs[tab_][0],
                    f ? f->GetId().c_str() : "-");
    }

    Outcome run() {
        if (const char* keys = std::getenv("DREAMCOMP_LAUNCHER_KEYS")) {
            std::string all = keys;
            std::size_t at = 0;
            while (at < all.size()) {
                std::size_t comma = all.find(',', at);
                if (comma == std::string::npos)
                    comma = all.size();
                if (comma > at)
                    script_.push_back(all.substr(at, comma - at));
                at = comma + 1;
            }
            script_at_ = SDL_GetTicks() + 500;
        }
        start_verify(false);
        focus_first_ = true;
        rebuild();
        while (!done_) {
            SDL_Event ev;
            if (SDL_WaitEventTimeout(&ev, 16)) {
                handle(ev);
                while (!done_ && SDL_PollEvent(&ev)) handle(ev);
            }
            poll_background();
            if (dirty_)
                rebuild();
            scripted_input();
            pad_tick();
            if (dirty_)
                rebuild();
            draw();
            SDL_RenderPresent(renderer_);
        }
        if (verifying_ && verify_.valid())
            verify_.wait();
        return outcome_;
    }

    // Screenshot mode: one PNG per tab (or the one asked for), no interaction.
    bool screenshots(std::string* error) {
        // Test aid: DREAMCOMP_LAUNCHER_DRAFT="scale=4;mods=a,b" shows unapplied choices, so the
        // pending marks can be checked in a capture. Nothing is saved in this mode.
        if (const char* d = std::getenv("DREAMCOMP_LAUNCHER_DRAFT")) {
            std::string all = d;
            std::size_t at = 0;
            while (at < all.size()) {
                std::size_t semi = all.find(';', at);
                if (semi == std::string::npos)
                    semi = all.size();
                const std::string kv = all.substr(at, semi - at);
                const auto eq = kv.find('=');
                if (eq != std::string::npos)
                    draft_.set(kv.substr(0, eq), kv.substr(eq + 1));
                at = semi + 1;
            }
        }
        // Test aid: DREAMCOMP_LAUNCHER_PADS="Name A;Name B" stands in for connected pads.
        if (const char* pads = std::getenv("DREAMCOMP_LAUNCHER_PADS")) {
            pads_.clear();
            std::string all = pads;
            std::size_t at = 0;
            while (at < all.size()) {
                std::size_t semi = all.find(';', at);
                if (semi == std::string::npos)
                    semi = all.size();
                if (semi > at)
                    pads_.push_back(all.substr(at, semi - at));
                at = semi + 1;
            }
        }
        start_verify(true);
        std::vector<int> tabs;
        if (!ctx_.tab.empty())
            tabs.push_back(tab_);
        else
            for (int i = 0; i < kTabCount; ++i) tabs.push_back(i);
        // Test aid: DREAMCOMP_LAUNCHER_FOCUS=<element id> focuses (and scrolls to) that element.
        const char* focus_env = std::getenv("DREAMCOMP_LAUNCHER_FOCUS");
        for (int t : tabs) {
            tab_ = t;
            focus_first_ = true;
            rebuild();
            if (focus_env && *focus_env)
                if (auto* el = doc_->GetElementById(focus_env)) {
                    el->Focus(true);
                    context_->Update();
                    el->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
                }
            auto path = ctx_.screenshot;
            if (ctx_.tab.empty())
                path = path.parent_path() /
                       (path.stem().string() + "-" + kTabs[t][0] + path.extension().string());
            if (!capture(path, error))
                return false;
        }
        return true;
    }

    // Renders the current page into an offscreen target of the context's size and writes a PNG.
    bool capture(const std::filesystem::path& path, std::string* error) {
        const Rml::Vector2i dim = context_->GetDimensions();
        SDL_Texture* target = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888,
                                                SDL_TEXTUREACCESS_TARGET, dim.x, dim.y);
        if (!target) {
            *error = std::string("render target: ") + SDL_GetError();
            return false;
        }
        SDL_SetRenderTarget(renderer_, target);
        for (int pass = 0; pass < 3; ++pass) draw();  // layout settles over a couple of updates
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
        const bool ok = dream::render::png::write_file(
            path, px.data(), static_cast<std::uint32_t>(rgba->w), static_cast<std::uint32_t>(rgba->h));
        SDL_DestroySurface(rgba);
        if (!ok) {
            *error = "cannot write " + path.string();
            return false;
        }
        std::printf("launcher: wrote %s\n", path.string().c_str());
        return true;
    }

private:
    const LaunchContext& ctx_;
    Draft draft_;
    std::filesystem::path config_dir_;
    dream::translator::GameConfig cfg_;
    bool cfg_ok_ = false;
    std::string cfg_error_;
    int tab_ = 0;

    DiscCheck disc_;
    bool verifying_ = false;
    std::future<DiscCheck> verify_;
    DialogResult dialog_;
    bool dialog_open_ = false;

    std::vector<std::string> pads_;
    std::vector<SDL_Gamepad*> open_pads_;
    Rml::Input::KeyIdentifier held_ = Rml::Input::KI_UNKNOWN;
    std::uint64_t repeat_at_ = 0;
    int stick_x_ = 0, stick_y_ = 0;

    std::string toast_;
    std::uint64_t toast_until_ = 0;
    std::vector<std::string> script_;
    std::uint64_t script_at_ = 0;
    std::string last_focus_;
    bool dirty_ = true, focus_first_ = false, focus_tab_ = false, done_ = false;
    Outcome outcome_ = Outcome::Quit;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool video_ = false, gamepad_ = false, rml_ = false;
    std::unique_ptr<FileInterface> files_;
    std::unique_ptr<SystemInterface> system_;
    std::unique_ptr<RenderInterface_SDL> render_;
    Rml::Context* context_ = nullptr;
    Rml::ElementDocument* doc_ = nullptr;
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
