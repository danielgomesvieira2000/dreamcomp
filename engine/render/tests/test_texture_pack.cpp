// Texture identity (hash scheme v1), file names, and the dump / pack round trip (dreamcomp).
// The golden values were computed independently, in Python with the reference xxhash package,
// from the byte layout docs/TEXTURE-PACKS.md specifies; if one changes, every pack made so far
// stops matching, so the fix is a new scheme version, never a new golden value.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "dream/render/png.h"
#include "dream/render/texture.h"
#include "dream/render/texture_pack.h"

#include "doctest.h"

namespace {

using namespace dream::render;
using u32 = std::uint32_t;

u32 make_tcw(u32 address, unsigned format, bool twiddled = true, bool vq = false,
             bool mipmapped = false, unsigned palette_select = 0) {
    return ((address >> 3) & 0x1FFFFFu) | (palette_select << 21) |
           (static_cast<u32>(!twiddled) << 26) | (static_cast<u32>(format) << 27) |
           (static_cast<u32>(vq) << 30) | (static_cast<u32>(mipmapped) << 31);
}

u32 make_tsp(unsigned u_shift, unsigned v_shift) {
    return (u_shift << 3) | v_shift;
}

TextureInfo info_for(u32 tcw, u32 tsp) {
    TextureInfo info;
    REQUIRE(describe_texture(tcw, tsp, 0, info));
    return info;
}

std::uint64_t hash_of(const TextureInfo& info, const std::vector<std::uint8_t>& vram,
                      const u32* palette = nullptr) {
    std::uint64_t h = 0;
    REQUIRE(texture_hash(info, vram.data(), vram.size(), palette, h));
    return h;
}

std::vector<std::uint8_t> pattern_vram(u32 at, u32 bytes, std::size_t size = 64 * 1024) {
    std::vector<std::uint8_t> v(size, 0xEE);
    for (u32 i = 0; i < bytes; ++i) v[at + i] = static_cast<std::uint8_t>(i * 7 + 3);
    return v;
}

std::vector<u32> test_palette() {
    std::vector<u32> pal(1024);
    for (u32 i = 0; i < 1024; ++i) pal[i] = 0xFF000000u | (i * 0x010203u);
    return pal;
}

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name) {
        path = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

}  // namespace

TEST_CASE("texture pack: xxh64 matches the reference implementation") {
    CHECK(xxh64("", 0) == 0xEF46DB3751D8E999ull);
    CHECK(xxh64("a", 1) == 0xD24EC4F1A98C6E5Bull);
    CHECK(xxh64("abc", 3) == 0x44BC2CF5AD770999ull);
    const std::string s = "Nobody inspects the spammish repetition";
    CHECK(xxh64(s.data(), s.size()) == 0xFBCEA83C8A378BF1ull);
    std::vector<std::uint8_t> big;
    for (int r = 0; r < 3; ++r)
        for (int i = 0; i < 256; ++i) big.push_back(static_cast<std::uint8_t>(i));
    big.insert(big.end(), {'x', 'y', 'z'});
    CHECK(xxh64(big.data(), big.size()) == 0xE921A1B45BD779F8ull);
    CHECK(xxh64("abc", 3, 0x9E3779B1) == 0x1318DF30094A85FDull);
}

TEST_CASE("texture pack: version 1 golden hashes") {
    // 8x8 ARGB1555, twiddled: header + 128 bytes.
    const auto vram = pattern_vram(0x100, 128);
    const TextureInfo a = info_for(make_tcw(0x100, 0), make_tsp(0, 0));
    CHECK(hash_of(a, vram) == 0xAF35E9037FD1F137ull);
    CHECK(texture_file_name(a, hash_of(a, vram)) == "8x8_1555_af35e9037fd1f137.png");

    // 8x8 4-bit palette, window 3 (entries 48..63): header + 32 index bytes + the used colours.
    std::vector<std::uint8_t> v(64 * 1024, 0);
    for (u32 i = 0; i < 32; ++i) v[i] = static_cast<std::uint8_t>(i * 5);
    const auto pal = test_palette();
    const TextureInfo p = info_for(make_tcw(0, 5, true, false, false, 3), make_tsp(0, 0));
    CHECK(p.palette_base == 48);
    CHECK(hash_of(p, v, pal.data()) == 0x967A9300F1A8C520ull);
}

TEST_CASE("texture pack: the same bytes give the same name wherever they sit") {
    const auto v1 = pattern_vram(0x100, 128);
    const auto v2 = pattern_vram(0x8000, 128);
    const TextureInfo a = info_for(make_tcw(0x100, 0), make_tsp(0, 0));
    const TextureInfo b = info_for(make_tcw(0x8000, 0), make_tsp(0, 0));
    CHECK(hash_of(a, v1) == hash_of(a, v1));
    CHECK(hash_of(a, v1) == hash_of(b, v2));
    CHECK(texture_file_name(a, hash_of(a, v1)) == texture_file_name(b, hash_of(b, v2)));
}

TEST_CASE("texture pack: one byte changed inside the texture changes the name, outside does not") {
    auto v = pattern_vram(0x100, 128);
    const TextureInfo a = info_for(make_tcw(0x100, 0), make_tsp(0, 0));
    const auto before = hash_of(a, v);
    v[0x100 + 127] ^= 1;
    CHECK(hash_of(a, v) != before);
    v[0x100 + 127] ^= 1;
    CHECK(hash_of(a, v) == before);
    v[0x100 + 128] ^= 0xFF;  // the next byte belongs to someone else
    v[0x100 - 1] ^= 0xFF;
    CHECK(hash_of(a, v) == before);
}

TEST_CASE("texture pack: format, size and layout are part of the identity") {
    const auto v = pattern_vram(0, 512);
    const auto h1555 = hash_of(info_for(make_tcw(0, 0), make_tsp(0, 0)), v);
    const auto h565 = hash_of(info_for(make_tcw(0, 1), make_tsp(0, 0)), v);
    const auto hlinear = hash_of(info_for(make_tcw(0, 0, false), make_tsp(0, 0)), v);
    const auto hwide = hash_of(info_for(make_tcw(0, 0), make_tsp(1, 0)), v);
    CHECK(h1555 != h565);
    CHECK(h1555 != hlinear);
    CHECK(h1555 != hwide);
}

TEST_CASE("texture pack: an indexed texture hashes only the palette entries it uses") {
    // 8x8 4-bit texture that uses indices 1 and 2 only.
    std::vector<std::uint8_t> v(64 * 1024, 0);
    for (u32 i = 0; i < 32; ++i) v[i] = (i & 1) ? 0x21 : 0x12;
    auto pal = test_palette();
    const TextureInfo t = info_for(make_tcw(0, 5, true, false, false, 2), make_tsp(0, 0));
    const auto before = hash_of(t, v, pal.data());
    pal[32 + 7] ^= 0x00FFFFFFu;  // an entry of this window the texture never indexes
    pal[100] ^= 0x00FFFFFFu;     // another window altogether
    CHECK(hash_of(t, v, pal.data()) == before);
    pal[32 + 2] ^= 0x00000001u;  // an entry it does use
    CHECK(hash_of(t, v, pal.data()) != before);
    pal[32 + 2] ^= 0x00000001u;
    // The same colours in a different window are the same texture.
    auto moved = test_palette();
    moved[16 + 1] = pal[32 + 1];
    moved[16 + 2] = pal[32 + 2];
    const TextureInfo t1 = info_for(make_tcw(0, 5, true, false, false, 1), make_tsp(0, 0));
    CHECK(hash_of(t1, v, moved.data()) == before);

    // 8-bit: the same rule.
    std::vector<std::uint8_t> v8(64 * 1024, 0);
    for (u32 i = 0; i < 64; ++i) v8[i] = static_cast<std::uint8_t>(10 + (i % 3));
    auto pal8 = test_palette();
    const TextureInfo t8 = info_for(make_tcw(0, 6, true, false, false, 0x10), make_tsp(0, 0));
    CHECK(t8.palette_base == 256);
    const auto b8 = hash_of(t8, v8, pal8.data());
    pal8[256 + 13] ^= 0xFFu;
    CHECK(hash_of(t8, v8, pal8.data()) == b8);
    pal8[256 + 11] ^= 0xFFu;
    CHECK(hash_of(t8, v8, pal8.data()) != b8);
}

TEST_CASE("texture pack: a compressed texture hashes its codebook and its indices") {
    // 16x16 VQ: 2 KB codebook, then 64 index bytes.
    std::vector<std::uint8_t> v(64 * 1024, 0);
    for (u32 i = 0; i < 2048 + 64; ++i) v[i] = static_cast<std::uint8_t>(i * 13);
    const TextureInfo t = info_for(make_tcw(0, 1, true, true), make_tsp(1, 1));
    const auto before = hash_of(t, v);
    v[2047] ^= 1;  // last codebook byte
    CHECK(hash_of(t, v) != before);
    v[2047] ^= 1;
    v[2048 + 63] ^= 1;  // last index
    CHECK(hash_of(t, v) != before);
    v[2048 + 63] ^= 1;
    v[2048 + 64] ^= 1;  // past the texture
    CHECK(hash_of(t, v) == before);
}

TEST_CASE("texture pack: a mipmapped texture hashes its top level, as the decoder reads it") {
    // 16x16 ARGB1555 mipmapped: the chain (1x1, 2x2, 4x4, 8x8 plus padding) comes first. Each
    // byte is checked against the decoder: the hash must change exactly when the picture does.
    std::vector<std::uint8_t> v(64 * 1024, 0);
    for (u32 i = 0; i < 4096; ++i) v[i] = static_cast<std::uint8_t>(i * 29 + 1);
    const TextureInfo t = info_for(make_tcw(0, 0, true, false, true), make_tsp(1, 1));
    const std::vector<u32> pal(1024, 0);
    std::vector<u32> pixels_before, pixels_after;
    REQUIRE(decode_texture(t, v.data(), v.size(), pal.data(), pixels_before));
    const auto before = hash_of(t, v);
    for (u32 offset = 0; offset < t.size_bytes() + 8; ++offset) {
        v[offset] ^= 0x5A;
        REQUIRE(decode_texture(t, v.data(), v.size(), pal.data(), pixels_after));
        const bool picture_changed = pixels_after != pixels_before;
        const bool hash_changed = hash_of(t, v) != before;
        if (picture_changed != hash_changed)
            FAIL("byte " << offset << ": picture changed " << picture_changed
                         << ", hash changed " << hash_changed);
        v[offset] ^= 0x5A;
    }
    CHECK(texture_format_name(t) == "1555mm");
}

TEST_CASE("texture pack: a texture outside video memory has no hash") {
    std::vector<std::uint8_t> v(1024, 0);
    const TextureInfo t = info_for(make_tcw(1024 - 64, 0), make_tsp(0, 0));
    std::uint64_t h = 0;
    CHECK_FALSE(texture_hash(t, v.data(), v.size(), nullptr, h));
}

TEST_CASE("texture pack: file names") {
    TextureInfo t = info_for(make_tcw(0, 1, true, true, true), make_tsp(2, 2));
    CHECK(texture_format_name(t) == "vq565mm");
    const std::string name = texture_file_name(t, 0x0123456789ABCDEFull);
    CHECK(name == "32x32_vq565mm_0123456789abcdef.png");
    std::uint64_t h = 0;
    REQUIRE(parse_texture_file_name(name, h));
    CHECK(h == 0x0123456789ABCDEFull);
    // Only the hash identifies the file: a pack author may rename the rest, or upper-case it.
    CHECK(parse_texture_file_name("logo-hd_0123456789ABCDEF.PNG", h));
    CHECK(h == 0x0123456789ABCDEFull);
    CHECK_FALSE(parse_texture_file_name("0123456789abcdef.png", h));  // no separator
    CHECK_FALSE(parse_texture_file_name("x_0123456789abcdeg.png", h));
    CHECK_FALSE(parse_texture_file_name("x_0123456789abcdef.jpg", h));
    CHECK_FALSE(parse_texture_file_name("x_123456789abcdef.png", h));
}

TEST_CASE("texture pack: dump, then load the dump back as a pack") {
    TempDir dir("dream_texture_pack_test");
    const auto v = pattern_vram(0x100, 128);
    const TextureInfo t = info_for(make_tcw(0x100, 0), make_tsp(0, 0));
    const std::vector<u32> pal(1024, 0);
    std::vector<u32> pixels;
    REQUIRE(decode_texture(t, v.data(), v.size(), pal.data(), pixels));
    const auto h = hash_of(t, v);

    {
        TextureReplacer dumper;
        REQUIRE(dumper.set_dump_dir(dir.path / "dump"));
        CHECK(dumper.dumping());
        CHECK_FALSE(dumper.replacing());
        dumper.dump(t, h, pixels);
        dumper.dump(t, h, pixels);  // once only
        CHECK(dumper.dumped == 1);
    }
    const auto file = dir.path / "dump" / texture_file_name(t, h);
    REQUIRE(std::filesystem::exists(file));
    png::Image img;
    REQUIRE(png::read_file(file, img));
    CHECK(img.width == 8);
    CHECK(img.height == 8);
    CHECK(img.pixels == pixels);

    {
        // A second run does not write what the first one did.
        TextureReplacer again;
        REQUIRE(again.set_dump_dir(dir.path / "dump"));
        again.dump(t, h, pixels);
        CHECK(again.dumped == 0);
    }

    // A pack: the dumped file, renamed and moved into a subdirectory, at 2x the size.
    std::filesystem::create_directories(dir.path / "pack" / "ui");
    std::vector<u32> big(16 * 16);
    for (u32 y = 0; y < 16; ++y)
        for (u32 x = 0; x < 16; ++x) big[y * 16 + x] = pixels[(y / 2) * 8 + x / 2] ^ 0x00FFFFFFu;
    char name[64];
    std::snprintf(name, sizeof name, "my-logo_%016llx.png", static_cast<unsigned long long>(h));
    REQUIRE(png::write_file(dir.path / "pack" / "ui" / name, big.data(), 16, 16));
    // Noise a pack can contain: a file that is not a texture, and a texture that will not load.
    REQUIRE(png::write_file(dir.path / "pack" / "readme.png", big.data(), 16, 16));
    if (FILE* f = std::fopen((dir.path / "pack" / "bad_00000000000000aa.png").string().c_str(),
                             "wb")) {
        std::fputs("not a png", f);
        std::fclose(f);
    }

    TextureReplacer pack;
    REQUIRE(pack.open_pack(dir.path / "pack"));
    CHECK(pack.pack_size() == 2);
    const png::Image* rep = pack.find(t, h);
    REQUIRE(rep != nullptr);
    CHECK(rep->width == 16);
    CHECK(rep->height == 16);
    CHECK(rep->pixels == big);
    CHECK(pack.find(t, h) == rep);  // kept in memory, not read again
    CHECK(pack.find(t, h ^ 1) == nullptr);
    CHECK(pack.find(t, 0xAA) == nullptr);
    CHECK(pack.replace_failed == 1);
    CHECK(pack.find(t, 0xAA) == nullptr);
    CHECK(pack.replace_failed == 1);  // reported once

    // Over the memory budget the image is still served, just not kept.
    TextureReplacer tight;
    tight.retain_budget = 0;
    REQUIRE(tight.open_pack(dir.path / "pack"));
    const png::Image* once = tight.find(t, h);
    REQUIRE(once != nullptr);
    CHECK(once->pixels == big);
    CHECK(tight.find(t, h) != nullptr);

    TextureReplacer missing;
    std::string error;
    CHECK_FALSE(missing.open_pack(dir.path / "no-such-dir", &error));
    CHECK_FALSE(error.empty());
}
