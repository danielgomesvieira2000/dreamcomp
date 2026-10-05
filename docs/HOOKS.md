# Hooks: replacing or wrapping game functions

## Using it

1. Find the function's entry address (load-address spelling, `0x8C......`) and check it is a
   discovered function: `build/game/gen/<id>.functions.json` lists every entry.
2. Name it in `game/<id>.toml`:

   ```toml
   [hooks]
   0x8C0ABCDE = "camera_update"        # entry and exit
   0x8C0F0000 = "draw_hud@entry"       # entry only
   0x8C012345 = "set_viewport@exit"    # exit only
   ```
3. Define each named side in a port source file:

   ```cpp
   #include "dreamcomp/hook.h"

   DC_HOOK_ENTRY(draw_hud) {
       // c.r[4..7]: integer/pointer arguments; c.fr[4..11]: float arguments (Hitachi ABI)
       return false;          // false: run the original; true: skip it (set c.r[0] yourself)
   }
   DC_HOOK_EXIT(set_viewport) {
       dreamcomp::write_f32(m, 0x8C148618, 853.33f);   // after the game computed it
   }
   ```
4. Rebuild. The translator prints `hook <name> on 0x...` per hook. An address that is not a
   function entry fails the translation; a named side without a definition fails the link.

Rules of thumb:
- **Exit hooks over vblank patches** when the game recomputes a value: the exit hook runs right
  after the computation, so no frame ever sees an inconsistent pair.
- Hooks run on the guest thread inside guest code: no blocking, no allocation per call on hot paths.
- A hook changes behaviour only for direct and indirect calls to the function's entry; code that
  jumps into the middle of it bypasses the hook.

## How it works (to build the same thing elsewhere)

- The translator emits every guest function `F` as a body `F__resume(c, m, pc)` plus a wrapper
  `F(c, m)`. All calls go through the wrapper (direct calls by name, indirect calls through the
  function table).
- `[hooks]` sets `FunctionSpec::hook/hook_entry/hook_exit` (`engine/translator/src/main.cpp`), and
  the wrapper becomes
  `if (dream_hook_entry_N(c, m)) return; F__resume(c, m, 0); dream_hook_exit_N(c, m);`
  with the hook functions declared `extern` in the generated unit
  (`engine/translator/src/emit/emit.cpp`).
- `DC_HOOK_ENTRY/EXIT` (`include/dreamcomp/hook.h`) open `namespace dream::gen` and define the
  matching symbols, so the generated unit links against the port's definitions.
- Cost: one direct call per hooked function call; unhooked functions are unchanged.
