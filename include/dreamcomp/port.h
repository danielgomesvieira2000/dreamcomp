// What a port tells dreamcomp about its game. One registration per executable, from the port's
// own sources:
//
//     static const dreamcomp::PortInfo kInfo = [] {
//         dreamcomp::PortInfo p;
//         p.id = "soulcalibur";
//         p.title = "Soulcalibur";
//         p.widescreen = &apply_widescreen;
//         return p;
//     }();
//     static dreamcomp::RegisterPort reg(kInfo);
//
// Every field is optional except id/title; a missing capability simply hides that option.
#pragma once

#include <cstdint>

namespace dream {
class System;
}

namespace dreamcomp {

class Settings;

struct PortInfo {
    const char* id = nullptr;     // settings folder name, e.g. "soulcalibur"
    const char* title = nullptr;  // shown in the window title and menus
    unsigned players = 4;         // player cards in the Controls tab (1..4, Maple ports A-D)

    // Widescreen. Called every vblank on the guest thread with the target aspect ratio (4/3 means
    // off: restore the original), so it can re-assert values the game recomputes. `anamorphic`
    // means the game still renders into its 640x480 framebuffer with a horizontally squeezed
    // view: the engine then draws into a render target of the target's shape (--render-aspect),
    // which undoes the squeeze at full horizontal resolution. `max_aspect` caps what the port has
    // been verified with.
    void (*widescreen)(dream::System& sys, float aspect) = nullptr;
    bool widescreen_anamorphic = true;
    float max_aspect = 32.0f / 9.0f;

    // Widescreen HUD correction (anamorphic ports). The game draws its 2D HUD in 640x480 screen
    // space, so the horizontal stretch that widens the 3D view also widens the HUD. Primitives
    // the rule matches are scaled back around the screen centre: the HUD keeps its original 4:3
    // layout and proportions, centred. Measured per game (docs/HUD.md); off when !enabled.
    // The rule: a sprite (PCW para type 5) drawn at a single depth is HUD when another sprite in
    // the same frame shares that exact depth (2D layers are drawn at a common 1/w; 3D particles
    // each have their own), or when its depth is at least `overlay_z` (a fixed overlay layer).
    struct HudRule {
        bool enabled = false;
        unsigned min_shared = 2;      // sprites at one depth for that depth to count as a 2D layer
        float overlay_z = 1000.0f;    // 1/w at or above this is always a 2D overlay
        float full_width = 600.0f;    // wider than this: a fade or flash covering the screen; left
    };
    HudRule hud;

    // Anything else the port wants each vblank (cheats, debug overlays...).
    void (*on_vblank)(dream::System& sys, Settings& settings) = nullptr;
    // Once, before the first guest instruction.
    void (*on_start)(dream::System& sys, Settings& settings) = nullptr;
};

const PortInfo* port();
Settings& settings();

struct RegisterPort {
    explicit RegisterPort(const PortInfo& info);
};

}  // namespace dreamcomp
