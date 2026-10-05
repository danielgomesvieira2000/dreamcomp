# Mods

## File replacement (players and mod authors)

A mod is a folder that mirrors the game disc's file tree. Every file in it replaces the disc file
with the same path (names are matched case-insensitively):

```
%APPDATA%\dreamcomp\soulcalibur\mods\
  my-stage-mod\
    STAGE.DAT            replaces STAGE.DAT on the disc
  translation\
    KMISSION.DAT
```

Enable mods in `settings.ini` (first listed wins when two provide the same file):

```
mods = translation, my-stage-mod
```

or for one run: `--mod DIR` (repeatable). The launcher prints one line per replaced file.

Limits (v1): a mod can replace files but not add new ones; games that read a file through a
fixed sector number instead of the filesystem only see replacements that are not larger than the
original.

Mods contain game data when they are made from the original files: distribute them on your own
terms, never in a port repository.

## How it works

`ModDisc` (`engine/runtime/src/gdrom/mod_disc.cpp`) wraps the opened disc image before the
GD-ROM HLE sees it. It walks the ISO9660 tree (`list_iso9660`, which detects whether extents are
absolute LBAs, frame addresses or track-relative), then for each replacement:

| Replacement size | Served where | Directory record |
|---|---|---|
| ≤ original sectors | the original LBAs; leftover sectors of the old file read as zeros | size patched |
| > original | new LBAs after the end of the data track (the track is extended) | extent and size patched |

Patched directory sectors and replacement data are served from `read_raw`, synthesising a mode-1
raw sector (sync, MSF header, user data) where the track stores 2352-byte sectors. Nothing is
written anywhere; the original image is opened read-only.

Code mods use [hooks](HOOKS.md) in a port's sources. A binary mod ABI (loading hook DLLs at run
time) is on the roadmap.
