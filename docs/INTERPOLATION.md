# High frame rate: geometry interpolation

## Why not "matrix interpolation"

N64 ports interpolate matrices because the N64's RSP receives matrices and transforms vertices on
the GPU side, where RT64 can blend them. A Dreamcast game transforms every vertex on the SH-4 and
hands the PowerVR2 finished screen-space vertices: the display list holds no matrices. The
equivalent here is to blend the **geometry** of two consecutive game frames. The game's logic
keeps running at its own 60 Hz (series rule: never change the logic rate).

## Method

1. Each game render produces a decoded `Frame` (strips with screen-space vertices).
2. Strips of frame N+1 are paired with strips of frame N (`dreamcomp::match_frames`,
   `include/dreamcomp/geometry_match.h`): same parameter words and vertex count, in submission
   order, with a small lookahead for strips that appear or disappear.
3. An in-between frame is drawn with every paired strip's vertices blended at t = 0.5 (position
   and 1/w; texture coordinates and colours from the newer frame), unpaired strips as in the newer
   frame. HUD primitives do not move, so they blend to themselves.
4. Presentation: in-between frame, then the real frame half a game-frame later. One game frame of
   added latency (~16.7 ms). Only useful on displays above 60 Hz; off otherwise.
5. Camera cuts (median displacement far above normal) skip the in-between frame.

## Gates

| Gate | Measurement | Status |
|---|---|---|
| G1 Correspondence | `dreamcomp_ta_match` on consecutive fight frames: ≥ 95 % of vertices paired, displacement distribution plausible | **passed** 2026-10-05: 7 consecutive Soulcalibur fight pairs, 97.8-100 % of vertices paired, median 2-3 px / p90 5-13 px per frame; outliers are off-screen or behind-camera vertices (clipped by the game) and a few mispairs, so strips moving implausibly far are not blended |
| G2 Visual | in-between frames look like a frame of the game (no torn strips) on contact sheets of fights, menus, cuts | — |
| G3 Cost | per-frame matching + second render within budget on Iris Xe (`DREAM_PROFILE=1`) | — |
| G4 Pacing | 120 Hz output smooth (frame times even: `frame_motion.py`-style check), audio unaffected | — |

## Tools

- `dreamcomp_ta_match A.ta B.ta [--window N]` — pairing coverage and displacement.
- `--dump-ta FILE --dump-ta-at-frame N --dump-ta-count K` — K consecutive display lists.
