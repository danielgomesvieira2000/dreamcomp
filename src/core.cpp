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
//   --launcher-screenshot FILE.png   render the launcher's tabs to PNGs and exit
//   --launcher-tab NAME              the tab to open / to capture
//   --launcher-size WxH              the launcher window's size (default 1280x720)
// Settings it turns into engine flags: texture_pack, dump_textures (docs/TEXTURE-PACKS.md),
// rumble (--rumble), mods (--mod, docs/MODS.md), aspect, fullscreen, scale.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "dream/runtime/host_ext.h"
#include "dream/runtime/system.h"
#include "dreamcomp/port.h"
#include "dreamcomp/settings.h"

#ifdef DREAMCOMP_WITH_FRONTEND
#include "dreamcomp/frontend.h"
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

// `rumble` (0-100, default 100) -> --rumble N; `mods = a,b` -> --mod <config dir>/mods/a ...
// (first wins, docs/MODS.md). A flag the user passed wins over the setting.
void add_input_and_mod_flags(std::vector<std::string>& args,
                             const std::filesystem::path& config_dir) {
    if (!has_flag(args, "--rumble") && !g_settings.get("rumble").empty()) {
        args.push_back("--rumble");
        args.push_back(std::to_string(std::clamp(g_settings.get_int("rumble", 100), 0, 100)));
    }
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
        const std::string shot_tab = flag_value(args, "--launcher-tab");
        const std::string shot_size = flag_value(args, "--launcher-size");
        drop_flag(args, "--launcher", false);
        drop_flag(args, "--no-launcher", false);
        drop_flag(args, "--launcher-screenshot", true);
        drop_flag(args, "--launcher-tab", true);
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
                 key == "fullscreen" || key == "scale" || key == "rumble" || key == "mods"))
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
                     "  --launcher-screenshot FILE.png  write the launcher's tabs as PNGs, exit\n"
                     "  --launcher-tab NAME    play, graphics, controls, enhancements or about\n"
                     "  --launcher-size WxH    launcher window size (default 1280x720)\n"
                     "  Settings rumble=0..100 and mods=a,b add --rumble and --mod\n"
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
        apply_presentation();
        if (g_port && g_port->on_start)
            g_port->on_start(sys, g_settings);
        std::printf("dreamcomp: %s, settings %s%s\n", g_port && g_port->title ? g_port->title : "?",
                    g_settings.file().string().c_str(),
                    widescreen() ? (", aspect " + g_settings.get("aspect", "16:9")).c_str() : "");
    }

    void on_vblank(dream::System& sys) override {
        if (g_port && g_port->widescreen)
            g_port->widescreen(sys, widescreen() ? target_aspect() : 4.0f / 3.0f);
        if (g_port && g_port->on_vblank)
            g_port->on_vblank(sys, g_settings);
    }

    void on_stop(dream::System& sys, const char* why) override {
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
    bool launch_dirty_ = false;
};

Core g_core;
dream::host::Register g_reg_core(g_core);

}  // namespace

const PortInfo* port() { return g_port; }
Settings& settings() { return g_settings; }

RegisterPort::RegisterPort(const PortInfo& info) { g_port = &info; }

}  // namespace dreamcomp
