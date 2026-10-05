// The launcher: the window a player sees when they double-click a port's executable (or pass
// --launcher). Pick and verify the disc, choose graphics/controls/enhancement settings, then
// Start Game. Built only when SDL3 is available (cmake/DreamcompFrontend.cmake), in which case
// DREAMCOMP_WITH_FRONTEND is defined for dreamcomp_core. Usage and customisation: docs/FRONTEND.md.
#pragma once

#include <filesystem>
#include <string>

namespace dreamcomp {

class Settings;
struct PortInfo;

namespace frontend {

struct LaunchContext {
    Settings* settings = nullptr;          // edited on Apply / Start Game, saved to its file
    const PortInfo* port = nullptr;        // title, widescreen capability
    std::filesystem::path config;          // the game's TOML ([game] title/product, [disc] sha1)
    std::filesystem::path exe_dir;         // frontend/ assets live here
    std::filesystem::path screenshot;      // non-empty: render, write PNG(s), no interaction
    std::string tab;                       // first tab shown: play|graphics|controls|enhancements|about
    int width = 1280, height = 720;        // window (and screenshot) size
};

enum class Outcome {
    Start,  // settings applied and saved: continue into the game
    Quit,   // the player closed the launcher
    Error,  // could not open (no assets, no display): `error` says why; start the game as before
};

// Runs the launcher to completion. The SDL window, renderer and every SDL subsystem the launcher
// initialised are gone when it returns, so the engine can open its own Vulkan window.
Outcome run_launcher(const LaunchContext& ctx, std::string* error);

}  // namespace frontend
}  // namespace dreamcomp
