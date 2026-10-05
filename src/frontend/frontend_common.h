// Pieces both frontend hosts share: UTF-8 file access for RmlUi and the fonts.
#pragma once

#include <RmlUi/Core.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace dreamcomp::frontend {

inline std::string u8_path(const std::filesystem::path& p) {
    const auto s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

// Paths are UTF-8; on Windows fopen would read them as the ANSI code page.
class Utf8FileInterface final : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override {
#ifdef _WIN32
        std::FILE* f = _wfopen(std::filesystem::u8path(path).c_str(), L"rb");
#else
        std::FILE* f = std::fopen(path.c_str(), "rb");
#endif
        return reinterpret_cast<Rml::FileHandle>(f);
    }
    void Close(Rml::FileHandle f) override { std::fclose(reinterpret_cast<std::FILE*>(f)); }
    size_t Read(void* buf, size_t size, Rml::FileHandle f) override {
        return std::fread(buf, 1, size, reinterpret_cast<std::FILE*>(f));
    }
    bool Seek(Rml::FileHandle f, long offset, int origin) override {
        return std::fseek(reinterpret_cast<std::FILE*>(f), offset, origin) == 0;
    }
    size_t Tell(Rml::FileHandle f) override {
        return static_cast<size_t>(std::ftell(reinterpret_cast<std::FILE*>(f)));
    }
};

// Inter (regular / semibold / bold, registered under one family so font-weight works) and
// PromptFont, from <exe dir>/frontend/fonts. Call after Rml::Initialise().
inline bool load_fonts(const std::filesystem::path& exe_dir, std::string* error) {
    const auto fonts = exe_dir / "frontend" / "fonts";
    struct Face {
        const char* file;
        const char* family;
        Rml::Style::FontWeight weight;
    } faces[] = {{"Inter-Regular.ttf", "Inter", Rml::Style::FontWeight::Normal},
                 {"Inter-SemiBold.ttf", "Inter", Rml::Style::FontWeight(600)},
                 {"Inter-Bold.ttf", "Inter", Rml::Style::FontWeight::Bold},
                 {"promptfont.ttf", "promptfont", Rml::Style::FontWeight::Normal}};
    for (const auto& f : faces) {
        const auto g = (fonts / f.file).generic_u8string();
        const std::string path(reinterpret_cast<const char*>(g.data()), g.size());
        if (!Rml::LoadFontFace(path, f.family, Rml::Style::FontStyle::Normal, f.weight, false)) {
            *error = "cannot load font " + path;
            return false;
        }
    }
    return true;
}

}  // namespace dreamcomp::frontend
