// The dreamcomp core launcher extension: settings, bare-launch defaults, widescreen and
// presentation. Linked into every port executable (cmake/DreamcompPort.cmake).
//
// Flags it adds (`--help` lists them under "dreamcomp"):
//   --widescreen / --no-widescreen   override the saved setting for this run
//   --fit letterbox|crop|stretch     how the picture fills the window
//   --set KEY=VALUE                  override any setting for this run
//   --save-settings                  write this run's overrides back to the settings file
//   --settings FILE                  use FILE instead of the per-user settings file
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

#if __has_include("dream/render/vk/present.h") && defined(DREAM_WITH_RENDERER)
#include "dream/render/vk/present.h"
#define DREAMCOMP_HAS_PRESENTER 1
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
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

class Core final : public dream::host::Extension {
public:
    const char* name() const override { return "dreamcomp"; }

    void adjust_args(std::vector<std::string>& args) override {
        const bool bare = args.size() == 1;
        const std::string id = g_port && g_port->id ? g_port->id : "dreamcomp";
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
        if (!has_flag(args, "--disc")) {
            const std::string disc = g_settings.get("disc");
            if (!disc.empty()) {
                args.push_back("--disc");
                args.push_back(disc);
            }
        }
        if (bare) {
            // Double-clicked: play, with the player's saved choices.
            args.push_back("--window");
            args.push_back("--scale");
            args.push_back(std::to_string(std::clamp(g_settings.get_int("scale", 2), 1, 4)));
            const std::string vmu = g_settings.get(
                "vmu", (default_config_dir(id) / "vmu_a1.bin").string());
            args.push_back("--vmu");
            args.push_back(vmu);
        }
    }

    int parse_arg(int i, int argc, char** argv) override {
        const char* a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[i + 1] : nullptr; };
        if (!std::strcmp(a, "--widescreen")) {
            g_settings.set_bool("widescreen", true);
            return 1;
        }
        if (!std::strcmp(a, "--no-widescreen")) {
            g_settings.set_bool("widescreen", false);
            return 1;
        }
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

    void usage(std::FILE* out) override {
        std::fprintf(out,
                     "\ndreamcomp:\n"
                     "  --widescreen / --no-widescreen  override the saved widescreen setting\n"
                     "  --fit MODE             letterbox, crop or stretch (default crop: no bars)\n"
                     "  --set KEY=VALUE        override one setting for this run\n"
                     "  --save-settings        keep this run's overrides\n"
                     "  --settings FILE        settings file (default: %s)\n"
                     "  Started with no arguments, the game opens in a window with the saved\n"
                     "  settings (disc, scale, memory card).\n",
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
                    widescreen() ? ", widescreen" : "");
    }

    void on_vblank(dream::System& sys) override {
        if (g_port && g_port->widescreen)
            g_port->widescreen(sys, widescreen());
        if (g_port && g_port->on_vblank)
            g_port->on_vblank(sys, g_settings);
    }

    void on_stop(dream::System& sys, const char* why) override {
        (void)sys, (void)why;
        if (save_on_exit_ || (g_settings.dirty() && !launch_dirty_))
            g_settings.save();
    }

private:
    static bool widescreen() {
        return g_port && g_port->widescreen && g_settings.get_bool("widescreen", false);
    }

    static void apply_presentation() {
#ifdef DREAMCOMP_HAS_PRESENTER
        auto& o = dream::render::vk::present_options();
        const std::string fit = g_settings.get("fit", "crop");
        o.fit = fit == "letterbox" ? dream::render::vk::PresentOptions::Fit::Letterbox
                : fit == "stretch" ? dream::render::vk::PresentOptions::Fit::Stretch
                                   : dream::render::vk::PresentOptions::Fit::Crop;
        o.display_aspect =
            widescreen() && g_port->widescreen_anamorphic ? g_port->widescreen_aspect : 0.0f;
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
