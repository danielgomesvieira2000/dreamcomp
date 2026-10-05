// Per-user settings for a port: one `key = value` text file in the platform's config directory
// (Windows %APPDATA%\dreamcomp\<port>\, macOS ~/Library/Application Support/dreamcomp/<port>/,
// Linux $XDG_CONFIG_HOME/dreamcomp/<port>/ or ~/.config/..., overridable with --settings FILE or
// DREAMCOMP_SETTINGS). Never inside the install directory, never in the repository.
#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace dreamcomp {

class Settings {
public:
    // Loads `file` (missing is fine: everything falls back to defaults).
    void load(const std::filesystem::path& file);
    // Writes back atomically (temp file + rename). False if the directory cannot be written.
    bool save() const;

    std::string get(const std::string& key, const std::string& def = {}) const;
    bool get_bool(const std::string& key, bool def) const;
    int get_int(const std::string& key, int def) const;
    float get_float(const std::string& key, float def) const;
    void set(const std::string& key, const std::string& value);
    void set_bool(const std::string& key, bool v) { set(key, v ? "true" : "false"); }
    void set_int(const std::string& key, int v) { set(key, std::to_string(v)); }

    const std::filesystem::path& file() const noexcept { return file_; }
    bool dirty() const noexcept { return dirty_; }

private:
    std::filesystem::path file_;
    std::map<std::string, std::string> values_;
    bool dirty_ = false;
};

// The platform config directory for `port_id` (created on demand by save()).
std::filesystem::path default_config_dir(const std::string& port_id);

}  // namespace dreamcomp
