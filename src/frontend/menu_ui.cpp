#include "menu_ui.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "RmlUi_Platform_SDL.h"
#include "dreamcomp/port.h"
#include "dreamcomp/settings.h"

namespace dreamcomp::frontend {

namespace {

constexpr const char* kTabs[][2] = {{"general", "General"},
                                    {"controls", "Controls"},
                                    {"graphics", "Graphics"},
                                    {"sound", "Sound"},
                                    {"mods", "Mods"}};
constexpr int kTabCount = 5;

// PromptFont code points (PromptFont 1.10 "xbox" set and keyboard keys).
constexpr const char* kGlyphA = "\xE2\x87\x93";      // U+21D3
constexpr const char* kGlyphB = "\xE2\x87\x92";      // U+21D2
constexpr const char* kGlyphLB = "\xE2\x86\x98";     // U+2198
constexpr const char* kGlyphRB = "\xE2\x86\x99";     // U+2199
constexpr const char* kGlyphEnter = "\xE2\x90\xAE";  // U+242E
constexpr const char* kGlyphEsc = "\xE2\x90\xAF";    // U+242F
constexpr const char* kCheck = "\xE2\x9C\x93";
constexpr const char* kCross = "\xE2\x9C\x97";
constexpr const char* kEllipsis = "\xE2\x80\xA6";
constexpr const char* kDot = "  \xC2\xB7  ";

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

std::string u8(const std::filesystem::path& p) {
    const auto s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

std::string url_path(const std::filesystem::path& p) {
    const auto s = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

std::string file_url(const std::filesystem::path& p) {
    std::string s = url_path(p);
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

std::string glyph(const char* g) { return std::string("<span class=\"glyph\">") + g + "</span>"; }
std::string keycap(const char* k) { return std::string("<span class=\"key\">") + k + "</span>"; }
std::string hint(const std::string& what) { return "<span class=\"hint-text\">" + what + "</span>"; }

// ---- navigation helpers ----
Rml::Element* region_of(Rml::Element* e) {
    for (; e; e = e->GetParentNode()) {
        const auto& id = e->GetId();
        if (id == "lmenu" || id == "ptabs" || id == "prows" || id == "pfoot" || id == "modal")
            return e;
    }
    return nullptr;
}

Rml::Rectanglef box_of(Rml::Element* e) {
    const Rml::Vector2f pos = e->GetAbsoluteOffset(Rml::BoxArea::Border);
    return Rml::Rectanglef::FromPositionSize(pos, e->GetBox().GetSize(Rml::BoxArea::Border));
}

long nav_score(const Rml::Rectanglef& from, const Rml::Rectanglef& to,
               Rml::Input::KeyIdentifier key) {
    // Distance along the direction, plus a heavy penalty for lying outside the strip the source
    // projects in that direction (the heuristic RmlUi's own `nav: auto` uses).
    const bool vertical = key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN;
    const int ax = vertical ? 1 : 0, cx = vertical ? 0 : 1;
    const float main = (key == Rml::Input::KI_DOWN || key == Rml::Input::KI_RIGHT)
                           ? to.p0[ax] - from.p1[ax]
                           : from.p0[ax] - to.p1[ax];
    if (main < -1.0f)
        return -1;
    const float cross =
        std::max(0.0f, to.p0[cx] - from.p1[cx]) + std::max(0.0f, from.p0[cx] - to.p1[cx]);
    return static_cast<long>(std::max(main, 0.0f)) + 10000L * static_cast<long>(cross);
}

Rml::Element* nav_search(Rml::Element* region, Rml::Element* from, Rml::Input::KeyIdentifier key) {
    if (!region)
        return nullptr;
    Rml::ElementList list;
    region->QuerySelectorAll(list, ".focusable");
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

const char* kAboutText =
    "A native PC port built with dreamcomp, a framework for Dreamcast recompilation ports: the "
    "game's SH-4 code is translated ahead of time into C++, and the console's graphics and sound "
    "hardware are emulated by the dream-recomp engine.<br/><br/><b>You need your own disc.</b> No "
    "game data is included; the port reads your disc image and checks it is the exact release it "
    "was built from.<br/><br/>dreamcomp by Daniel Gomes Vieira. dream-recomp engine by phobos665, "
    "with portions of Flycast (flyinghead and contributors). Licensed under the GNU General Public "
    "License v2.0.<br/><br/>Interface: RmlUi (MIT), FreeType (FTL / GPL-2.0), SDL3 (zlib). Fonts: "
    "Inter and PromptFont (SIL Open Font License 1.1).<br/><br/>Press Select to open the read-me.";

}  // namespace

// ---- construction -------------------------------------------------------------------------------

MenuUi::MenuUi(const UiConfig& cfg, Mode mode, UiHost host)
    : cfg_(cfg), mode_(mode), host_(std::move(host)), draft_(*cfg.settings) {
    config_dir_ = cfg.settings->file().parent_path();
    if (!cfg.config.empty()) {
        std::string err;
        game_ok_ = dream::translator::load_game_config(cfg.config, game_, err);
        if (!game_ok_)
            game_error_ = err;
    } else {
        game_error_ = "the game's configuration file was not found next to the executable";
    }
    panel_ = mode_ == Mode::InGame;
}

MenuUi::~MenuUi() {
    if (verifying_ && verify_.valid())
        verify_.wait();
    // The document outlives this listener (RmlUi frees it at Shutdown): detach first.
    if (doc_) {
        doc_->RemoveEventListener(Rml::EventId::Click, this);
        doc_->RemoveEventListener(Rml::EventId::Keydown, this);
        doc_->RemoveEventListener(Rml::EventId::Focus, this, true);
        doc_->RemoveEventListener(Rml::EventId::Mouseover, this, true);
    }
}

std::string MenuUi::title() const {
    if (cfg_.port && cfg_.port->title)
        return cfg_.port->title;
    if (game_ok_ && !game_.title.empty())
        return game_.title;
    return "dreamcomp";
}

bool MenuUi::load(Rml::Context* ctx, std::string* error) {
    ctx_ = ctx;
    const auto fe = cfg_.exe_dir / "frontend";
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
    // RmlUi parses a document's source as a URL, where "C:" would be a protocol; it spells drive
    // letters "C|" itself and turns them back when resolving relative links.
    std::string source = url_path(fe / "launcher.rml");
    std::replace(source.begin(), source.end(), ':', '|');
    doc_ = ctx_->LoadDocumentFromMemory(rml, source);
    if (!doc_) {
        *error = "cannot load " + u8(fe / "launcher.rml");
        return false;
    }
    doc_->SetClass("ingame", mode_ == Mode::InGame);
    doc_->AddEventListener(Rml::EventId::Click, this);
    doc_->AddEventListener(Rml::EventId::Keydown, this);
    doc_->AddEventListener(Rml::EventId::Focus, this, true);
    doc_->AddEventListener(Rml::EventId::Mouseover, this, true);
    doc_->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    if (mode_ == Mode::Pregame)
        start_verify(false);
    refresh_pads();
    dirty_ = true;
    return true;
}

std::vector<std::string> MenuUi::all_screens() {
    std::vector<std::string> out{"launcher"};
    for (const auto& t : kTabs) out.push_back(std::string("settings/") + t[0]);
    return out;
}

std::string MenuUi::screen_name() const {
    return panel_ ? std::string("settings/") + kTabs[tab_][0] : std::string("launcher");
}

void MenuUi::show(const std::string& screen) {
    modal_open_ = false;
    if (screen.rfind("settings", 0) == 0) {
        const auto slash = screen.find('/');
        open_panel(slash == std::string::npos ? std::string("general") : screen.substr(slash + 1));
    } else if (mode_ == Mode::Pregame) {
        panel_ = false;
        focus_first_ = true;
        dirty_ = true;
    }
}

void MenuUi::open_panel(const std::string& tab) {
    for (int i = 0; i < kTabCount; ++i)
        if (tab == kTabs[i][0])
            tab_ = i;
    if (!panel_ && mode_ == Mode::Pregame)
        panel_from_ = focus_id();
    panel_ = true;
    focus_first_ = true;
    dirty_ = true;
}

void MenuUi::close_panel() {
    if (modal_open_ && modal_.confirm_quit) {
        modal_open_ = false;
        dirty_ = true;
    }
    if (mode_ == Mode::InGame) {
        if (host_.close_panel)
            host_.close_panel();
        return;
    }
    panel_ = false;
    want_focus_ = panel_from_.empty() ? std::string("l-start") : panel_from_;
    dirty_ = true;
}

// ---- disc -----------------------------------------------------------------------------------------

void MenuUi::start_verify(bool sync) {
    const std::string path = cfg_.settings->get("disc");
    if (path.empty()) {
        disc_ = DiscCheck{};
        verifying_ = false;
        return;
    }
    const std::string expected = game_ok_ ? game_.sha1_1st_read : std::string();
    const auto p = std::filesystem::u8path(path);
    if (sync) {
        disc_ = verify_disc(p, expected);
        verifying_ = false;
        return;
    }
    verifying_ = true;
    verify_ = std::async(std::launch::async, [p, expected] { return verify_disc(p, expected); });
}

void MenuUi::verify_now() {
    if (verifying_ && verify_.valid())
        verify_.wait();
    verifying_ = false;
    start_verify(true);
    dirty_ = true;
}

void MenuUi::disc_picked(const std::string& path) {
    dialog_open_ = false;
    dirty_ = true;
    if (path.empty())
        return;
    // Loading a disc is an action, not a setting: it is checked at once and, when it is the right
    // one, remembered at once. A wrong one is explained in a message and not kept.
    const std::string expected = game_ok_ ? game_.sha1_1st_read : std::string();
    const DiscCheck c = verify_disc(std::filesystem::u8path(path), expected);
    const std::string file = u8(std::filesystem::u8path(path).filename());
    if (disc_playable(c)) {
        disc_ = c;
        cfg_.settings->set("disc", path);
        if (!cfg_.settings->save())
            std::fprintf(stderr, "launcher: could not write %s\n",
                         u8(cfg_.settings->file()).c_str());
        std::printf("launcher: disc loaded: %s\n", path.c_str());
        want_focus_ = "l-start";
        return;
    }
    modal_open_ = true;
    if (c.status == DiscStatus::WrongRevision) {
        modal_ = {"Wrong release or revision",
                  file + " is a different release or revision of " + title() +
                      ". This port is built for one exact version and cannot run another.",
                  "disc&#160;&#160;" + esc(c.found_sha1) + "<br/>expected&#160;&#160;" +
                      esc(c.expected_sha1)};
    } else {
        modal_ = {"Cannot read this image",
                  file + ": " + (c.detail.empty() ? std::string("not a readable disc image") : c.detail) +
                      ". Choose a .cue, .gdi or .chd dump of your own disc.",
                  ""};
    }
    want_focus_ = "modal-ok";
    std::printf("launcher: disc refused: %s\n", modal_.title.c_str());
}

bool MenuUi::playable() const { return !verifying_ && disc_playable(disc_) && game_ok_; }

// ---- rows -------------------------------------------------------------------------------------------

std::string MenuUi::name_html(const std::string& key, const std::string& name, bool live) const {
    std::string s = esc(name);
    if (mode_ == Mode::InGame && !live)
        s += "<span class=\"badge\">Next start</span>";
    (void)key;
    return s;
}

std::string MenuUi::row_cycle(const std::string& key, const std::string& name,
                              const std::string& value, const std::string& help, bool live) {
    const std::string id = "r-" + key;
    descs_[id] = {name, help, live};
    return "<div id=\"" + id + "\" class=\"prow focusable" + (draft_.pending(key) ? " pending" : "") +
           "\" act=\"cycle:" + key + "\"><div class=\"plabel\">" + name_html(key, name, live) +
           "</div><div class=\"pvalue\"><span class=\"chev\" act=\"prev:" + key +
           "\">\xE2\x80\xB9</span><span class=\"val\">" + esc(value) +
           "</span><span class=\"chev\" act=\"next:" + key +
           "\">\xE2\x80\xBA</span></div><span class=\"pend\"></span></div>";
}

std::string MenuUi::row_toggle(const std::string& key, const std::string& name, bool on,
                               const std::string& help, bool live) {
    const std::string id = "r-" + key;
    descs_[id] = {name, help, live};
    return "<div id=\"" + id + "\" class=\"prow focusable" + (draft_.pending(key) ? " pending" : "") +
           "\" act=\"toggle:" + key + "\"><div class=\"plabel\">" + name_html(key, name, live) +
           "</div><div class=\"pvalue\"><span class=\"switch-label\">" + (on ? "On" : "Off") +
           "</span><span class=\"switch" + (on ? " on" : "") +
           "\"><span class=\"knob\"></span></span></div><span class=\"pend\"></span></div>";
}

std::string MenuUi::row_slider(const std::string& key, const std::string& name, int percent,
                               const std::string& help, bool live) {
    const std::string id = "r-" + key;
    descs_[id] = {name, help, live};
    return "<div id=\"" + id + "\" class=\"prow focusable" + (draft_.pending(key) ? " pending" : "") +
           "\" act=\"slider:" + key + "\"><div class=\"plabel\">" + name_html(key, name, live) +
           "</div><div class=\"pvalue\"><div class=\"slider\" id=\"" + key +
           "-slider\"><div class=\"track\"><div class=\"fill\" style=\"width: " +
           std::to_string(percent) + "%;\"></div></div></div><span class=\"slider-val\">" +
           (percent == 0 ? std::string("Off") : std::to_string(percent) + " %") +
           "</span></div><span class=\"pend\"></span></div>";
}

std::string MenuUi::row_button(const std::string& id, const std::string& act,
                               const std::string& name, const std::string& button,
                               const std::string& help, bool enabled) {
    descs_[id] = {name, help, true};
    std::string r = "<div id=\"" + id + "\" class=\"prow" + (enabled ? " focusable" : " dim") +
                    "\" act=\"" + (enabled ? act : std::string()) + "\"><div class=\"plabel\">" +
                    esc(name) + "</div>";
    if (!button.empty())
        r += "<div class=\"pvalue\"><span class=\"inline-button\">" + esc(button) + "</span></div>";
    r += "<span class=\"pend\"></span></div>";
    return r;
}

float MenuUi::draft_aspect() const {
    if (!cfg_.port || !cfg_.port->widescreen)
        return 4.0f / 3.0f;
    return std::min(parse_aspect(draft_.get("aspect", "16:9")), cfg_.port->max_aspect);
}

bool MenuUi::tab_has_settings(int tab) const { return tab != 1; }

// ---- pages ------------------------------------------------------------------------------------------

std::string MenuUi::page_launcher() const {
    std::string s = "<div id=\"backdrop\"><div class=\"band b0\"></div><div class=\"band "
                    "b1\"></div><div class=\"band b2\"></div><div class=\"band b3\"></div><div "
                    "class=\"band b4\"></div><div class=\"band b5\"></div><div class=\"band "
                    "b6\"></div><div class=\"band b7\"></div></div>";
    std::error_code ec;
    if (std::filesystem::exists(cfg_.exe_dir / "frontend" / "art.png", ec))
        s += "<img id=\"art\" src=\"art.png\"/>";
    std::string sub;
    if (game_ok_) {
        sub = game_.product;
        if (!game_.region.empty())
            sub += (sub.empty() ? "" : kDot) + game_.region;
    }
    s += "<div id=\"launcher\"><div id=\"l-title\">" + esc(title()) + "</div><div id=\"l-sub\">" +
         esc(sub) + "</div><div id=\"lmenu\">";
    const bool ready = playable();
    s += std::string("<div id=\"l-start\" class=\"litem focusable\" act=\"") +
         (ready ? "start" : "load") + "\">" + (ready ? "Start Game" : "Load Game") + "</div>";
    s += "<div id=\"l-controls\" class=\"litem focusable\" act=\"panel:controls\">Controls</div>";
    s += "<div id=\"l-settings\" class=\"litem focusable\" act=\"panel:general\">Settings</div>";
    s += "<div id=\"l-mods\" class=\"litem focusable\" act=\"panel:mods\">Mods</div>";
    s += "<div id=\"l-quit\" class=\"litem focusable\" act=\"exit\">Quit</div></div>";

    // The disc, discreetly, under the list.
    std::string cls, text;
    const std::string shown = disc_.path.empty() ? cfg_.settings->get("disc") : disc_.path;
    const std::string file =
        shown.empty() ? std::string() : u8(std::filesystem::u8path(shown).filename());
    if (!game_ok_) {
        cls = "bad";
        text = std::string(kCross) + "  This build cannot start: " + game_error_;
    } else if (verifying_) {
        text = "Checking disc" + std::string(kEllipsis);
    } else if (dialog_open_) {
        text = "Choose your disc image" + std::string(kEllipsis);
    } else if (disc_playable(disc_)) {
        cls = "ok";
        text = std::string(kCheck) + "  " + file + "  \xE2\x80\x94  verified";
    } else if (disc_.status == DiscStatus::None) {
        text = "Load Game: choose an image of your own disc (.cue, .gdi or .chd)";
    } else {
        cls = "bad";
        text = std::string(kCross) + "  " + file + "  \xE2\x80\x94  " +
               (disc_.status == DiscStatus::WrongRevision ? "wrong release or revision"
                                                          : "cannot be read");
    }
    s += "<div id=\"l-disc\" class=\"" + cls + "\">" + esc(text) + "</div></div>";
    if (ready)
        s += "<div id=\"l-change\" class=\"focusable\" act=\"load\">Change disc" +
             std::string(kEllipsis) + "</div>";
    s += "<div id=\"version\">" + esc(cfg_.version) + "</div>";
    s += "<div id=\"l-hints\">";
    if (!pads_.empty())
        s += glyph(kGlyphA) + hint("Select");
    else
        s += glyph(kGlyphEnter) + hint("Select");
    s += "</div>";
    return s;
}

std::string MenuUi::tab_general() {
    std::string s;
    s += row_slider("rumble", "Rumble strength", std::clamp(draft_.get_int("rumble", 100), 0, 100),
                    "How strongly controllers with motors vibrate when the game asks for it. 0 % "
                    "turns rumble off.",
                    true);
    const auto fps = fps_choices();
    s += row_cycle("fps", "Frame rate", fps[choice_index(fps, draft_.get("fps", "auto"))].label,
                   "<b>Auto</b> blends an in-between frame between the game's frames on displays "
                   "of 120 Hz or more.<br/><b>120</b>: always blend.<br/><b>60</b>: only the "
                   "game's own frames.<br/><br/>The game itself always runs at 60.",
                   false);
    s += row_button("r-about", "readme", "About", "Read me", kAboutText);
    return s;
}

std::string MenuUi::tab_controls() {
    std::string s;
    if (mode_ == Mode::InGame)
        s += row_button("r-rebind", "rebind", "Rebind buttons", "Open",
                        "The keyboard and controller layout for every player. Opens the binding "
                        "screen; the game pauses while it is open. F1 opens it too.");
    else
        s += row_button("r-rebind", "", "Rebind buttons", "",
                        "Available in game: press Escape or a pad's Select / Back for this panel, "
                        "or F1 for the binding screen.",
                        false);
    s += "<div class=\"psection\">Players</div>";
    descs_["p-keyboard"] = {"Keyboard", "Plays as player 1, together with the first controller."
                                        "<br/><br/>Arrows: d-pad<br/>Z X A S: A B X Y<br/>Return: "
                                        "Start<br/>Q W: triggers",
                            true};
    s += "<div id=\"p-keyboard\" class=\"prow player focusable\"><span "
         "class=\"pad-player\">P1</span><div class=\"plabel\">Keyboard</div></div>";
    if (pads_.empty())
        s += "<div class=\"pnote\">No controllers connected. Plug one in at any time.</div>";
    for (std::size_t i = 0; i < pads_.size(); ++i) {
        const std::string player = i < 4 ? "P" + std::to_string(i + 1) : "\xE2\x80\x94";
        const std::string id = "p-pad" + std::to_string(i);
        descs_[id] = {pads_[i],
                      i == 0   ? "Plays as player 1, together with the keyboard."
                      : i < 4 ? "Plays as player " + std::to_string(i + 1) + "."
                              : "Not used: four players at most.",
                      true};
        s += "<div id=\"" + id + "\" class=\"prow player focusable\"><span class=\"pad-player\">" +
             player + "</span><div class=\"plabel\">" + esc(pads_[i]) + "</div></div>";
    }
    return s;
}

std::string MenuUi::tab_graphics() {
    const bool ws = cfg_.port && cfg_.port->widescreen;
    const bool anam = cfg_.port && cfg_.port->widescreen_anamorphic;
    std::string s;
    const int scale = std::clamp(draft_.get_int("scale", 2), 1, 8);
    s += row_cycle("scale", "Resolution", scale_label(scale, draft_aspect(), anam),
                   "Draws the 3D scene at a multiple of the console's 640\xC3\x97" "480. Higher is "
                   "sharper and needs a faster graphics card.",
                   false);
    if (ws) {
        const auto choices = aspect_choices(true, cfg_.port->max_aspect);
        s += row_cycle("aspect", "Aspect ratio",
                       choices[choice_index(choices, draft_.get("aspect", "16:9"))].label,
                       "A wider view of the scene, not a stretch. 4:3 is the original picture.",
                       false);
    }
    const auto fits = fit_choices();
    s += row_cycle("fit", "Fit to window", fits[choice_index(fits, draft_.get("fit", "crop"))].label,
                   "How the picture fills the window when their shapes differ.<br/><br/><b>Crop</b> "
                   "fills it without black bars. <b>Letterbox</b> shows the whole picture. "
                   "<b>Stretch</b> fills it and distorts the shape.",
                   true);
    s += row_toggle("fullscreen", "Fullscreen", draft_.get_bool("fullscreen", false),
                    "Borderless fullscreen on the window's display. Alt+Enter switches at any time.",
                    true);
    if (cfg_.port && cfg_.port->hud.enabled) {
        s += row_toggle("hud_fix", "Widescreen HUD", draft_.get_bool("hud_fix", true),
                        "Keeps the 2D HUD in its original proportions when the picture is wider "
                        "than 4:3.",
                        true);
        const std::vector<Choice> layouts = {{"edges", "Screen edges"}, {"center", "Centered 4:3"}};
        s += row_cycle("hud_layout", "HUD layout",
                       layouts[choice_index(layouts, draft_.get("hud_layout", "edges"))].label,
                       "<b>Screen edges</b> keeps health bars and names at the edges of a wide "
                       "screen. <b>Centered 4:3</b> keeps the original layout in the middle.",
                       true);
    }
    return s;
}

std::string MenuUi::tab_sound() {
    return row_slider("volume", "Master volume", std::clamp(draft_.get_int("volume", 100), 0, 100),
                      "The level of everything the game plays.", true);
}

std::string MenuUi::tab_mods() {
    std::string s;
    const std::string pack = draft_.get("texture_pack");
    const auto dir = texture_pack_dir(pack, config_dir_);
    s += row_toggle("texture_pack", "Texture pack", texture_pack_on(pack),
                    "Replaces the game's textures with the PNG files in<br/>" + esc(u8(dir)), false);
    s += row_button("r-opentex", "open-textures", "Open texture folder", "Open",
                    "Opens " + esc(u8(dir)) + " (created if missing). Put a pack's files there.");
    s += row_toggle("dump_textures", "Dump textures", draft_.get_bool("dump_textures", false),
                    "For pack authors: writes every new texture the game uses to<br/>" +
                        esc(u8(config_dir_ / "texture_dump")),
                    false);
    s += "<div class=\"psection\">Mods</div>";
    const auto mods_dir = config_dir_ / "mods";
    const auto mods = list_mods(mods_dir, draft_.get("mods"));
    const std::string mod_help =
        "Each folder in the mods folder mirrors the disc's files and replaces the files it "
        "contains. Enabled mods load top to bottom; the first one providing a file wins. Use "
        "\xE2\x96\xB2 \xE2\x96\xBC to change the order.";
    int order = 0;
    for (std::size_t i = 0; i < mods.size(); ++i) {
        const auto& m = mods[i];
        const std::string id = id_safe(m.name);
        const bool can_up = m.enabled && i > 0;
        const bool can_down = m.enabled && i + 1 < mods.size() && mods[i + 1].enabled;
        descs_["m-" + id] = {m.name, mod_help, false};
        s += "<div class=\"mod-row\"><span class=\"mod-order\">" +
             (m.enabled ? std::to_string(++order) : std::string()) + "</span>";
        s += "<div id=\"m-" + id + "\" class=\"prow focusable" +
             std::string(draft_.pending("mods") ? " pending" : "") + "\" act=\"mod:" + esc(m.name) +
             "\"><div class=\"plabel\">" + name_html("mods", m.name, false) +
             "</div><div class=\"pvalue\"><span class=\"switch-label\">" +
             (m.enabled ? "On" : "Off") + "</span><span class=\"switch" + (m.enabled ? " on" : "") +
             "\"><span class=\"knob\"></span></span></div></div>";
        s += "<div id=\"mu-" + id + "\" class=\"mini" + std::string(can_up ? " focusable" : " off") +
             "\" act=\"" + (can_up ? "modup:" + esc(m.name) : std::string()) +
             "\">\xE2\x96\xB2</div>";
        s += "<div id=\"md-" + id + "\" class=\"mini" +
             std::string(can_down ? " focusable" : " off") + "\" act=\"" +
             (can_down ? "moddown:" + esc(m.name) : std::string()) + "\">\xE2\x96\xBC</div></div>";
    }
    if (mods.empty())
        s += "<div class=\"pnote\">No mods installed.</div>";
    s += row_button("r-openmods", "open-mods", "Open mods folder", "Open",
                    "Opens " + esc(u8(mods_dir)) + " (created if missing). Add each mod as a "
                    "folder there.<br/><br/>" + mod_help);
    return s;
}

std::string MenuUi::page_panel() {
    descs_.clear();
    std::string s = "<div id=\"shade\"></div><div id=\"panel\"><div id=\"ptabs\">";
    for (int i = 0; i < kTabCount; ++i)
        s += std::string("<div id=\"tab-") + kTabs[i][0] + "\" class=\"ptab focusable" +
             (i == tab_ ? " active" : "") + "\" act=\"tab:" + std::to_string(i) + "\">" +
             kTabs[i][1] + "</div>";
    s += "<div class=\"pspacer\"></div><div id=\"p-quit\" class=\"pquit focusable\" "
         "act=\"ask-quit\">Quit Game</div><div id=\"p-close\" class=\"pclose focusable\" "
         "act=\"close\">\xC3\x97</div></div><div id=\"pbody\"><div id=\"prows\">";
    switch (tab_) {
    case 0: s += tab_general(); break;
    case 1: s += tab_controls(); break;
    case 2: s += tab_graphics(); break;
    case 3: s += tab_sound(); break;
    default: s += tab_mods(); break;
    }
    s += "</div><div id=\"pdesc\"><div id=\"pdesc-title\"></div><div id=\"pdesc-text\"></div><div "
         "id=\"pdesc-note\"></div></div></div><div id=\"pfoot\"><div id=\"phints\">";
    if (!pads_.empty())
        s += glyph(kGlyphA) + hint("Select") + glyph(kGlyphB) + hint("Close") + glyph(kGlyphLB) +
             glyph(kGlyphRB) + hint("Tabs");
    else
        s += glyph(kGlyphEnter) + hint("Select") + glyph(kGlyphEsc) + hint("Close") +
             keycap("Q") + keycap("E") + hint("Tabs");
    s += "</div><div id=\"pending\">";
    if (!toast_.empty())
        s += esc(toast_);
    else if (draft_.any_pending())
        s += "\xE2\x97\x8F  Not applied yet";
    s += "</div>";
    if (tab_has_settings(tab_))
        s += std::string("<div id=\"btn-apply\" class=\"button focusable") +
             (draft_.any_pending() ? " attention" : "") + "\" act=\"apply\">Apply</div>";
    s += "</div></div>";
    return s;
}

std::string MenuUi::page_modal() const {
    std::string s = std::string("<div id=\"mshade\"></div><div id=\"modal\"") +
                    (modal_.confirm_quit ? " class=\"confirm\"" : "") +
                    "><div class=\"mtitle\">" + esc(modal_.title) + "</div><div class=\"mtext\">" +
                    esc(modal_.text) + "</div>" +
                    (modal_.extra.empty() ? std::string()
                                          : "<div class=\"hashes\">" + modal_.extra + "</div>") +
                    "<div class=\"mbuttons\">";
    if (modal_.confirm_quit)
        s += "<div id=\"modal-quit\" class=\"button danger focusable\" act=\"exit\">Quit</div>"
             "<div id=\"modal-cancel\" class=\"button focusable\" act=\"modal-ok\">Cancel</div>";
    else
        s += "<div id=\"modal-ok\" class=\"button focusable\" act=\"modal-ok\">OK</div>";
    return s + "</div></div>";
}

void MenuUi::describe(Rml::Element* e) {
    if (!doc_ || !panel_)
        return;
    for (; e; e = e->GetParentNode()) {
        auto it = descs_.find(e->GetId());
        if (it == descs_.end())
            continue;
        if (desc_shown_ == it->first)
            return;
        desc_shown_ = it->first;
        if (auto* t = doc_->GetElementById("pdesc-title"))
            t->SetInnerRML(esc(it->second.title));
        if (auto* t = doc_->GetElementById("pdesc-text"))
            t->SetInnerRML(it->second.text);
        if (auto* t = doc_->GetElementById("pdesc-note"))
            t->SetInnerRML(mode_ == Mode::InGame && !it->second.live
                               ? "Takes effect the next time the game starts."
                               : "");
        redraw_ = true;
        return;
    }
}

void MenuUi::rebuild() {
    dirty_ = false;
    redraw_ = true;
    Rml::Element* focused_el = ctx_->GetFocusElement();
    std::string focus_id = focused_el && focused_el != doc_ ? focused_el->GetId() : std::string();
    if (focus_id.empty())
        focus_id = last_focus_;
    std::string body;
    if (mode_ == Mode::Pregame)
        body = page_launcher();
    if (panel_)
        body += page_panel();
    if (modal_open_)
        body += page_modal();
    doc_->SetClass("panel-open", panel_);
    desc_shown_.clear();
    if (auto* page = doc_->GetElementById("page"))
        page->SetInnerRML(body);
    ctx_->Update();
    Rml::Element* target = nullptr;
    if (!want_focus_.empty()) {
        target = doc_->GetElementById(want_focus_);
        want_focus_.clear();
    }
    if (!target && focus_first_) {
        if (panel_) {
            if (auto* rows = doc_->GetElementById("prows"))
                target = rows->QuerySelector(".focusable");
            if (!target)
                target = doc_->GetElementById(std::string("tab-") + kTabs[tab_][0]);
        } else {
            target = doc_->GetElementById("l-start");
        }
    }
    focus_first_ = false;
    if (!target && !focus_id.empty())
        target = doc_->GetElementById(focus_id);
    if (!target)
        target = doc_->QuerySelector(".focusable");
    if (target) {
        target->Focus(true);
        target->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
        last_focus_ = target->GetId();
        describe(target);
    }
    ctx_->Update();
}

// ---- actions ------------------------------------------------------------------------------------

void MenuUi::set_tab(int t) {
    if (!panel_)
        return;
    tab_ = (t % kTabCount + kTabCount) % kTabCount;
    Rml::Element* f = focused();
    if (f && f->GetId().rfind("tab-", 0) == 0)
        want_focus_ = std::string("tab-") + kTabs[tab_][0];
    else
        focus_first_ = true;
    dirty_ = true;
}

void MenuUi::adjust(const std::string& key, int step) {
    if (key == "scale") {
        const int v = std::clamp(draft_.get_int("scale", 2), 1, 8) + step;
        draft_.set("scale", std::to_string(((v - 1) % 8 + 8) % 8 + 1));
    } else if (key == "aspect" && cfg_.port && cfg_.port->widescreen) {
        draft_.set("aspect", cycle(aspect_choices(true, cfg_.port->max_aspect),
                                   draft_.get("aspect", "16:9"), step));
    } else if (key == "fit") {
        draft_.set("fit", cycle(fit_choices(), draft_.get("fit", "crop"), step));
    } else if (key == "fps") {
        draft_.set("fps", cycle(fps_choices(), draft_.get("fps", "auto"), step));
    } else if (key == "hud_layout") {
        draft_.set("hud_layout",
                   cycle({{"edges", ""}, {"center", ""}}, draft_.get("hud_layout", "edges"), step));
    } else if (key == "rumble" || key == "volume") {
        const int v = std::clamp(draft_.get_int(key, 100) + step * 10, 0, 100);
        draft_.set(key, std::to_string(v));
    }
    dirty_ = true;
}

void MenuUi::toggle(const std::string& key, int dir) {  // dir: 0 flip, -1 off, +1 on
    if (key == "texture_pack") {
        const bool on = texture_pack_on(draft_.get("texture_pack"));
        const bool want = dir == 0 ? !on : dir > 0;
        if (want != on) {
            const std::string saved = cfg_.settings->get("texture_pack");
            draft_.set("texture_pack", want ? (texture_pack_on(saved) ? saved : "") : "off");
        }
    } else {
        const bool def = key == "hud_fix";
        const bool on = draft_.get_bool(key, def);
        const bool want = dir == 0 ? !on : dir > 0;
        draft_.set(key, want ? "true" : "false");
    }
    dirty_ = true;
}

void MenuUi::mod_action(const std::string& kind, const std::string& name) {
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

void MenuUi::open_folder(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!SDL_OpenURL(file_url(dir).c_str()))
        std::fprintf(stderr, "launcher: cannot open %s: %s\n", u8(dir).c_str(), SDL_GetError());
}

void MenuUi::apply() {
    if (!draft_.any_pending())
        return;
    toast_ = draft_.apply() ? "Applied" : "Could not write the settings file";
    toast_until_ = SDL_GetTicks() + 2500;
    std::printf("launcher: apply: %s (%s)\n", toast_.c_str(), u8(cfg_.settings->file()).c_str());
    if (host_.applied)
        host_.applied();
    dirty_ = true;
}

void MenuUi::back() {
    if (modal_open_) {  // OK on a message, Cancel on the quit question
        modal_open_ = false;
        want_focus_ = modal_.confirm_quit && panel_ ? "p-quit" : "l-start";
        dirty_ = true;
        return;
    }
    if (panel_) {
        close_panel();
        return;
    }
    if (auto* e = doc_->GetElementById("l-quit")) {
        e->Focus(true);
        redraw_ = true;
    }
}

void MenuUi::run_act(const std::string& act, Rml::Event* ev) {
    const auto colon = act.find(':');
    const std::string kind = act.substr(0, colon);
    const std::string arg = colon == std::string::npos ? std::string() : act.substr(colon + 1);
    if (kind == "start") {
        if (playable() && host_.start)
            host_.start();
    } else if (kind == "load") {
        if (!dialog_open_ && host_.pick_disc) {
            dialog_open_ = true;
            dirty_ = true;
            host_.pick_disc();
        }
    } else if (modal_open_ && kind != "exit" && kind != "modal-ok") {
        return;  // a dialog is up: only its own buttons act
    } else if (kind == "panel") {
        open_panel(arg);
    } else if (kind == "close") {
        close_panel();
    } else if (kind == "exit") {
        if (host_.exit_game)
            host_.exit_game();
    } else if (kind == "modal-ok") {
        back();
    } else if (kind == "ask-quit") {
        modal_open_ = true;
        modal_ = {"Quit " + title() + "?",
                  mode_ == Mode::InGame
                      ? "The game closes. Your memory card is saved as you play; settings you have "
                        "not applied are not kept."
                      : "The launcher closes.",
                  "", true};
        want_focus_ = "modal-cancel";
        dirty_ = true;
    } else if (kind == "tab") {
        tab_ = std::clamp(std::atoi(arg.c_str()), 0, kTabCount - 1);
        want_focus_ = std::string("tab-") + kTabs[tab_][0];
        dirty_ = true;
    } else if (kind == "cycle" || kind == "next") {
        adjust(arg, +1);
    } else if (kind == "prev") {
        adjust(arg, -1);
    } else if (kind == "toggle") {
        toggle(arg, 0);
    } else if (kind == "slider") {
        // A click on the bar sets the value there; Enter steps by 10 and wraps.
        const float mx = ev ? ev->GetParameter<float>("mouse_x", -1.0f) : -1.0f;
        Rml::Element* bar = doc_->GetElementById(arg + "-slider");
        if (mx >= 0.0f && bar) {
            const float x0 = bar->GetAbsoluteLeft() + bar->GetClientLeft();
            const float w = bar->GetClientWidth();
            if (w > 0.0f && mx >= x0 - 20.0f && mx <= x0 + w + 20.0f) {
                const int v =
                    static_cast<int>(std::clamp((mx - x0) / w, 0.0f, 1.0f) * 10.0f + 0.5f) * 10;
                draft_.set(arg, std::to_string(v));
                dirty_ = true;
            }
        } else {
            const int v = draft_.get_int(arg, 100);
            draft_.set(arg, std::to_string(v >= 100 ? 0 : v + 10));
            dirty_ = true;
        }
    } else if (kind == "mod" || kind == "modup" || kind == "moddown") {
        mod_action(kind, arg);
    } else if (kind == "open-textures") {
        open_folder(texture_pack_dir(draft_.get("texture_pack"), config_dir_));
    } else if (kind == "open-mods") {
        open_folder(config_dir_ / "mods");
    } else if (kind == "readme") {
        std::error_code ec;
        const auto local = cfg_.exe_dir / "README.md";
        const std::string url = std::filesystem::exists(local, ec)
                                    ? file_url(local)
                                    : "https://github.com/danielgomesvieira2000/dreamcomp#readme";
        SDL_OpenURL(url.c_str());
    } else if (kind == "rebind") {
        if (host_.open_bindings)
            host_.open_bindings();
    } else if (kind == "apply") {
        apply();
    }
    redraw_ = true;
}

Rml::Element* MenuUi::focused() const { return ctx_ ? ctx_->GetFocusElement() : nullptr; }

std::string MenuUi::focus_id() const {
    Rml::Element* f = focused();
    return f ? f->GetId() : std::string();
}

Rml::Element* MenuUi::element(const std::string& id) const {
    return doc_ ? doc_->GetElementById(id) : nullptr;
}

void MenuUi::ProcessEvent(Rml::Event& ev) {
    if (ev == Rml::EventId::Focus) {
        Rml::Element* t = ev.GetTargetElement();
        if (t && !t->GetId().empty() && t != doc_)
            last_focus_ = t->GetId();
        describe(t);
        redraw_ = true;
        return;
    }
    if (ev == Rml::EventId::Mouseover) {
        describe(ev.GetTargetElement());
        return;
    }
    if (ev == Rml::EventId::Click) {
        for (Rml::Element* e = ev.GetTargetElement(); e; e = e->GetParentNode()) {
            const std::string act = e->GetAttribute<Rml::String>("act", "");
            if (!act.empty()) {
                run_act(act, &ev);
                ev.StopPropagation();
                break;
            }
        }
        return;
    }
    if (ev == Rml::EventId::Keydown) {
        const auto k =
            static_cast<Rml::Input::KeyIdentifier>(ev.GetParameter<int>("key_identifier", 0));
        const bool arrow = k == Rml::Input::KI_UP || k == Rml::Input::KI_DOWN ||
                           k == Rml::Input::KI_LEFT || k == Rml::Input::KI_RIGHT;
        Rml::Element* focus = focused();
        if (!focus || focus == doc_ || focus->GetTagName() == "body") {
            if (arrow) {
                Rml::Element* to = last_focus_.empty() ? nullptr : doc_->GetElementById(last_focus_);
                if (!to)
                    to = doc_->QuerySelector(panel_ ? "#panel .focusable" : ".focusable");
                if (to)
                    to->Focus(true);
                ev.StopPropagation();
            }
            return;
        }
        if (k == Rml::Input::KI_LEFT || k == Rml::Input::KI_RIGHT) {
            const int step = k == Rml::Input::KI_LEFT ? -1 : +1;
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
        if (arrow) {
            navigate(focus, k);
            ev.StopPropagation();
        }
    }
}

// Focus moves by the launcher's own spatial search (RmlUi's `nav: auto` does not enter or leave a
// scroll container), in regions: the launcher list, and in the panel the tab bar, the rows and the
// footer. Left/right stay in a region; up/down cross into the neighbouring one.
void MenuUi::navigate(Rml::Element* focus, Rml::Input::KeyIdentifier key) {
    Rml::Element* region = region_of(focus);
    if (modal_open_) {
        // Only the dialog's buttons, left and right.
        if (key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT)
            if (Rml::Element* t = nav_search(doc_->GetElementById("modal"), focus, key)) {
                t->Focus(true);
                redraw_ = true;
            }
        return;
    }
    if (!region && focus && focus->GetId() == "l-change")
        region = doc_->GetElementById("lmenu");
    Rml::Element* tabs = doc_->GetElementById("ptabs");
    Rml::Element* rows = doc_->GetElementById("prows");
    Rml::Element* foot = doc_->GetElementById("pfoot");
    Rml::Element* target = nullptr;
    if (region && region->GetId() == "lmenu") {
        // The launcher list, plus "Change disc" under it.
        Rml::ElementList items;
        doc_->QuerySelectorAll(items, "#lmenu .focusable, #l-change");
        auto it = std::find(items.begin(), items.end(), focus);
        if (it != items.end()) {
            if (key == Rml::Input::KI_DOWN && it + 1 != items.end())
                target = *(it + 1);
            else if (key == Rml::Input::KI_UP && it != items.begin())
                target = *(it - 1);
        }
    } else {
        target = nav_search(region, focus, key);
        if (!target && key == Rml::Input::KI_DOWN) {
            if (region == tabs)
                target = rows ? rows->QuerySelector(".focusable") : nullptr;
            if (!target && region != foot && foot)
                target = foot->QuerySelector(".focusable");
        } else if (!target && key == Rml::Input::KI_UP) {
            if (region == foot)
                target = nav_search(rows, focus, key);
            if (!target && region != tabs)
                target = doc_->GetElementById(std::string("tab-") + kTabs[tab_][0]);
        }
    }
    if (!target)
        return;
    target->Focus(true);
    target->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
    redraw_ = true;
}

// ---- input --------------------------------------------------------------------------------------

void MenuUi::refresh_pads() {
    std::vector<std::string> names;
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; ++i) {
            const char* n = SDL_GetGamepadNameForID(ids[i]);
            names.push_back(n && *n ? n : "Controller");
        }
        SDL_free(ids);
    }
    set_pads(std::move(names));
}

void MenuUi::set_pads(std::vector<std::string> names) {
    pads_ = std::move(names);
    dirty_ = true;
}

void MenuUi::key(Rml::Input::KeyIdentifier k) {
    ctx_->ProcessKeyDown(k, 0);
    ctx_->ProcessKeyUp(k, 0);
    redraw_ = true;
}

void MenuUi::pad_dir(Rml::Input::KeyIdentifier k) {
    held_ = k;
    repeat_at_ = SDL_GetTicks() + 380;
    key(k);
}

bool MenuUi::handle(const SDL_Event& ev, const MouseMap& map) {
    if (!ctx_)
        return false;
    switch (ev.type) {
    case SDL_EVENT_KEY_DOWN: {
        const SDL_Keycode k = ev.key.key;
        if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE) {
            if (!ev.key.repeat)
                back();
            return true;
        }
        if (k == SDLK_Q || k == SDLK_E) {
            set_tab(tab_ + (k == SDLK_Q ? -1 : 1));
            return true;
        }
        if (k == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT))
            return false;  // Alt+Enter: the window's fullscreen toggle
        ctx_->ProcessKeyDown(RmlSDL::ConvertKey(static_cast<int>(k)), RmlSDL::GetKeyModifierState());
        redraw_ = true;
        return true;
    }
    case SDL_EVENT_KEY_UP:
        ctx_->ProcessKeyUp(RmlSDL::ConvertKey(static_cast<int>(ev.key.key)),
                           RmlSDL::GetKeyModifierState());
        return true;
    case SDL_EVENT_TEXT_INPUT:
        return true;
    case SDL_EVENT_MOUSE_MOTION: {
        float cx = 0, cy = 0;
        if (map(ev.motion.x, ev.motion.y, cx, cy)) {
            ctx_->ProcessMouseMove(static_cast<int>(cx), static_cast<int>(cy),
                                   RmlSDL::GetKeyModifierState());
            redraw_ = true;
        }
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        ctx_->ProcessMouseButtonDown(RmlSDL::ConvertMouseButton(ev.button.button),
                                     RmlSDL::GetKeyModifierState());
        redraw_ = true;
        return true;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        ctx_->ProcessMouseButtonUp(RmlSDL::ConvertMouseButton(ev.button.button),
                                   RmlSDL::GetKeyModifierState());
        redraw_ = true;
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        ctx_->ProcessMouseWheel(-ev.wheel.y, RmlSDL::GetKeyModifierState());
        redraw_ = true;
        return true;
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_GAMEPAD_REMOVED:
        refresh_pads();
        return false;  // the window keeps its own device list too
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        switch (ev.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: pad_dir(Rml::Input::KI_UP); break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: pad_dir(Rml::Input::KI_DOWN); break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: pad_dir(Rml::Input::KI_LEFT); break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: pad_dir(Rml::Input::KI_RIGHT); break;
        case SDL_GAMEPAD_BUTTON_SOUTH: key(Rml::Input::KI_RETURN); break;
        case SDL_GAMEPAD_BUTTON_EAST: back(); break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: set_tab(tab_ - 1); break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: set_tab(tab_ + 1); break;
        case SDL_GAMEPAD_BUTTON_START:
            // Before the game Start starts it; in game Start belongs to the game.
            if (mode_ == Mode::Pregame && !panel_ && playable() && host_.start)
                host_.start();
            break;
        case SDL_GAMEPAD_BUTTON_BACK:
            if (mode_ == Mode::InGame)
                close_panel();
            break;
        default: break;
        }
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        held_ = Rml::Input::KI_UNKNOWN;
        return true;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        if (ev.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTX && ev.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTY)
            return true;
        const float v = static_cast<float>(ev.gaxis.value) / 32767.0f;
        const bool x = ev.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX;
        int& state = x ? stick_x_ : stick_y_;
        const int now = v > 0.6f ? 1 : v < -0.6f ? -1 : (std::fabs(v) < 0.3f ? 0 : state);
        if (now != state) {
            state = now;
            if (now == 0)
                held_ = Rml::Input::KI_UNKNOWN;
            else if (x)
                pad_dir(now > 0 ? Rml::Input::KI_RIGHT : Rml::Input::KI_LEFT);
            else
                pad_dir(now > 0 ? Rml::Input::KI_DOWN : Rml::Input::KI_UP);
        }
        return true;
    }
    default:
        return false;
    }
}

void MenuUi::tick() {
    if (verifying_ && verify_.valid() &&
        verify_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        disc_ = verify_.get();
        verifying_ = false;
        dirty_ = true;
    }
    if (held_ != Rml::Input::KI_UNKNOWN && SDL_GetTicks() >= repeat_at_) {
        repeat_at_ = SDL_GetTicks() + 95;
        key(held_);
    }
    if (!toast_.empty() && SDL_GetTicks() >= toast_until_) {
        toast_.clear();
        dirty_ = true;
    }
    if (dirty_)
        rebuild();
}

bool MenuUi::script_event(const std::string& k, SDL_Event& e) {
    e = SDL_Event{};
    if (k.rfind("pad:", 0) == 0) {
        const std::string b = k.substr(4);
        const SDL_GamepadButton btn = b == "up"      ? SDL_GAMEPAD_BUTTON_DPAD_UP
                                      : b == "down"  ? SDL_GAMEPAD_BUTTON_DPAD_DOWN
                                      : b == "left"  ? SDL_GAMEPAD_BUTTON_DPAD_LEFT
                                      : b == "right" ? SDL_GAMEPAD_BUTTON_DPAD_RIGHT
                                      : b == "a"     ? SDL_GAMEPAD_BUTTON_SOUTH
                                      : b == "b"     ? SDL_GAMEPAD_BUTTON_EAST
                                      : b == "lb"    ? SDL_GAMEPAD_BUTTON_LEFT_SHOULDER
                                      : b == "rb"    ? SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER
                                      : b == "start" ? SDL_GAMEPAD_BUTTON_START
                                      : b == "back" || b == "select" ? SDL_GAMEPAD_BUTTON_BACK
                                                                     : SDL_GAMEPAD_BUTTON_INVALID;
        if (btn == SDL_GAMEPAD_BUTTON_INVALID)
            return false;
        e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        e.gbutton.button = static_cast<Uint8>(btn);
        e.gbutton.down = true;
        return true;
    }
    const SDL_Keycode code = k == "up"      ? SDLK_UP
                             : k == "down"  ? SDLK_DOWN
                             : k == "left"  ? SDLK_LEFT
                             : k == "right" ? SDLK_RIGHT
                             : k == "enter" ? SDLK_RETURN
                             : k == "esc"   ? SDLK_ESCAPE
                             : k == "tab"   ? SDLK_TAB
                             : k == "q"     ? SDLK_Q
                             : k == "e"     ? SDLK_E
                                            : SDLK_UNKNOWN;
    if (code == SDLK_UNKNOWN)
        return false;
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = code;
    e.key.down = true;
    return true;
}

}  // namespace dreamcomp::frontend
