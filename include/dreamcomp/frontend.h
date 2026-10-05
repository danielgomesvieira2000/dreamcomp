// The launcher: the window a player sees when they double-click a port's executable (or pass
// --launcher). Pick and verify the disc, choose graphics/controls/enhancement settings, then
// Start Game. Built only when SDL3 is available (cmake/DreamcompFrontend.cmake), in which case
// DREAMCOMP_WITH_FRONTEND is defined for dreamcomp_core. Usage and customisation: docs/FRONTEND.md.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace dreamcomp {

class Settings;
struct PortInfo;

// The port's own version, shown in the launcher. dreamcomp_add_port() generates a source file that
// calls this from a static initialiser with the port project's VERSION. Defined in core.cpp.
void set_port_version(const char* version);

namespace frontend {

struct LaunchContext {
    Settings* settings = nullptr;          // edited on Apply / Start Game, saved to its file
    const PortInfo* port = nullptr;        // title, widescreen capability
    std::filesystem::path config;          // the game's TOML ([game] title/product, [disc] sha1)
    std::filesystem::path exe_dir;         // frontend/ assets live here
    std::filesystem::path screenshot;      // non-empty: render, write PNG(s), no interaction
    std::string tab;                       // screen: launcher | settings/<general|controls|graphics|sound|mods>
    std::string version;                   // shown bottom-left: "dreamcomp 0.1.0 · Soulcalibur 0.1.0"
    int width = 1280, height = 720;        // window (and screenshot) size
};

enum class Outcome {
    Start,  // settings applied and saved: continue into the game
    Quit,   // the player closed the launcher
    Error,  // could not open (no assets, no display): `error` says why; start the game as before
};

// The same menu over the running game (Escape / pad Select toggles it). Created by the core
// extension when the engine opens its window; everything runs on the guest thread.
struct OverlayContext {
    Settings* settings = nullptr;
    const PortInfo* port = nullptr;
    std::filesystem::path config, exe_dir;
    std::string version;
    std::function<void()> applied;  // after Apply: re-read the settings that apply live
};

class Overlay {
public:
    explicit Overlay(const OverlayContext& ctx);
    ~Overlay();
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    bool on_event(const void* sdl_event);  // true: the overlay used it (host_ext.h)
    bool is_open() const;
    void draw(std::uint32_t* rgba, unsigned w, unsigned h, unsigned view_w, unsigned view_h);
    // The menu rendered at the window's own size (premultiplied RGBA), for the engine to blend
    // over the game on the GPU. `changed`: it was redrawn since the last call. Null while closed
    // (or while a test screenshot of the composited frame is pending: draw() takes that one).
    const std::uint32_t* render_window(unsigned view_w, unsigned view_h, bool& changed);
    // Each vblank (test script: DREAMCOMP_OVERLAY_KEYS). on_event() is called from the window's
    // event poll and draw() from the present; they share a mutex, so they may run on different
    // threads.
    void on_vblank(std::uint64_t frame);

    struct Impl;

private:
    Impl* impl_;
};

// Runs the launcher to completion. The SDL window, renderer and every SDL subsystem the launcher
// initialised are gone when it returns, so the engine can open its own Vulkan window.
Outcome run_launcher(const LaunchContext& ctx, std::string* error);

}  // namespace frontend
}  // namespace dreamcomp
