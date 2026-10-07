// The in-game HUD editor: see hud_editor.h and docs/HUD.md.
#include "hud_editor.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

namespace dreamcomp::frontend {

namespace {

// The page: transparent over the game, outlines placed absolutely, a small panel top left.
const char* kDocument = R"(<rml>
<head>
<title>HUD editor</title>
<style>
body { width: 100%; height: 100%; margin: 0; padding: 0; font-family: Inter; font-size: 15dp; color: #e8ecf4; }
#boxes { position: absolute; left: 0; top: 0; width: 100%; height: 100%; }
.box { position: absolute; border-width: 2px; }
.box.hud.left { border-color: #4aa3ff; background-color: #4aa3ff22; }
.box.hud.center { border-color: #48d17a; background-color: #48d17a22; }
.box.hud.right { border-color: #ff6b6b; background-color: #ff6b6b22; }
.box.hud.stretch { border-color: #ffd84a; background-color: #ffd84a22; }
.box.piece { border-width: 1px; border-color: #c8c8c870; }
.box.piece:hover { border-color: #ffffffd0; background-color: #ffffff20; }
.box.hud:hover { background-color: #ffffff40; }
.tag { position: absolute; left: 0; top: -17dp; font-size: 12dp; font-weight: bold; padding: 0 4dp;
       background-color: #000000b0; pointer-events: none; }
.box.hud.left .tag { color: #4aa3ff; }
.box.hud.center .tag { color: #48d17a; }
.box.hud.right .tag { color: #ff6b6b; }
.box.hud.stretch .tag { color: #ffd84a; }
#panel { position: absolute; width: 420dp; padding: 10dp 14dp; cursor: move;
         background-color: #0b0f18e8; border-width: 1px; border-color: #3a4560; }
#pausebadge { display: block; color: #ff9f43; font-weight: bold; }
#panel h1 { font-size: 18dp; font-weight: bold; margin-bottom: 4dp; display: block; }
#panel p { display: block; margin: 3dp 0; line-height: 19dp; }
.key { color: #9fb3d9; }
.l { color: #4aa3ff; } .c { color: #48d17a; } .r { color: #ff6b6b; } .s { color: #ffd84a; } .g { color: #c8c8c8; }
#status { color: #ffd84a; }
.buttons { display: block; margin-top: 8dp; }
button { display: inline-block; padding: 4dp 14dp; margin-right: 8dp; border-width: 1px; border-color: #5a6a90;
         background-color: #1a2236; color: #e8ecf4; }
button:hover { background-color: #2c3a5c; }
</style>
</head>
<body>
<div id="boxes"></div>
<div id="panel">
<h1>HUD editor</h1>
<p id="pausebadge"></p>
<p id="legend"></p>
<p id="counts"></p>
<p id="status"></p>
<p><span class="key">Left click</span>: anchor <span class="l">left</span> &gt; <span class="c">centre</span> &gt;
<span class="r">right</span> &gt; <span class="s">stretched</span>. <span class="key">Right click</span>: back to automatic.
<span class="key">Click a grey outline</span>: add it to the HUD.</p>
<p><span class="key">Space</span> pause / resume the game (edit the HUD on screen), <span class="key">S</span> save,
<span class="key">drag</span> this panel to move it (<span class="key">P</span>: next corner), <span class="key">F1</span> / <span class="key">Esc</span> close.
Saved overrides go to your settings folder; <span class="key">tools/hud_promote.py</span> moves them into the port.</p>
<div class="buttons"><button id="pause">Pause</button><button id="save">Save</button><button id="close">Close</button></div>
</div>
</body>
</rml>)";

std::string fmt(const char* f, double a, double b, double c, double d) {
    char buf[160];
    std::snprintf(buf, sizeof buf, f, a, b, c, d);
    return buf;
}

std::string escape(const std::string& s) {
    std::string out;
    for (char ch : s) {
        if (ch == '<')
            out += "&lt;";
        else if (ch == '>')
            out += "&gt;";
        else if (ch == '&')
            out += "&amp;";
        else
            out += ch;
    }
    return out;
}

const char* letter(hud::Anchor a) {
    switch (a) {
        case hud::Anchor::Left: return "L";
        case hud::Anchor::Center: return "C";
        case hud::Anchor::Right: return "R";
        case hud::Anchor::Stretch: return "S";
        default: return "A";
    }
}

}  // namespace

HudEditor::HudEditor(const OverlayContext& ctx, Post post, std::function<void()> close)
    : ctx_(ctx), post_(std::move(post)), close_(std::move(close)) {}

HudEditor::~HudEditor() {
    if (doc_) {
        doc_->RemoveEventListener(Rml::EventId::Mousedown, this);
        doc_->RemoveEventListener(Rml::EventId::Click, this);
    }
}

bool HudEditor::load(Rml::Context* ctx, std::string* error) {
    rml_ = ctx;
    doc_ = rml_->LoadDocumentFromMemory(kDocument, "hud_editor.rml");
    if (!doc_) {
        if (error)
            *error = "cannot build the HUD editor page";
        return false;
    }
    doc_->AddEventListener(Rml::EventId::Mousedown, this);
    doc_->AddEventListener(Rml::EventId::Click, this);
    doc_->Hide();
    return true;
}

void HudEditor::show() {
    if (!doc_)
        return;
    visible_ = true;
    have_snap_ = false;
    last_pull_ms_ = 0;
    if (ctx_.hud_watch)
        ctx_.hud_watch(true);
    doc_->Show();
    place_panel();
    rebuild();
}

// HUDs live at the screen's edges and corners: the panel goes in whichever corner the player
// picks (P), bottom left at first.
void HudEditor::place_panel() {
    Rml::Element* p = doc_ ? doc_->GetElementById("panel") : nullptr;
    if (!p)
        return;
    if (dragged_) {
        p->SetProperty("left", fmt("%.0fpx", panel_x_, 0, 0, 0));
        p->SetProperty("top", fmt("%.0fpx", panel_y_, 0, 0, 0));
        p->SetProperty("right", "auto");
        p->SetProperty("bottom", "auto");
        redraw_ = true;
        return;
    }
    const bool right = corner_ == 1 || corner_ == 3, bottom = corner_ >= 2;
    p->SetProperty("left", right ? "auto" : "12dp");
    p->SetProperty("right", right ? "12dp" : "auto");
    p->SetProperty("top", bottom ? "auto" : "12dp");
    p->SetProperty("bottom", bottom ? "12dp" : "auto");
    redraw_ = true;
}

void HudEditor::set_paused(bool on) {
    if (on == paused_)
        return;
    paused_ = on;
    if (ctx_.hud_pause)
        ctx_.hud_pause(on);
    std::printf("hud editor: game %s\n", on ? "paused" : "resumed");
    show_pause();
    last_pull_ms_ = 0;
}

void HudEditor::show_pause() {
    if (!doc_)
        return;
    if (auto* e = doc_->GetElementById("pausebadge"))
        e->SetInnerRML(paused_ ? "GAME PAUSED: edit the HUD on screen (Space resumes)" : "");
    if (auto* e = doc_->GetElementById("pause"))
        e->SetInnerRML(paused_ ? "Resume" : "Pause");
    redraw_ = true;
}

void HudEditor::hide() {
    if (!doc_)
        return;
    visible_ = false;
    dragging_ = false;
    set_paused(false);
    if (ctx_.hud_watch)
        ctx_.hud_watch(false);
    doc_->Hide();
    redraw_ = true;
}

void HudEditor::set_window(float w, float h) {
    frame_mode_ = false;
    win_w_ = w;
    win_h_ = h;
    redraw_ = true;
    rebuild();
}

void HudEditor::set_frame(float x, float y, float w, float h) {
    frame_mode_ = true;
    fx_ = x;
    fy_ = y;
    fw_ = w;
    fh_ = h;
    redraw_ = true;
    rebuild();
}

// Where the 640x480 space is drawn: the picture letterboxed into the window by its own shape (the
// presenter never stretches or crops: Core::apply_presentation), or the frame given.
void HudEditor::image_rect(float& x, float& y, float& w, float& h) const {
    if (frame_mode_) {
        x = fx_;
        y = fy_;
        w = fw_;
        h = fh_;
        return;
    }
    const float a = snap_.image_aspect > 0.0f ? snap_.image_aspect : 4.0f / 3.0f;
    if (win_w_ / std::max(1.0f, win_h_) > a) {
        h = win_h_;
        w = win_h_ * a;
    } else {
        w = win_w_;
        h = win_w_ / a;
    }
    x = (win_w_ - w) * 0.5f;
    y = (win_h_ - h) * 0.5f;
}

void HudEditor::rebuild() {
    if (!doc_ || !visible_)
        return;
    Rml::Element* boxes = doc_->GetElementById("boxes");
    if (!boxes)
        return;
    float ix, iy, iw, ih;
    image_rect(ix, iy, iw, ih);
    // Big outlines first, so smaller ones sit on top and stay clickable.
    std::vector<std::size_t> order(snap_.boxes.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const auto& A = snap_.boxes[a];
        const auto& B = snap_.boxes[b];
        if (A.hud != B.hud)
            return !A.hud;  // grey pieces below the HUD
        return (A.x1 - A.x0) * (A.y1 - A.y0) > (B.x1 - B.x0) * (B.y1 - B.y0);
    });
    std::string rml;
    std::size_t hud_n = 0, piece_n = 0;
    for (std::size_t i : order) {
        const hud::Box& b = snap_.boxes[i];
        // An element none of whose pieces is drawn this frame has no bounds (+-inf): nothing to
        // outline (it printed "left: -infpx" to the log).
        if (!std::isfinite(b.x0) || !std::isfinite(b.x1) || !std::isfinite(b.y0) || !std::isfinite(b.y1))
            continue;
        const float x = ix + b.x0 / 640.0f * iw, y = iy + b.y0 / 480.0f * ih;
        const float w = std::max(4.0f, (b.x1 - b.x0) / 640.0f * iw);
        const float h = std::max(4.0f, (b.y1 - b.y0) / 480.0f * ih);
        if (b.hud) {
            ++hud_n;
            // Ids in drawing order, for scripted tests (DREAMCOMP_OVERLAY_KEYS=...,click:hud-0).
            rml += "<div id=\"hud-" + std::to_string(hud_n - 1) + "\" class=\"box hud " +
                   std::string(hud::name(b.anchor)) + "\" data-box=\"" + std::to_string(i) + "\" style=\"" +
                   fmt("left: %.0fpx; top: %.0fpx; width: %.0fpx; height: %.0fpx;", x, y, w, h) +
                   "\"><span class=\"tag\">" + letter(b.anchor) + (b.override_index >= 0 ? "*" : "") +
                   "</span></div>";
        } else {
            ++piece_n;
            rml += "<div id=\"piece-" + std::to_string(piece_n - 1) + "\" class=\"box piece\" data-box=\"" +
                   std::to_string(i) + "\" style=\"" +
                   fmt("left: %.0fpx; top: %.0fpx; width: %.0fpx; height: %.0fpx;", x, y, w, h) +
                   "\"></div>";
        }
    }
    boxes->SetInnerRML(rml);
    if (auto* e = doc_->GetElementById("legend"))
        e->SetInnerRML(std::string("<span class=\"l\">L</span> left, <span class=\"c\">C</span> centre, "
                                   "<span class=\"r\">R</span> right, <span class=\"s\">S</span> stretched, "
                                   "* overridden, <span class=\"g\">grey</span>: 2D piece not in the HUD. ") +
                       (snap_.image_aspect < 4.0f / 3.0f + 0.01f
                            ? "The picture is 4:3: nothing is moved; these anchors apply with the Expanded aspect ratio."
                            : snap_.edges ? "HUD layout: Expanded (edges)."
                                          : "HUD layout: Original (centred 4:3): anchors apply with layout Expanded."));
    if (auto* e = doc_->GetElementById("counts"))
        e->SetInnerRML(have_snap_ ? std::to_string(hud_n) + " elements, " + std::to_string(piece_n) +
                                        " other 2D pieces; overrides: " + std::to_string(snap_.port_overrides) +
                                        " from the port, " + std::to_string(snap_.user_overrides) + " yours" +
                                        (snap_.unsaved ? " (unsaved)" : "")
                                  : std::string("waiting for a frame..."));
    if (auto* e = doc_->GetElementById("status"))
        e->SetInnerRML(escape(snap_.status));
    redraw_ = true;
}

void HudEditor::tick() {
    if (!visible_ || !ctx_.hud_snapshot)
        return;
    const std::uint64_t now = SDL_GetTicks();
    if (have_snap_ && now - last_pull_ms_ < 150)
        return;  // a fresh outline set ~7 times a second is plenty for editing
    last_pull_ms_ = now;
    hud::Snapshot s;
    if (!ctx_.hud_snapshot(s) || (have_snap_ && s.frame == snap_.frame))
        return;
    snap_ = std::move(s);
    have_snap_ = true;
    rebuild();
}

bool HudEditor::handle(const SDL_Event& ev, const MouseMap& map) {
    if (!visible_)
        return false;
    switch (ev.type) {
        case SDL_EVENT_KEY_DOWN:
            if (ev.key.repeat)
                return true;
            if (ev.key.key == SDLK_F1 || ev.key.key == SDLK_ESCAPE) {
                close_();
                return true;
            }
            if (ev.key.key == SDLK_P) {
                dragged_ = false;
                corner_ = (corner_ + 1) % 4;
                place_panel();
                return true;
            }
            if (ev.key.key == SDLK_SPACE) {
                set_paused(!paused_);
                return true;
            }
            if (ev.key.key == SDLK_S) {
                if (ctx_.hud_save)
                    post_([save = ctx_.hud_save] { save(); });
                last_pull_ms_ = 0;
                return true;
            }
            return true;
        case SDL_EVENT_MOUSE_MOTION: {
            float cx, cy;
            if (map(ev.motion.x, ev.motion.y, cx, cy)) {
                mouse_x_ = cx;
                mouse_y_ = cy;
                if (dragging_) {
                    panel_x_ = cx - grab_x_;
                    panel_y_ = cy - grab_y_;
                    place_panel();
                } else if (rml_) {
                    rml_->ProcessMouseMove(static_cast<int>(cx), static_cast<int>(cy), 0);
                }
            }
            redraw_ = true;
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            // A left press on the panel itself (not a button) starts dragging it.
            Rml::Element* panel = doc_ ? doc_->GetElementById("panel") : nullptr;
            Rml::Element* over = rml_ ? rml_->GetHoverElement() : nullptr;
            bool on_panel = false, on_button = false;
            for (Rml::Element* e = over; e; e = e->GetParentNode()) {
                if (e->GetTagName() == "button")
                    on_button = true;
                if (e == panel)
                    on_panel = true;
            }
            if (ev.button.button == SDL_BUTTON_LEFT && on_panel && !on_button) {
                const Rml::Vector2f at = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
                grab_x_ = mouse_x_ - at.x;
                grab_y_ = mouse_y_ - at.y;
                panel_x_ = at.x;
                panel_y_ = at.y;
                dragging_ = dragged_ = true;
                return true;
            }
            if (rml_)
                rml_->ProcessMouseButtonDown(ev.button.button == SDL_BUTTON_RIGHT ? 1 : 0, 0);
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (dragging_ && ev.button.button == SDL_BUTTON_LEFT) {
                dragging_ = false;
                return true;
            }
            if (rml_)
                rml_->ProcessMouseButtonUp(ev.button.button == SDL_BUTTON_RIGHT ? 1 : 0, 0);
            return true;
        default:
            return false;
    }
}

void HudEditor::ProcessEvent(Rml::Event& ev) {
    Rml::Element* e = ev.GetTargetElement();
    if (ev.GetId() == Rml::EventId::Click) {
        for (; e; e = e->GetParentNode()) {
            if (e->GetId() == "save") {
                if (ctx_.hud_save)
                    post_([save = ctx_.hud_save] { save(); });
                last_pull_ms_ = 0;
                return;
            }
            if (e->GetId() == "close") {
                close_();
                return;
            }
            if (e->GetId() == "pause") {
                set_paused(!paused_);
                return;
            }
        }
        return;
    }
    // Mousedown on an outline: left or right button.
    const int button = ev.GetParameter<int>("button", 0);
    for (; e; e = e->GetParentNode()) {
        const Rml::Variant* attr = e->GetAttribute("data-box");
        if (!attr)
            continue;
        const std::size_t i = static_cast<std::size_t>(attr->Get<int>());
        if (i >= snap_.boxes.size() || !ctx_.hud_edit)
            return;
        const hud::Box box = snap_.boxes[i];
        hud::Edit edit;
        if (box.hud)
            edit = button == 1 ? hud::Edit::Reset : hud::Edit::Cycle;
        else if (button == 0)
            edit = hud::Edit::Add;
        else
            return;
        std::printf("hud editor: %s %s element at %.0f,%.0f-%.0f,%.0f\n",
                    edit == hud::Edit::Cycle ? "cycle" : edit == hud::Edit::Reset ? "reset" : "add",
                    hud::name(box.anchor), box.ox0, box.oy0, box.ox1, box.oy1);
        post_([fn = ctx_.hud_edit, box, edit] { fn(box, edit); });
        last_pull_ms_ = 0;  // show the result at the next tick
        return;
    }
}

}  // namespace dreamcomp::frontend
