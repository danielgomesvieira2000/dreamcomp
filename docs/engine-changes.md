# Engine changes (dreamcomp → dream-recomp)

`engine/` is a git subtree of [dream-recomp](https://github.com/phobos665/dream-recomp)
(GPL-2.0, phobos665). Every change dreamcomp makes inside `engine/` is listed here, so that
upstream merges stay reviewable and each change can be offered upstream as a pull request.

Update the subtree: `git subtree pull --prefix engine https://github.com/phobos665/dream-recomp.git main --squash`,
then re-check every row below (a row whose change upstream absorbed is moved to *Upstreamed*).

| Date | Area | Change | Why | Upstream status |
|---|---|---|---|---|
| 2026-10-05 | `engine/runtime/boot/boot_main.cpp` | `--present-mode` parsing guarded by `DREAM_WITH_RENDERER` | A headless build (`-DDREAM_RENDERER=OFF`) failed to compile: `PM`/`present_mode` exist only with the renderer | not offered yet |
| 2026-10-05 | `engine/runtime/src/gdrom/disc.cpp`, `engine/runtime/include/dream/runtime/gdrom/disc.h`, `engine/tools/dcdisc/dcdisc/cue.py`, `engine/tools/dcdisc/dcdisc/image.py`, `engine/tools/dcdisc/dcdisc/cli.py`, `engine/tools/dcdisc/tests/test_cue.py` | Redump `.cue` disc images (single/high-density `REM` markers, INDEX 01 as track start, INDEX 00 pregap skipped by file offset) in both the C++ runtime and `dcdisc` | Redump distributes Dreamcast dumps as cue/bin; users had to hand-write a `.gdi` | not offered yet |
| 2026-10-05 | `engine/runtime/src/gdrom/disc.cpp` | 64-bit file seek (`_fseeki64` / `fseeko`) in the GDI/CUE reader | `fseek` takes a 32-bit `long` on Windows; data tracks reach 1.2 GB | not offered yet |
| 2026-10-05 | `engine/runtime/boot/boot_main.cpp`, `engine/runtime/src/gdrom/disc.cpp`, `engine/runtime/include/dream/runtime/gdrom/disc.h`, `engine/runtime/include/dream/runtime/sha1.h` | `--disc FILE` overrides `[disc] image`; when the config's extracted `[binary] path` is absent, the boot file named in IP.BIN is read off the disc (`read_root_file`, ISO9660 root lookup) and must match `[disc] sha1_1st_read` (exit 3 otherwise) | A released port needs only the player's disc image at run time, never an extracted copy beside the executable; a different revision of the game is refused instead of misbehaving | not offered yet |
| 2026-10-05 | `engine/runtime/src/aica/aica.cpp` | A 32-bit AICA register write is treated as a 16-bit write of the low half | Soulcalibur's sound driver keys voices on with 32-bit `STR`; the mixer ran key-on/SA/FEG updates only for 16-bit writes, so the game was silent (0 key-ons -> 12 in 1800 frames) | not offered yet |
| 2026-10-05 | `engine/translator/include/dream/translator/emit.h`, `engine/translator/src/emit/emit.cpp`, `engine/translator/src/main.cpp` | `[hooks]` implemented: `"name"`, `"name@entry"`, `"name@exit"` make the function's wrapper call `dream_hook_entry_<name>` (true = replaced) / `dream_hook_exit_<name>`, defined by the port; a hook address that is not a discovered function entry fails the translation | The table was parsed but never emitted; ports need native hooks for widescreen, HUD, camera and mods | not offered yet |
| 2026-10-05 | `engine/runtime/include/dream/runtime/host_ext.h`, `engine/runtime/boot/boot_main.cpp` | Launcher extension registry: `adjust_args`, `parse_arg`, `usage`, `on_start`, `on_vblank`, `on_stop` | dreamcomp's settings, bare-launch defaults and enhancements run inside the stock launcher without forking it | not offered yet |
| 2026-10-05 | `engine/render/include/dream/render/vk/present.h`, `engine/render/src/vk/present.cpp` | `present_options()`: fit (letterbox / crop / stretch) and a display-aspect override | Anamorphic widescreen needs the 4:3 framebuffer shown at 16:9; Daniel's series rule is no black bars (crop) | not offered yet |
| 2026-10-05 | `engine/runtime/boot/boot_main.cpp` | `--screenshot-at` takes a comma-separated list | One run captures a whole boot sequence instead of one run per frame | not offered yet |

## Pinned base

| Upstream commit | Date pulled | Notes |
|---|---|---|
| `51a148c7bf9191fa8593e707c4996cf887076b33` | 2026-10-05 | Evaluated on Windows 11 / clang-cl 22 / Iris Xe: Soulcalibur (USA) reaches Character Select |

## Upstreamed

None yet.
