// ModDisc and the ISO9660 walker (dreamcomp addition), on the synthetic GD-ROM fixture.
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "dream/runtime/gdrom/disc.h"
#include "dream/runtime/gdrom/mod_disc.h"

#include "doctest.h"

using namespace dream::gdrom;

namespace {

const char* kFixture = DREAM_DCDISC_FIXTURES "/synthetic.chd";

std::filesystem::path scratch(const char* name) {
    auto p = std::filesystem::temp_directory_path() / "dream_mod_disc_test" / name;
    std::filesystem::remove_all(p);
    std::filesystem::create_directories(p);
    return p;
}

void write_file(const std::filesystem::path& p, const std::string& bytes) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << bytes;
}

std::string read(Disc& d, const char* name) {
    std::vector<std::uint8_t> out;
    if (!read_root_file(d, name, out))
        return "<missing>";
    return std::string(out.begin(), out.end());
}

const IsoEntry* find(const std::vector<IsoEntry>& v, const std::string& path) {
    for (const auto& e : v)
        if (e.path == path)
            return &e;
    return nullptr;
}

}  // namespace

TEST_CASE("iso9660: the walker lists nested files of the fixture") {
    std::string err;
    auto disc = open_disc(kFixture, err);
    REQUIRE_MESSAGE(disc, err);
    std::vector<IsoEntry> files;
    REQUIRE(list_iso9660(*disc, files));
    REQUIRE(find(files, "README.TXT"));
    REQUIRE(find(files, "DATA/LEVEL01.BIN"));
    REQUIRE(find(files, "SOUND/BGM01.ADX"));
    CHECK(find(files, "DATA")->dir);
    CHECK(find(files, "DATA/LEVEL01.BIN")->size == 8192);
    CHECK(read(*disc, "readme.txt") == "synthetic test disc for dcdisc\n");
}

TEST_CASE("mods: a smaller replacement is served in place, a larger one relocated") {
    std::string err;
    auto base = open_disc(kFixture, err);
    REQUIRE(base);
    std::vector<IsoEntry> before;
    REQUIRE(list_iso9660(*base, before));
    const std::uint32_t old_end = base->hd_track()->end_lba();

    const auto root = scratch("basic");
    write_file(root / "README.TXT", "modded!\n");
    write_file(root / "data" / "level01.bin", std::string(10000, 'Q'));  // 8192 -> 10000: grows
    write_file(root / "NOT_ON_DISC.TXT", "ignored");

    std::string log;
    auto disc = ModDisc::wrap(std::move(base), {root}, log);
    REQUIRE(disc);
    CHECK(log.find("README.TXT") != std::string::npos);
    CHECK(log.find("ignored NOT_ON_DISC.TXT") != std::string::npos);

    CHECK(read(*disc, "README.TXT") == "modded!\n");
    std::vector<IsoEntry> after;
    REQUIRE(list_iso9660(*disc, after));
    // In place: same LBA, new size.
    CHECK(find(after, "README.TXT")->lba == find(before, "README.TXT")->lba);
    CHECK(find(after, "README.TXT")->size == 8);
    // Relocated past the old end of the track, which grew to hold it.
    const IsoEntry* lvl = find(after, "DATA/LEVEL01.BIN");
    REQUIRE(lvl);
    CHECK(lvl->size == 10000);
    CHECK(lvl->lba >= old_end);
    CHECK(disc->hd_track()->end_lba() >= lvl->lba + 5);
    std::uint8_t sec[2048];
    REQUIRE(disc->read_user(lvl->lba + 4, sec));  // last sector: 10000 - 4*2048 = 1808 bytes
    CHECK(sec[0] == 'Q');
    CHECK(sec[1807] == 'Q');
    CHECK(sec[1808] == 0);
    // Untouched files still read from the base image.
    const IsoEntry* bgm = find(after, "SOUND/BGM01.ADX");
    REQUIRE(bgm);
    REQUIRE(disc->read_user(bgm->lba, sec));
    CHECK(sec[0] == 0x80);
}

TEST_CASE("mods: the first root wins, and no matching file means no wrapper") {
    std::string err;
    const auto a = scratch("first"), b = scratch("second"), empty = scratch("empty");
    write_file(a / "README.TXT", "from a\n");
    write_file(b / "README.TXT", "from b\n");
    std::string log;
    auto disc = ModDisc::wrap(open_disc(kFixture, err), {a, b}, log);
    CHECK(read(*disc, "README.TXT") == "from a\n");

    auto plain = ModDisc::wrap(open_disc(kFixture, err), {empty}, log);
    REQUIRE(plain);
    CHECK(dynamic_cast<ModDisc*>(plain.get()) == nullptr);
}
