# Third-party notices

| Component | Where | Licence | Notes |
|---|---|---|---|
| dream-recomp | `engine/` | GPL-2.0 | phobos665; vendored subtree, changes listed in `docs/engine-changes.md` |
| Flycast (portions, via dream-recomp) | `engine/runtime/src/aica/` and others | GPL-2.0 | flyinghead and contributors; see the engine's ADR 1 |
| libchdr | `engine/third_party/libchdr` (submodule) | BSD-3-Clause | rtissera |
| toml++ | `engine/third_party/tomlplusplus` | MIT | Mark Gillard |
| doctest | `engine/third_party/doctest` | MIT | engine tests only |
| SDL3 | fetched to `.deps/`, not committed | zlib | libsdl.org; shipped as SDL3.dll in Windows packages |
| RmlUi 6.3 | fetched at configure time (`cmake/DreamcompFrontend.cmake`), not committed | MIT | Michael R. P. Ragazzon and contributors; the launcher UI, statically linked, incl. its SDL3 platform/renderer backends |
| FreeType 2.14.3 | fetched at configure time, not committed | FTL or GPL-2.0 (dual) | The FreeType Project; used under GPL-2.0 (or FTL); statically linked into the launcher |
| Inter 4.1 | downloaded at configure time (SHA-256 checked), shipped in `<exe dir>/frontend/fonts/` | SIL OFL 1.1 | Rasmus Andersson; licence shipped as `Inter-LICENSE.txt` |
| PromptFont 1.10 | downloaded at configure time (SHA-256 checked), shipped in `<exe dir>/frontend/fonts/` | SIL OFL 1.1 | Yukari "Shinmera" Hafner; controller prompt glyphs; licence shipped as `PromptFont-LICENSE.txt` |
| Vulkan loader | system | Apache-2.0 | |
| Widescreen values for some titles | port sources | — | facts taken from Flycast's `core/cheats.cpp` table (GPL-2.0), credited at the use site |
