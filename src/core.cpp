// The dreamcomp core launcher extension: settings, bare-launch defaults, widescreen and
// presentation. Linked into every port executable (cmake/DreamcompPort.cmake).
//
// Flags it adds (`--help` lists them under "dreamcomp"):
//   --widescreen / --no-widescreen   override the saved setting for this run
//   --fit letterbox|crop|stretch     how the picture fills the window
//   --set KEY=VALUE                  override any setting for this run
//   --save-settings                  write this run's overrides back to the settings file
//   --settings FILE                  use FILE instead of the per-user settings file
//   --launcher / --no-launcher       open (or skip) the launcher window (docs/FRONTEND.md)
//   --launcher-screenshot FILE.png   render the frontend's screens to PNGs and exit
//   --launcher-screen NAME           the screen to open / to capture (--launcher-tab: alias)
// In a window, Escape or a pad's Select/Back opens the settings panel over the running game.
//   --launcher-size WxH              the launcher window's size (default 1280x720)
// Settings it turns into engine flags: texture_pack, dump_textures (docs/TEXTURE-PACKS.md),
// rumble (--rumble), mods (--mod, docs/MODS.md), aspect, fullscreen, scale.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "dream/render/display_list.h"
#include "dream/runtime/host_ext.h"
#include "dream/runtime/system.h"
#include "dreamcomp/port.h"
#include "dreamcomp/settings.h"

#include "dreamcomp/frontend.h"

#ifndef DREAMCOMP_VERSION
#define DREAMCOMP_VERSION "?"
#endif

#if __has_include("dream/render/vk/present.h") && defined(DREAM_WITH_RENDERER)
#include "dream/render/vk/present.h"
#define DREAMCOMP_HAS_PRESENTER 1
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace dreamcomp {

namespace {

const PortInfo* g_port = nullptr;
Settings g_settings;
// Function-local: the port's generated initialiser may run before this file's globals.
std::string& port_version_ref() {
    static std::string v;
    return v;
}

// "dreamcomp 0.1.0 · Soulcalibur 0.1.0", bottom-left in the launcher.
std::string version_line() {
    std::string v = std::string("dreamcomp ") + DREAMCOMP_VERSION;
    if (g_port && g_port->title)
        v += std::string("  \xC2\xB7  ") + g_port->title +
             (port_version_ref().empty() ? std::string() : " " + port_version_ref());
    return v;
}

std::filesystem::path exe_dir(const std::string& argv0) {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
        return std::filesystem::path(buf).parent_path();
#endif
    std::error_code ec;
    auto p = std::filesystem::absolute(argv0, ec);
    return ec ? std::filesystem::current_path() : p.parent_path();
}

bool has_flag(const std::vector<std::string>& a, const char* f) {
    return std::find(a.begin(), a.end(), f) != a.end();
}

std::string flag_value(const std::vector<std::string>& a, const char* f) {
    for (std::size_t i = 1; i + 1 < a.size(); ++i)
        if (a[i] == f)
            return a[i + 1];
    return {};
}

// Removes every `f` (and its value when `takes_value`) so the engine never sees the flag.
void drop_flag(std::vector<std::string>& a, const char* f, bool takes_value) {
    for (std::size_t i = 1; i < a.size();) {
        if (a[i] == f)
            a.erase(a.begin() + static_cast<std::ptrdiff_t>(i),
                    a.begin() + static_cast<std::ptrdiff_t>(std::min(a.size(), i + (takes_value ? 2 : 1))));
        else
            ++i;
    }
}

// `rumble` (0-100, default 100) -> --rumble N; `volume` (0-100) -> --volume N; `mods = a,b` -> --mod <config dir>/mods/a ...
// (first wins, docs/MODS.md). A flag the user passed wins over the setting.
void add_input_and_mod_flags(std::vector<std::string>& args,
                             const std::filesystem::path& config_dir) {
    if (!has_flag(args, "--rumble") && !g_settings.get("rumble").empty()) {
        args.push_back("--rumble");
        args.push_back(std::to_string(std::clamp(g_settings.get_int("rumble", 100), 0, 100)));
    }
    if (!has_flag(args, "--volume") && !g_settings.get("volume").empty()) {
        args.push_back("--volume");
        args.push_back(std::to_string(std::clamp(g_settings.get_int("volume", 100), 0, 100)));
    }
    if (!has_flag(args, "--soft-clip") && g_settings.get("clip", "hard") == "soft")
        args.push_back("--soft-clip");
    if (has_flag(args, "--mod"))
        return;
    const std::string mods = g_settings.get("mods");
    std::size_t at = 0;
    while (at < mods.size()) {
        std::size_t comma = mods.find(',', at);
        if (comma == std::string::npos)
            comma = mods.size();
        std::string name = mods.substr(at, comma - at);
        at = comma + 1;
        const auto b = name.find_first_not_of(" \t");
        if (b == std::string::npos)
            continue;
        name = name.substr(b, name.find_last_not_of(" \t") - b + 1);
        const auto dir = config_dir / "mods" / name;
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) {
            std::fprintf(stderr, "dreamcomp: mod %s: no folder %s\n", name.c_str(),
                         dir.string().c_str());
            continue;
        }
        args.push_back("--mod");
        args.push_back(dir.string());
    }
}

// Texture packs and dumps (docs/TEXTURE-PACKS.md). `texture_pack` names the pack directory;
// unset, `<config dir>/textures` is used when it exists, and "off" turns packs off.
// `dump_textures = true` writes every new texture to `<config dir>/texture_dump/`. The config dir
// is the settings file's own directory, so `--settings` moves both with it. A flag the user passed
// wins over the setting.
void add_texture_flags(std::vector<std::string>& args, const std::filesystem::path& config_dir) {
    if (!has_flag(args, "--texture-pack")) {
        std::string pack = g_settings.get("texture_pack");
        std::error_code ec;
        if (pack.empty() && std::filesystem::is_directory(config_dir / "textures", ec))
            pack = (config_dir / "textures").string();
        if (pack == "off" || pack == "none" || pack == "false")
            pack.clear();
        if (!pack.empty()) {
            args.push_back("--texture-pack");
            args.push_back(pack);
        }
    }
    if (!has_flag(args, "--dump-textures") && g_settings.get_bool("dump_textures", false)) {
        args.push_back("--dump-textures");
        args.push_back((config_dir / "texture_dump").string());
    }
}

// "16:9" -> 1.7778. The setting `aspect` (default 16:9) applies to ports that implement
// PortInfo::widescreen; others stay 4:3. The older `widescreen = false` key still means 4:3.
float parse_aspect(const std::string& s) {
    const auto colon = s.find(':');
    if (colon == std::string::npos)
        return 4.0f / 3.0f;
    const float w = std::strtof(s.c_str(), nullptr);
    const float h = std::strtof(s.c_str() + colon + 1, nullptr);
    return (w > 0.0f && h > 0.0f) ? std::clamp(w / h, 4.0f / 3.0f, 4.0f) : 4.0f / 3.0f;
}

float target_aspect() {
    if (!g_port || !g_port->widescreen)
        return 4.0f / 3.0f;
    if (g_settings.get("aspect").empty() && !g_settings.get("widescreen").empty() &&
        !g_settings.get_bool("widescreen", true))
        return 4.0f / 3.0f;
    return std::min(parse_aspect(g_settings.get("aspect", "16:9")), g_port->max_aspect);
}

class Core final : public dream::host::Extension {
public:
    const char* name() const override { return "dreamcomp"; }

    void adjust_args(std::vector<std::string>& args) override {
        const bool bare = args.size() == 1;
        const std::string id = g_port && g_port->id ? g_port->id : "dreamcomp";
        // Launcher flags are dreamcomp's alone: take them out before the engine parses.
        const bool want_launcher = has_flag(args, "--launcher");
        const char* env_nl = std::getenv("DREAMCOMP_NO_LAUNCHER");
        const bool no_launcher =
            has_flag(args, "--no-launcher") || (env_nl && *env_nl && *env_nl != '0');
        const std::string shot = flag_value(args, "--launcher-screenshot");
        std::string shot_tab = flag_value(args, "--launcher-screen");
        if (shot_tab.empty())
            shot_tab = flag_value(args, "--launcher-tab");
        const std::string shot_size = flag_value(args, "--launcher-size");
        drop_flag(args, "--launcher", false);
        drop_flag(args, "--no-launcher", false);
        drop_flag(args, "--launcher-screenshot", true);
        drop_flag(args, "--launcher-tab", true);
        drop_flag(args, "--launcher-screen", true);
        drop_flag(args, "--launcher-size", true);
        // Double-clicked, or the launcher asked for: the play path with the saved settings.
        const bool play = bare || want_launcher;

        std::filesystem::path file = flag_value(args, "--settings");
        if (file.empty())
            if (const char* env = std::getenv("DREAMCOMP_SETTINGS"))
                file = env;
        if (file.empty())
            file = default_config_dir(id) / "settings.ini";
        g_settings.load(file);

        if (!has_flag(args, "--config")) {
            // A packaged port keeps its config next to the executable; a build tree one level up.
            const auto dir = exe_dir(args[0]);
            for (const auto& cand : {dir / (id + ".toml"), dir / "game" / (id + ".toml"),
                                     dir.parent_path().parent_path() / "game" / (id + ".toml")}) {
                if (std::filesystem::exists(cand)) {
                    args.push_back("--config");
                    args.push_back(cand.string());
                    break;
                }
            }
        }
        // The launcher runs before anything is derived from the settings: Start saves the
        // player's choices, and everything below builds the engine's flags from them.
        if (!shot.empty() || (play && !no_launcher))
            run_launcher(args, shot, shot_tab, shot_size);
        // `--set` is parsed after this, but the texture keys decide which engine flags to add
        // here, so those two are applied now. parse_arg sets them again to the same value, and a
        // setting only becomes dirty once, so --save-settings behaves as it does for any key.
        for (std::size_t i = 1; i + 1 < args.size(); ++i) {
            if (args[i] != "--set")
                continue;
            const std::string& kv = args[i + 1];
            const auto eq = kv.find('=');
            const std::string key = kv.substr(0, eq);
            if (eq != std::string::npos &&
                (key == "texture_pack" || key == "dump_textures" || key == "aspect" ||
                 key == "fullscreen" || key == "scale" || key == "rumble" || key == "mods" || key == "volume" || key == "clip" ||
                 key == "fps"))
                g_settings.set(key, kv.substr(eq + 1));
        }
        // The aspect flags decide the render target's shape, which the engine needs before it
        // parses anything: apply them now too (parse_arg consumes them later).
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--widescreen")
                g_settings.set("aspect", "16:9");
            else if (args[i] == "--no-widescreen")
                g_settings.set("aspect", "4:3");
            else if (args[i] == "--aspect" && i + 1 < args.size())
                g_settings.set("aspect", args[i + 1]);
        }
        add_texture_flags(args, g_settings.file().parent_path());
        add_input_and_mod_flags(args, g_settings.file().parent_path());
        const float aspect = target_aspect();
        if (aspect > 4.0f / 3.0f + 0.01f && g_port->widescreen_anamorphic &&
            !has_flag(args, "--render-aspect")) {
            args.push_back("--render-aspect");
            args.push_back(std::to_string(aspect));
        }
        if (g_settings.get_bool("fullscreen", false) && !has_flag(args, "--fullscreen"))
            args.push_back("--fullscreen");
        // Frame rate (docs/INTERPOLATION.md): auto blends a frame between game frames on displays
        // above 60 Hz; 120 forces it; 60 is the game's own rate only.
        if (!has_flag(args, "--interpolate") && !has_flag(args, "--interpolate-auto")) {
            const std::string fps = g_settings.get("fps", "auto");
            if (fps == "auto")
                args.push_back("--interpolate-auto");
            else if (fps == "120")
                args.push_back("--interpolate");
        }

        // For the in-game menu (wants_overlay()).
        config_path_ = flag_value(args, "--config");
        exe_dir_ = exe_dir(args[0]);

        if (!has_flag(args, "--disc")) {
            const std::string disc = g_settings.get("disc");
            if (!disc.empty()) {
                args.push_back("--disc");
                args.push_back(disc);
            }
        }
        if (play) {
            // Double-clicked (or --launcher): play, with the player's saved choices. A flag given
            // on the command line wins (the engine takes the last occurrence, so none is added).
            if (!has_flag(args, "--window"))
                args.push_back("--window");
            if (!has_flag(args, "--scale")) {
                args.push_back("--scale");
                args.push_back(std::to_string(std::clamp(g_settings.get_int("scale", 2), 1, 8)));
            }
            if (!has_flag(args, "--vmu")) {
                const std::string vmu = g_settings.get(
                    "vmu", (default_config_dir(id) / "vmu_a1.bin").string());
                // The engine creates a missing card, but not the folder it lives in, which on a
                // first launch does not exist yet.
                std::error_code ec;
                std::filesystem::create_directories(std::filesystem::path(vmu).parent_path(), ec);
                args.push_back("--vmu");
                args.push_back(vmu);
            }
        }
    }

    int parse_arg(int i, int argc, char** argv) override {
        const char* a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[i + 1] : nullptr; };
        if (!std::strcmp(a, "--widescreen") || !std::strcmp(a, "--no-widescreen"))
            return 1;  // applied in adjust_args
        if (!std::strcmp(a, "--aspect") && next())
            return 2;  // applied in adjust_args
        if (!std::strcmp(a, "--fit") && next()) {
            g_settings.set("fit", next());
            return 2;
        }
        if (!std::strcmp(a, "--settings") && next())
            return 2;  // consumed in adjust_args
        if (!std::strcmp(a, "--save-settings")) {
            save_on_exit_ = true;
            return 1;
        }
        if (!std::strcmp(a, "--set") && next()) {
            const std::string kv = next();
            const auto eq = kv.find('=');
            if (eq != std::string::npos)
                g_settings.set(kv.substr(0, eq), kv.substr(eq + 1));
            return 2;
        }
        return 0;
    }

    // The launcher window (docs/FRONTEND.md). Quit ends the process; Start returns with the
    // settings saved; a launcher that cannot open says why and the game starts as before.
    static void run_launcher(const std::vector<std::string>& args, const std::string& shot,
                             const std::string& tab, const std::string& size) {
#ifdef DREAMCOMP_WITH_FRONTEND
        frontend::LaunchContext ctx;
        ctx.settings = &g_settings;
        ctx.port = g_port;
        ctx.config = flag_value(args, "--config");
        ctx.exe_dir = exe_dir(args[0]);
        ctx.screenshot = shot;
        ctx.tab = tab;
        ctx.version = version_line();
        if (const auto x = size.find('x'); x != std::string::npos) {
            ctx.width = std::clamp(std::atoi(size.c_str()), 640, 7680);
            ctx.height = std::clamp(std::atoi(size.c_str() + x + 1), 360, 4320);
        }
        std::string err;
        const auto out = frontend::run_launcher(ctx, &err);
        if (!shot.empty()) {
            if (out == frontend::Outcome::Error)
                std::fprintf(stderr, "launcher: %s\n", err.c_str());
            std::fflush(stdout);
            std::exit(out == frontend::Outcome::Error ? 1 : 0);
        }
        if (out == frontend::Outcome::Quit) {
            std::fflush(stdout);
            std::exit(0);
        }
        if (out == frontend::Outcome::Error)
            std::fprintf(stderr, "launcher: %s; starting the game directly\n", err.c_str());
#else
        (void)args, (void)tab, (void)size;
        if (!shot.empty()) {
            std::fprintf(stderr, "launcher: this build has no launcher (SDL3 not found)\n");
            std::exit(1);
        }
#endif
    }

    void usage(std::FILE* out) override {
        std::fprintf(out,
                     "\ndreamcomp:\n"
                     "  --aspect W:H           4:3 (original), 16:9, 21:9 or 32:9: a wider view, not\n"
                     "                         a stretch (ports with widescreen support)\n"
                     "  --widescreen / --no-widescreen  shorthand for --aspect 16:9 / 4:3\n"
                     "  --fit MODE             letterbox, crop or stretch (default crop: no bars)\n"
                     "  --set KEY=VALUE        override one setting for this run\n"
                     "  --save-settings        keep this run's overrides\n"
                     "  --settings FILE        settings file (default: %s)\n"
                     "  --launcher             open the launcher window first (the default\n"
                     "                         when started with no arguments)\n"
                     "  --no-launcher          skip it (also DREAMCOMP_NO_LAUNCHER=1)\n"
                     "  --launcher-screenshot FILE.png  write the frontend's screens as PNGs, exit\n"
                     "  --launcher-screen NAME launcher, or settings/TAB (general, controls,\n"
                     "                         graphics, sound, mods)\n"
                     "  In a window, Escape or a pad's Select/Back opens the settings panel over\n"
                     "  the running game; DREAMCOMP_NO_OVERLAY=1 turns that off.\n"
                     "  --launcher-size WxH    launcher window size (default 1280x720)\n"
                     "  Settings rumble=0..100, volume=0..100 and mods=a,b add --rumble,\n"
                     "  --volume and --mod\n"
                     "  <settings dir>/mods/<name> (first wins).\n"
                     "  Settings texture_pack=DIR|off and dump_textures=true add --texture-pack\n"
                     "  and --dump-textures (default pack: <settings dir>/textures if present).\n"
                     "  Started with no arguments, the launcher opens; Start Game plays in a\n"
                     "  window with the saved settings (disc, scale, memory card).\n",
                     g_settings.file().string().c_str());
    }

    void on_start(dream::System& sys) override {
        // Settings changed by flags this run are not saved unless asked: a test run must not
        // leave its choices behind in the player's file.
        launch_dirty_ = g_settings.dirty();
        hud_fix_ = g_settings.get_bool("hud_fix", true);
        hud_edges_ = g_settings.get("hud_layout", "edges") != "center";
        apply_presentation();
        if (g_port && g_port->on_start)
            g_port->on_start(sys, g_settings);
        std::printf("dreamcomp: %s, settings %s%s\n", g_port && g_port->title ? g_port->title : "?",
                    g_settings.file().string().c_str(),
                    widescreen() ? (", aspect " + g_settings.get("aspect", "16:9")).c_str() : "");
    }

    void on_vblank(dream::System& sys) override {
#ifdef DREAMCOMP_WITH_FRONTEND
        if (overlay_)
            overlay_->on_vblank(vblanks_);
#endif
        ++vblanks_;
        if (g_port && g_port->widescreen)
            g_port->widescreen(sys, widescreen() ? target_aspect() : 4.0f / 3.0f);
        if (g_port && g_port->on_vblank)
            g_port->on_vblank(sys, g_settings);
    }

    // Widescreen HUD correction (PortInfo::hud, docs/HUD.md). Setting `hud_fix` (default on)
    // turns it off for an A/B. Counts are reported at exit.
    void on_frame(dream::render::Frame& frame) override {
        if (!g_port || !g_port->hud.enabled || !widescreen() || !hud_fix_)
            return;
        const auto& rule = g_port->hud;
        const float k = (4.0f / 3.0f) / target_aspect();  // < 1: squeeze back
        // Pass 1: depth of every flat sprite, and how many sprites share each depth.
        auto flat_z = [&](const dream::render::Polygon& p, float& z) {
            if ((p.pcw >> 29) != 5u || p.count == 0)
                return false;
            z = frame.vertices[p.first].z;
            for (std::uint32_t i = 1; i < p.count; ++i)
                if (frame.vertices[p.first + i].z != z)
                    return false;
            return true;
        };
        depth_counts_.clear();
        for (const auto& list : frame.lists)
            for (const auto& p : list) {
                float z;
                if (flat_z(p, z))
                    ++depth_counts_[z];
            }
        // Pass 2: the HUD primitives and their horizontal extents.
        hud_items_.clear();
        for (auto& list : frame.lists) {
            for (const auto& p : list) {
                float z;
                if (!flat_z(p, z))
                    continue;
                if (z < rule.overlay_z && depth_counts_[z] < rule.min_shared)
                    continue;
                HudItem it{&p, 1e30f, -1e30f, 1e30f, -1e30f};
                for (std::uint32_t i = 0; i < p.count; ++i) {
                    const auto& v = frame.vertices[p.first + i];
                    it.x0 = std::min(it.x0, v.x);
                    it.x1 = std::max(it.x1, v.x);
                    it.y0 = std::min(it.y0, v.y);
                    it.y1 = std::max(it.y1, v.y);
                }
                if (it.x1 - it.x0 > rule.full_width)
                    continue;
                hud_items_.push_back(it);
            }
        }
        // Layout `center`: the whole HUD scaled about the screen centre (its original 4:3 layout).
        // Layout `edges` (default): sprites that touch horizontally and overlap vertically form one
        // element (a health bar is a cap, a bar and a cap); each element is un-stretched about the
        // edge of the screen third its centre falls in, so bars stay at the screen edges.
        const std::size_t n = hud_items_.size();
        hud_group_.resize(n);
        for (std::size_t i = 0; i < n; ++i) hud_group_[i] = i;
        auto root = [&](std::size_t i) {
            while (hud_group_[i] != i) i = hud_group_[i] = hud_group_[hud_group_[i]];
            return i;
        };
        if (hud_edges_) {
            // Touching pieces (gap <= 3 px, overlapping rows) are one element; so are pieces of
            // one text line -- same top and bottom within 4 px -- across word gaps up to 1.5x
            // their height, so "INSERT COIN" moves as one. A bar beside taller timer digits has a
            // different extent and stays separate.
            constexpr float kTouch = 3.0f, kSameRow = 4.0f;
            for (std::size_t i = 0; i < n; ++i)
                for (std::size_t j = i + 1; j < n; ++j) {
                    const auto& a = hud_items_[i];
                    const auto& b = hud_items_[j];
                    const float gap = std::max(a.x0, b.x0) - std::min(a.x1, b.x1);
                    const bool rows_overlap = a.y0 <= b.y1 && b.y0 <= a.y1;
                    const bool same_line = std::abs(a.y0 - b.y0) <= kSameRow &&
                                           std::abs(a.y1 - b.y1) <= kSameRow;
                    const float line_gap = 1.5f * std::max(a.y1 - a.y0, b.y1 - b.y0);
                    if ((rows_overlap && gap <= kTouch) || (same_line && gap <= line_gap))
                        hud_group_[root(i)] = root(j);
                }
        }
        group_x0_.assign(n, 1e30f);
        group_x1_.assign(n, -1e30f);
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t r = root(i);
            group_x0_[r] = std::min(group_x0_[r], hud_items_[i].x0);
            group_x1_[r] = std::max(group_x1_[r], hud_items_[i].x1);
        }
        for (std::size_t i = 0; i < n; ++i) {
            float anchor = 320.0f;
            if (hud_edges_) {
                const std::size_t r = root(i);
                const float cx = 0.5f * (group_x0_[r] + group_x1_[r]);
                anchor = cx < 640.0f / 3.0f ? group_x0_[r] : cx > 1280.0f / 3.0f ? group_x1_[r] : 320.0f;
            }
            const auto& p = *hud_items_[i].poly;
            for (std::uint32_t v = 0; v < p.count; ++v) {
                auto& vx = frame.vertices[p.first + v];
                vx.x = anchor + (vx.x - anchor) * k;
            }
            ++hud_corrected_;
        }
    }

    // --- the in-game menu (docs/FRONTEND.md) ---
    bool wants_overlay() override {
#ifdef DREAMCOMP_WITH_FRONTEND
        const char* off = std::getenv("DREAMCOMP_NO_OVERLAY");
        if (off && *off && *off != '0')
            return false;
        if (!overlay_) {
            frontend::OverlayContext oc;
            oc.settings = &g_settings;
            oc.port = g_port;
            oc.config = config_path_;
            oc.exe_dir = exe_dir_;
            oc.version = version_line();
            oc.applied = [this] { apply_live(); };
            overlay_ = std::make_unique<frontend::Overlay>(oc);
        }
        return true;
#else
        return false;
#endif
    }
#ifdef DREAMCOMP_WITH_FRONTEND
    bool on_event(const void* ev) override { return overlay_ && overlay_->on_event(ev); }
    bool overlay_open() override { return overlay_ && overlay_->is_open(); }
    const std::uint32_t* overlay_image(unsigned vw, unsigned vh, bool& changed) override {
#ifdef DREAMCOMP_WITH_FRONTEND
        if (overlay_)
            return overlay_->render_window(vw, vh, changed);
#endif
        (void)vw, (void)vh;
        changed = false;
        return nullptr;
    }

    void draw_overlay(std::uint32_t* rgba, unsigned w, unsigned h, unsigned vw,
                      unsigned vh) override {
        if (overlay_)
            overlay_->draw(rgba, w, h, vw, vh);
    }
#endif

    // After Apply in the in-game menu: what can change without a restart does so now (fit, HUD,
    // rumble, fullscreen). Everything else is marked "Next start" in the menu.
    void apply_live() {
        hud_fix_ = g_settings.get_bool("hud_fix", true);
        hud_edges_ = g_settings.get("hud_layout", "edges") != "center";
        apply_presentation();
        auto& hc = dream::host::host_controls();
        if (hc.set_rumble)
            hc.set_rumble(static_cast<float>(std::clamp(g_settings.get_int("rumble", 100), 0, 100)) /
                          100.0f);
        if (hc.set_volume)
            hc.set_volume(static_cast<float>(std::clamp(g_settings.get_int("volume", 100), 0, 100)) /
                          100.0f);
        if (hc.set_soft_clip)
            hc.set_soft_clip(g_settings.get("clip", "hard") == "soft");
        if (hc.set_fullscreen && hc.fullscreen &&
            hc.fullscreen() != g_settings.get_bool("fullscreen", false))
            hc.set_fullscreen(g_settings.get_bool("fullscreen", false));
        launch_dirty_ = false;  // the file now holds what is in effect
        std::printf("dreamcomp: applied in game: fit %s, hud %s/%s, rumble %d%%, fullscreen %s\n",
                    g_settings.get("fit", "crop").c_str(), hud_fix_ ? "on" : "off",
                    hud_edges_ ? "edges" : "center", g_settings.get_int("rumble", 100),
                    g_settings.get_bool("fullscreen", false) ? "on" : "off");
    }

    void on_stop(dream::System& sys, const char* why) override {
#ifdef DREAMCOMP_WITH_FRONTEND
        overlay_.reset();
#endif
        if (hud_corrected_)
            std::printf("dreamcomp: widescreen HUD: %llu primitives corrected\n",
                        static_cast<unsigned long long>(hud_corrected_));
        (void)sys, (void)why;
        if (save_on_exit_ || (g_settings.dirty() && !launch_dirty_))
            g_settings.save();
    }

private:
    static bool widescreen() { return target_aspect() > 4.0f / 3.0f + 0.01f; }

    static void apply_presentation() {
#ifdef DREAMCOMP_HAS_PRESENTER
        auto& o = dream::render::vk::present_options();
        const std::string fit = g_settings.get("fit", "crop");
        o.fit = fit == "letterbox" ? dream::render::vk::PresentOptions::Fit::Letterbox
                : fit == "stretch" ? dream::render::vk::PresentOptions::Fit::Stretch
                                   : dream::render::vk::PresentOptions::Fit::Crop;
        // The render target already has the target's shape (--render-aspect): show it as is.
        o.display_aspect = 0.0f;
#endif
    }

    bool save_on_exit_ = false;
    std::filesystem::path config_path_, exe_dir_;
    std::uint64_t vblanks_ = 0;
#ifdef DREAMCOMP_WITH_FRONTEND
    std::unique_ptr<frontend::Overlay> overlay_;
#endif
    bool hud_fix_ = true, hud_edges_ = true;
    struct HudItem {
        const dream::render::Polygon* poly;
        float x0, x1, y0, y1;
    };
    std::vector<HudItem> hud_items_;
    std::vector<std::size_t> hud_group_;
    std::vector<float> group_x0_, group_x1_;
    std::unordered_map<float, unsigned> depth_counts_;
    std::uint64_t hud_corrected_ = 0;
    bool launch_dirty_ = false;
};

Core g_core;
dream::host::Register g_reg_core(g_core);

}  // namespace

const PortInfo* port() { return g_port; }

void set_port_version(const char* version) { port_version_ref() = version ? version : ""; }
Settings& settings() { return g_settings; }

RegisterPort::RegisterPort(const PortInfo& info) { g_port = &info; }

}  // namespace dreamcomp
