# Texture packs

A dreamcomp port can write out every texture the game uses, and load replacements for any of them
from a directory of PNG files. Both work in the windowed renderer only (`--window`, which a
double-clicked port always uses).

The code: `engine/render/src/texture_pack.cpp` (identity, dump, pack index),
`engine/render/src/png.cpp` (PNG encoder and decoder), `engine/render/src/vk/texture_cache.cpp`
(where both are consulted), `src/core.cpp` (settings). Tests:
`engine/render/tests/test_texture_pack.cpp`, `engine/render/tests/test_png.cpp`.

**Never commit a dumped texture or a pack.** A dump is the publisher's art, and a repainted pack is
still derived from it. `.gitignore` ignores `texture_dump/` and `texture_packs/`, and
`python tools/audit.py assets` flags any tracked PNG named like a dumped texture.

## Using it

### 1. Dump

```
soulcalibur-recomp --window --dump-textures C:\work\dump
```

or once and for all in the settings file (`dump_textures = true`), which writes to
`<config dir>/texture_dump/`. Play through the parts of the game you want to change; every
distinct texture is written once, when it is first decoded, as

```
<width>x<height>_<format>_<hash>.png        e.g.  256x256_vq565mm_9f1c03be5a7d2e41.png
```

`format` is `1555`, `565`, `4444`, `yuv422`, `bump`, `pal4` or `pal8`, prefixed `vq` when the
texture is compressed and suffixed `mm` when it is mipmapped. The image is RGBA, top mip level
only, exactly as the game's decoder produced it. A second run into the same directory skips files
that are already there, so dumping is resumable. The end-of-run report line `textures: ...`
counts what was written and how long it took.

Dumping costs a hash and a PNG encode per new texture (see *Costs*), so it is for authoring, not
for playing.

### 2. Edit

Open the PNG in any editor and paint. Rules:

- **Keep the file name's hash.** Only the 16 hex digits after the last `_` identify the texture;
  everything before them is for you and may be renamed (`title-logo_9f1c03be5a7d2e41.png` is fine).
  Upper or lower case both work. Sub-directories are fine too.
- **Any resolution, same shape.** A 2x or 4x replacement is the usual thing. The aspect ratio must
  match the original or the art is stretched across the polygons (the loader warns). The GPU's
  maximum texture size is the only upper limit.
- **Keep the alpha channel meaningful.** Transparent texels in the original are transparent for a
  reason (cut-outs, fades). Save as 8-bit RGBA or RGB PNG; greyscale, grey+alpha and 8-bit
  palette PNGs also load. 16-bit and interlaced PNGs are refused with a message.

### 3. Install

Put the edited files in a directory and point the game at it:

```
soulcalibur-recomp --window --texture-pack C:\work\mypack
```

or set `texture_pack = C:\work\mypack` in the settings file, or simply place the pack at
`<config dir>/textures/`, which is used automatically when it exists (`texture_pack = off`
disables that). `--set texture_pack=DIR` and `--set dump_textures=true` override the file for one
run and are kept only with `--save-settings`, like every other setting. A flag passed explicitly
wins over the settings.

`<config dir>` is the directory holding the settings file: `%APPDATA%\dreamcomp\<port>\` on Windows,
`~/.config/dreamcomp/<port>/` on Linux, or wherever `--settings FILE` points.

The pack directory is indexed once at start (the log says how many files and how long it took);
the run never looks at the file system per texture afterwards. Each replacement PNG is decoded the
first time its texture is needed and kept in memory (up to 1 GB of decoded images), so the game
dropping its texture cache (a palette change) does not reload anything from disk.

## How it works

The renderer's texture cache decodes a guest texture once and uploads it. With a pack or a dump
directory set, a cache miss also works out the texture's **content hash** (below). If the pack has
a file for that hash, its pixels are uploaded instead of the decoded ones, at the file's own
resolution; otherwise the texture is decoded as usual, and in dump mode written out.

A replaced texture is an ordinary cache entry with different pixels. Everything that drops a
decoded texture drops a replaced one the same way, and the next use hashes the bytes again:

- **Palette change.** The cache is dropped; each texture is re-hashed with the new colours. A
  paletted texture whose palette the game animates therefore has one hash per palette state, and a
  pack must supply each state it wants to replace (or the unreplaced states show the original).
- **Render to texture** (`--framebuffer-writeback`). The written range is invalidated as before,
  and is also remembered: a texture overlapping a range the renderer has written a frame into is
  never dumped or replaced, because its content is a frame and every frame would be a new file.
- **New data at the same address.** The engine's cache does not watch CPU writes into video
  memory (an existing engine limitation, `engine/docs/runtime-render.md`); when it re-decodes, the
  replacement follows the new hash.

**Mipmaps.** The engine currently uploads only the top level of every texture (no mip chain), and
a replacement is uploaded the same way: one level, sampled with the filter the game asked for. A
4x replacement on a distant polygon can therefore shimmer exactly as an upscaled original would.
Generating a chain for both is a renderer change for later; the hash already covers only the top
level, so packs will not change when it happens.

### Costs

Measured on Soulcalibur, boot to Character Select (1500 frames; Windows 11, clang-cl Release,
Iris Xe):

| | |
|---|---|
| Cache misses hashed | 2754 (134 palette changes each drop the cache), 64-67 ms in total: ~24 µs each |
| Dump | 168 distinct textures, 2.9 MB of PNG, 427 ms writing: ~2.5 ms each, once |
| Pack index | 0.1-0.2 ms for a one-file pack (one directory walk at start) |
| Replacement | a 512x512 RGBA PNG (saved by Pillow) decoded once in 8.4 ms, then served from memory for 131 cache misses |

With neither flag set nothing is hashed and the cache behaves exactly as before.

## Hash scheme, version 1

This is a specification: packs depend on it. It must never change; a different identity is a new
version, and the loader would then have to know both.

The hash is **XXH64 with seed 0** over the byte string below. All integers are little-endian.

| Bytes | Content |
|---|---|
| 4 | ASCII `DCTX` |
| 4 | scheme version, `1` |
| 4 | width in texels (8 to 1024, from the TSP word) |
| 4 | height in texels |
| 4 | flags: bits 0-2 pixel format (0 ARGB1555, 1 RGB565, 2 ARGB4444, 3 YUV422, 4 bump map, 5 4-bit palette, 6 8-bit palette); bit 3 twiddled; bit 4 VQ-compressed; bit 5 mipmapped; bit 6 stride. Other bits 0. |
| n | texel data, below |
| 4 x k | palette colours, indexed formats only, below |

`twiddled` is the effective layout (an indexed or compressed texture is always twiddled, whatever
the scan-order bit says), as `describe_texture` in `engine/render/src/texture.cpp` works it out.

**Texel data** is the exact range of video memory (the 64-bit texture view) that the decoder reads
for the **top mip level**, copied as stored:

- *Not compressed*: from `base` for `w*h*2` bytes (16-bit formats and YUV), `w*h` (8-bit palette)
  or `w*h/2` (4-bit palette). `base` is the texture address, plus, when mipmapped, the size of the
  smaller levels that precede the top one: the sum over sizes 1, 2, 4, ... up to `w/2` of `size^2`
  texels at the format's bytes per texel (integer arithmetic: 2 per texel for 16-bit formats,
  1 for 8-bit palette, `size^2/2` rounded down for 4-bit palette), plus a padding of 2 bytes
  (16-bit formats), 1 byte (8-bit palette) or 2 bytes (4-bit palette). This is the decoder's
  `mipmap_base_offset`, frozen here as version 1 defines it.
- *VQ-compressed*: the whole 2048-byte codebook at the texture address, then the `(w/2)*(h/2)`
  index bytes starting at `address + 2048 + mip offset`, where the mip offset is the same sum
  counted in index bytes (`size^2/4` per level) plus 1 byte of padding.

The lower mip levels are deliberately not hashed: they are derived art, and the top level is what
a replacement replaces.

**Palette colours** (4-bit and 8-bit palette formats, not compressed): for every index value
`i` from 0 to 15 (4-bit) or 0 to 255 (8-bit), in ascending order, that occurs **at least once** in
the texel data, the 4 bytes R, G, B, A of palette entry `palette_base + i` after conversion to
8 bits per channel (PAL_RAM_CTRL formats ARGB1555, RGB565 and ARGB4444 widen each channel by
repeating its high bits, as `unpack_palette` does; ARGB8888 is taken as is). Entries the texture
does not use are left out, so a game rewriting other colours in a shared bank does not rename it;
the palette window itself (`palette_base`) is not hashed, so the same picture in a different bank
is the same texture.

Not hashed, on purpose: the texture's address (a streaming game reloads the same art elsewhere),
the TSP sampling bits (filtering, clamping, flipping are how a polygon uses a texture, not what it
is), and decoded pixels (a decoder fix must never invalidate a pack).

The unit tests pin two golden values computed independently from this description in Python with
the reference `xxhash` package: an 8x8 twiddled ARGB1555 texture whose 128 bytes are
`(i*7+3) & 255` hashes to `af35e9037fd1f137`, and an 8x8 4-bit texture in palette window 3 whose
32 bytes are `(i*5) & 255`, with palette entry `j` = `0xFF000000 | j*0x010203` (stored as
r | g<<8 | b<<16 | a<<24), hashes to `967a9300f1a8c520`.

## Limitations

- Window mode only; a headless run ignores both flags.
- No pack manifest yet (`engine/docs/future-enhancements.md` §3 asks for one naming the scheme
  version); version 1 is implied. When a manifest arrives, a pack without one means version 1.
- Animated palettes: one file per palette state (see above).
- No mip chain for originals or replacements (see above).
- Each cache entry keeps its upload staging buffer, so a 4x replacement costs its size in host
  memory as well as on the GPU while it is resident.
- Replacements are loaded synchronously the first time they are needed; a very large pack can
  hitch on first sight of a new scene. An asynchronous preload is future work.
- Render-to-texture targets are never dumped or replaced.
