// The launcher's logic, without any UI: the settings it edits (as a draft applied on Apply), disc
// verification, the mods folder. Kept apart from launcher.cpp so it can be tested without a
// window (src/frontend/tests/test_launcher_model.cpp).
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace dreamcomp {
class Settings;
}

namespace dreamcomp::frontend {

// ---- settings draft ------------------------------------------------------------------------
// Daniel's rule: a value changes when the player presses Apply (or Start Game), never on click.
// The draft holds what the player picked; `pending(key)` marks rows that differ from the file.
class Draft {
public:
    explicit Draft(Settings& s) : s_(s) {}

    // The draft value, else the saved value, else `def`.
    std::string get(const std::string& key, const std::string& def = {}) const;
    int get_int(const std::string& key, int def) const;
    bool get_bool(const std::string& key, bool def) const;
    void set(const std::string& key, const std::string& value);

    bool pending(const std::string& key) const;
    bool any_pending() const;
    // Writes the draft into the settings and saves the file. False if the save failed (the values
    // are still applied in memory for this run).
    bool apply();
    void revert() { changes_.clear(); }

private:
    Settings& s_;
    std::map<std::string, std::string> changes_;
};

// ---- option tables -------------------------------------------------------------------------
struct Choice {
    std::string value, label;
};

// Internal resolution: "2× — 1280×960" for scale 2 at 4:3; wider targets (anamorphic widescreen)
// get the engine's wider render target.
std::string scale_label(int scale, float aspect, bool anamorphic);
float parse_aspect(const std::string& s);  // "16:9" -> 1.778; anything else -> 4/3
// Aspect choices the port supports: 4:3 always, wider ones up to max_aspect when widescreen is
// implemented.
std::vector<Choice> aspect_choices(bool widescreen, float max_aspect);
std::vector<Choice> fit_choices();
// Index of `value` in `choices` (0 if absent), and the value `step` places further, wrapping.
std::size_t choice_index(const std::vector<Choice>& choices, const std::string& value);
std::string cycle(const std::vector<Choice>& choices, const std::string& value, int step);

// ---- disc ----------------------------------------------------------------------------------
enum class DiscStatus { None, Verified, NoReference, WrongRevision, NoBootFile, Unreadable };

struct DiscCheck {
    DiscStatus status = DiscStatus::None;
    std::string path;
    std::string boot_name;     // "1ST_READ.BIN"
    std::string found_sha1;    // of the boot file read off the image
    std::string expected_sha1; // [disc] sha1_1st_read
    std::string detail;        // the reader's error for Unreadable
};

// Opens `image`, reads the boot file named in IP.BIN and compares its SHA-1 with `expected`
// (empty: nothing to compare against, NoReference). Takes a moment on a CHD: call it off the UI
// thread.
DiscCheck verify_disc(const std::filesystem::path& image, const std::string& expected);
bool disc_playable(const DiscCheck& c);

// ---- texture packs ---------------------------------------------------------------------------
// `texture_pack` is a directory, "off", or empty (= <settings dir>/textures when it exists).
bool texture_pack_on(const std::string& value);
std::filesystem::path texture_pack_dir(const std::string& value,
                                       const std::filesystem::path& config_dir);

// ---- mods ------------------------------------------------------------------------------------
struct ModEntry {
    std::string name;
    bool enabled = false;
};
// Every subfolder of `mods_dir`: the enabled ones first, in `setting` order ("a,b": first wins),
// then the rest alphabetically. Names in the setting with no folder are dropped.
std::vector<ModEntry> list_mods(const std::filesystem::path& mods_dir, const std::string& setting);
std::string mods_setting(const std::vector<ModEntry>& mods);  // enabled names in list order
std::vector<std::string> split_list(const std::string& s);    // "a, b" -> {"a","b"}

}  // namespace dreamcomp::frontend
