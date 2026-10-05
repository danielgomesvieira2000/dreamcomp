# What may be published

Not legal advice; the rules this project follows so that its repositories can be public.

## Never in any repository (framework or port)

- Disc images or tracks (`.cue/.bin/.gdi/.chd/.cdi/.iso`), IP.BIN, 1ST_READ.BIN or any file from
  a disc, extracted or converted (textures, models, audio, video, text dumps).
- Anything derived from game data: generated C++ (`gen/`), upscaled/replaced textures made from
  the originals, decompressed archives, symbol dumps that embed code bytes.
- BIOS, flash or memory-card images; Sega Katana SDK headers, libraries or source (not even in
  comments: describe interfaces in your own words or point at KallistiOS, BSD-licensed).

`.gitignore` blocks the usual names; `python tools/audit.py assets` (pre-commit hook and CI) blocks
by extension, file signature (IP.BIN, PVR, AFS, raw CD sync) and size.

## Allowed

- Configuration measured from a disc: addresses, sizes, SHA-1 hashes, function boundaries,
  relocation tables (`game/<id>.toml`). These are facts about the binary, not the binary.
- Original code: hooks, enhancements, tools, documentation that describes the game's behaviour in
  our own words.
- Texture or mod packs **made from scratch** by their authors, distributed separately from the
  port, under their own terms.

## Releases

A release executable contains machine-translated game code compiled in. That is the same
model as N64Recomp-family ports (Zelda64Recomp and Daniel's own N64 ports): the player still must
own and supply the disc, which the executable reads at run time and verifies. Whether to publish
binaries for a given port is the maintainer's decision per release; source releases are always
fine.

## Licences

dreamcomp and its ports are GPL-2.0 because the engine is. Third-party components and their
licences: [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
