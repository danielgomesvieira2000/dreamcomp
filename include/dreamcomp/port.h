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

    // Widescreen. Called every vblank on the guest thread with the target aspect ratio (4/3 means
    // off: restore the original), so it can re-assert values the game recomputes. `anamorphic`
    // means the game still renders into its 640x480 framebuffer with a horizontally squeezed
    // view: the engine then draws into a render target of the target's shape (--render-aspect),
    // which undoes the squeeze at full horizontal resolution. `max_aspect` caps what the port has
    // been verified with.
    void (*widescreen)(dream::System& sys, float aspect) = nullptr;
    bool widescreen_anamorphic = true;
    float max_aspect = 32.0f / 9.0f;

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
