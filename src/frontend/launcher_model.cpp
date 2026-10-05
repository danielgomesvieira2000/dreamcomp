#include "launcher_model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "dream/runtime/gdrom/disc.h"
#include "dream/runtime/sha1.h"
#include "dreamcomp/settings.h"

namespace dreamcomp::frontend {

// ---- Draft -----------------------------------------------------------------------------------

std::string Draft::get(const std::string& key, const std::string& def) const {
    const auto it = changes_.find(key);
    if (it != changes_.end())
        return it->second;
    return s_.get(key, def);
}

int Draft::get_int(const std::string& key, int def) const {
    const std::string v = get(key);
    if (v.empty())
        return def;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 0);
    return end && *end == '\0' ? static_cast<int>(n) : def;
}

bool Draft::get_bool(const std::string& key, bool def) const {
    const std::string v = get(key);
    if (v == "true" || v == "1" || v == "on" || v == "yes")
        return true;
    if (v == "false" || v == "0" || v == "off" || v == "no")
        return false;
    return def;
}

void Draft::set(const std::string& key, const std::string& value) {
    // Back to the saved value: no longer a change.
    if (value == s_.get(key, "\x01"))
        changes_.erase(key);
    else
        changes_[key] = value;
}

bool Draft::pending(const std::string& key) const { return changes_.count(key) != 0; }

bool Draft::any_pending() const { return !changes_.empty(); }

bool Draft::apply() {
    for (const auto& [k, v] : changes_) s_.set(k, v);
    changes_.clear();
    return s_.save();
}

// ---- options ---------------------------------------------------------------------------------

float parse_aspect(const std::string& s) {
    const auto colon = s.find(':');
    if (colon == std::string::npos)
        return 4.0f / 3.0f;
    const float w = std::strtof(s.c_str(), nullptr);
    const float h = std::strtof(s.c_str() + colon + 1, nullptr);
    return (w > 0.0f && h > 0.0f) ? std::clamp(w / h, 4.0f / 3.0f, 4.0f) : 4.0f / 3.0f;
}

std::string scale_label(int scale, float aspect, bool anamorphic) {
    scale = std::clamp(scale, 1, 8);
    const float wide = (anamorphic && aspect > 4.0f / 3.0f + 0.01f) ? aspect / (4.0f / 3.0f) : 1.0f;
    const int w = static_cast<int>(640.0f * static_cast<float>(scale) * wide + 0.5f);
    const int h = 480 * scale;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d\xC3\x97  \xE2\x80\x94  %d\xC3\x97%d", scale, w, h);
    return buf;
}

std::vector<Choice> aspect_choices(bool widescreen, float max_aspect) {
    std::vector<Choice> out{{"4:3", "4:3  (original)"}};
    if (!widescreen)
        return out;
    const Choice wide[] = {{"16:9", "16:9"}, {"21:9", "21:9"}, {"32:9", "32:9"}};
    for (const auto& c : wide)
        if (parse_aspect(c.value) <= max_aspect + 0.01f)
            out.push_back(c);
    return out;
}

std::vector<Choice> fps_choices() {
    return {{"auto", "Auto"}, {"120", "120"}, {"60", "60  (original)"}};
}

std::vector<Choice> fit_choices() {
    return {{"crop", "Crop  (no bars)"}, {"letterbox", "Letterbox"}, {"stretch", "Stretch"}};
}

std::size_t choice_index(const std::vector<Choice>& choices, const std::string& value) {
    for (std::size_t i = 0; i < choices.size(); ++i)
        if (choices[i].value == value)
            return i;
    return 0;
}

std::string cycle(const std::vector<Choice>& choices, const std::string& value, int step) {
    if (choices.empty())
        return value;
    const long n = static_cast<long>(choices.size());
    long i = static_cast<long>(choice_index(choices, value)) + step;
    i = ((i % n) + n) % n;
    return choices[static_cast<std::size_t>(i)].value;
}

// ---- disc ------------------------------------------------------------------------------------

DiscCheck verify_disc(const std::filesystem::path& image, const std::string& expected) {
    DiscCheck c;
    c.path = image.string();
    c.expected_sha1 = expected;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(image, ec)) {
        c.status = DiscStatus::Unreadable;
        c.detail = "file not found";
        return c;
    }
    std::string err;
    std::unique_ptr<dream::gdrom::Disc> disc = dream::gdrom::open_disc(image, err);
    if (!disc) {
        c.status = DiscStatus::Unreadable;
        c.detail = err.empty() ? "not a disc image this build can read" : err;
        return c;
    }
    std::vector<std::uint8_t> boot;
    if (!dream::gdrom::read_root_file(*disc, "", boot, &c.boot_name)) {
        c.status = DiscStatus::NoBootFile;
        c.detail = "no boot file in the image's filesystem";
        return c;
    }
    c.found_sha1 = dream::sha1_hex(boot.data(), boot.size());
    if (expected.empty())
        c.status = DiscStatus::NoReference;
    else
        c.status = c.found_sha1 == expected ? DiscStatus::Verified : DiscStatus::WrongRevision;
    return c;
}

bool disc_playable(const DiscCheck& c) {
    return c.status == DiscStatus::Verified || c.status == DiscStatus::NoReference;
}

// ---- texture packs ---------------------------------------------------------------------------

bool texture_pack_on(const std::string& v) { return !(v == "off" || v == "none" || v == "false"); }

std::filesystem::path texture_pack_dir(const std::string& v, const std::filesystem::path& config_dir) {
    if (!texture_pack_on(v) || v.empty())
        return config_dir / "textures";
    return v;
}

// ---- mods ------------------------------------------------------------------------------------

std::vector<std::string> split_list(const std::string& s) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at <= s.size()) {
        std::size_t comma = s.find(',', at);
        if (comma == std::string::npos)
            comma = s.size();
        std::string item = s.substr(at, comma - at);
        const auto b = item.find_first_not_of(" \t");
        const auto e = item.find_last_not_of(" \t");
        if (b != std::string::npos)
            out.push_back(item.substr(b, e - b + 1));
        at = comma + 1;
    }
    return out;
}

std::vector<ModEntry> list_mods(const std::filesystem::path& mods_dir, const std::string& setting) {
    std::vector<std::string> folders;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(mods_dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code ec2;
        if (it->is_directory(ec2))
            folders.push_back(it->path().filename().string());
    }
    std::sort(folders.begin(), folders.end());
    std::vector<ModEntry> out;
    for (const auto& name : split_list(setting)) {
        const bool exists = std::find(folders.begin(), folders.end(), name) != folders.end();
        const bool dup = std::any_of(out.begin(), out.end(),
                                     [&](const ModEntry& m) { return m.name == name; });
        if (exists && !dup)
            out.push_back({name, true});
    }
    for (const auto& name : folders)
        if (std::none_of(out.begin(), out.end(), [&](const ModEntry& m) { return m.name == name; }))
            out.push_back({name, false});
    return out;
}

std::string mods_setting(const std::vector<ModEntry>& mods) {
    std::string out;
    for (const auto& m : mods) {
        if (!m.enabled)
            continue;
        if (!out.empty())
            out += ",";
        out += m.name;
    }
    return out;
}

}  // namespace dreamcomp::frontend
