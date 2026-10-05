// Unit checks for the launcher's logic (no window): settings draft and Apply, option cycling,
// mods list and order, disc verification failures. Build: target dreamcomp_launcher_tests.
//   dreamcomp_launcher_tests <scratch dir> [<disc image> <expected sha1>]
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "dreamcomp/settings.h"
#include "launcher_model.h"

using namespace dreamcomp;
using namespace dreamcomp::frontend;

static int g_fail = 0;
#define CHECK(x)                                                                \
    do {                                                                        \
        if (!(x)) {                                                             \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);  \
            ++g_fail;                                                           \
        }                                                                       \
    } while (0)

static std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <scratch dir> [<disc> <sha1>]\n", argv[0]);
        return 2;
    }
    const std::filesystem::path dir = std::filesystem::path(argv[1]) / "launcher_model_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "mods" / "alpha");
    std::filesystem::create_directories(dir / "mods" / "beta");
    std::filesystem::create_directories(dir / "mods" / "gamma");
    const auto file = dir / "settings.ini";

    // Draft: nothing is written until Apply; reverting a value clears its pending mark.
    {
        Settings s;
        s.load(file);
        s.set("scale", "2");
        CHECK(s.save());
        s.load(file);
        Draft d(s);
        CHECK(!d.any_pending());
        d.set("scale", "4");
        d.set("rumble", "40");
        CHECK(d.pending("scale") && d.pending("rumble") && d.any_pending());
        CHECK(s.get("scale") == "2");                       // not applied yet
        CHECK(slurp(file).find("scale = 4") == std::string::npos);
        d.set("scale", "2");                                // back to the saved value
        CHECK(!d.pending("scale"));
        d.set("scale", "3");
        CHECK(d.apply());
        CHECK(!d.any_pending());
        CHECK(s.get("scale") == "3" && s.get("rumble") == "40");
        const std::string text = slurp(file);
        CHECK(text.find("scale = 3") != std::string::npos);
        CHECK(text.find("rumble = 40") != std::string::npos);
    }

    // Options.
    CHECK(scale_label(2, 4.0f / 3.0f, true) == "2\xC3\x97  \xE2\x80\x94  1280\xC3\x97" "960");
    CHECK(scale_label(2, 16.0f / 9.0f, true) == "2\xC3\x97  \xE2\x80\x94  1707\xC3\x97" "960");
    CHECK(aspect_choices(false, 4.0f).size() == 1);
    CHECK(aspect_choices(true, 32.0f / 9.0f).size() == 4);
    CHECK(aspect_choices(true, 16.0f / 9.0f).size() == 2);
    CHECK(cycle(fit_choices(), "crop", 1) == "letterbox");
    CHECK(cycle(fit_choices(), "crop", -1) == "stretch");
    CHECK(cycle(fit_choices(), "bogus", 1) == "letterbox");
    CHECK(texture_pack_on("") && !texture_pack_on("off"));
    CHECK(texture_pack_dir("", dir) == dir / "textures");
    CHECK(texture_pack_dir("D:/packs/x", dir) == std::filesystem::path("D:/packs/x"));

    // Mods: enabled first in setting order, then the rest alphabetically; missing names dropped.
    {
        auto m = list_mods(dir / "mods", "gamma, missing ,alpha");
        CHECK(m.size() == 3);
        CHECK(m[0].name == "gamma" && m[0].enabled);
        CHECK(m[1].name == "alpha" && m[1].enabled);
        CHECK(m[2].name == "beta" && !m[2].enabled);
        CHECK(mods_setting(m) == "gamma,alpha");
        std::swap(m[0], m[1]);
        CHECK(mods_setting(m) == "alpha,gamma");
        m[2].enabled = true;
        CHECK(mods_setting(m) == "alpha,gamma,beta");
        CHECK(list_mods(dir / "nothing", "a").empty());
    }

    // Disc: a missing file and a non-image are refused with a reason.
    {
        auto c = verify_disc(dir / "nope.cue", "abc");
        CHECK(c.status == DiscStatus::Unreadable && !disc_playable(c));
        std::ofstream(dir / "junk.gdi") << "not a gdi\n";
        c = verify_disc(dir / "junk.gdi", "abc");
        CHECK(c.status == DiscStatus::Unreadable || c.status == DiscStatus::NoBootFile);
        CHECK(!disc_playable(c));
    }
    // And, given the owner's disc, the right and a wrong reference.
    if (argc >= 4) {
        auto c = verify_disc(argv[2], argv[3]);
        std::printf("disc: status %d boot %s sha1 %s\n", static_cast<int>(c.status),
                    c.boot_name.c_str(), c.found_sha1.c_str());
        CHECK(c.status == DiscStatus::Verified && disc_playable(c));
        c = verify_disc(argv[2], "0000000000000000000000000000000000000000");
        CHECK(c.status == DiscStatus::WrongRevision && !disc_playable(c));
        CHECK(!c.found_sha1.empty() && c.found_sha1 != c.expected_sha1);
    }

    std::filesystem::remove_all(dir);
    std::printf(g_fail ? "%d check(s) failed\n" : "all launcher model checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
