# Frame pacing and latency

What the player sees: every game frame shown for exactly one refresh, little time between input and
photons, and headroom so a heavy frame never misses. Measured from the display's side with Intel
PresentMon (`tools/pacing.py`), plus the engine's own `pacing:` report lines.

## How to measure

```
python tools/pacing.py                                    # fight, windowed, scale 2, 30 s recorded
python tools/pacing.py --out work/pace/x --set scale=3 -- --fullscreen
python tools/pacing.py --compare work/pace/a work/pace/b
```

PresentMon console: `winget install Intel.PresentMon.Console`; members of Performance Log Users
need no admin. Columns used: `FrameTime` (present to present), `DisplayedTime` (how long a frame
stayed on screen: one refresh is perfect, two is a repeated frame), `DisplayLatency` (frame start
to photons), `CPUBusy`/`GPUBusy`, `PresentMode`. The engine prints `pacing:` lines: guest frame
intervals, late frames with their cause, and the input age (pads read -> that frame presented).

## Baseline, 2026-10-06 (Iris Xe laptop, 60 Hz panel, Soulcalibur fight, real speed)

| Run | Present mode | Frame time p50 / p99 / max | Shown once / held 2+ / dropped | Display latency | GPU busy |
|---|---|---|---|---|---|
| windowed, scale 2 | Composed: Copy with GPU GDI | 16.67 / 18.0 / 25.1 ms | 1794 / 1 / 0 (30 s) | 48.0 ms | 7.6 ms |
| fullscreen (`--fullscreen`), scale 2 | Composed: Copy with GPU GDI | 16.66 / 18.0 / 34.0 ms | 1195 / 1 / 0 (20 s) | 48.0 ms | 7.9 ms |
| scale 1 / 3 / 4, windowed | same | p99 19.6 / 18.1 / 18.2 ms | 1 held per 20 s each | 47.5-47.8 ms | 3.0 / 8.2 / 9.0 ms |
| `DISABLE_VULKAN_OBS_CAPTURE=1` | same | 16.67 / 17.9 / 25.7 ms | 1195 / 1 / 0 | 47.9 ms | 7.0 ms |
| `--present-mode immediate` | same | 16.75 / 22.3 / 40.4 ms | 824 / 35 / 35 | 46.1 ms | 5.6 ms |

Findings:

- **Pacing is even.** Every frame but one per ~17-20 s is shown exactly once. The exception is the
  refresh mismatch: the Dreamcast runs at 59.94 Hz, the panel at 60 Hz, so every 16.7 s the panel
  has no new frame and shows one twice. Fixed by display sync (below).
- **Performance has headroom** at every scale on Iris Xe: GPU at most 9 ms of 16.7.
- **Latency is the weak point**: 48 ms frame start to photons. Intel's Vulkan driver presents
  windowed *and* borderless-fullscreen swapchains as "Composed: Copy with GPU GDI" (a copy, then
  DWM composition), about two refreshes from present to photons. The driver offers only FIFO and
  IMMEDIATE (mailbox falls back to FIFO), and `VK_EXT_full_screen_exclusive`.
- OBS's implicit Vulkan layer (`VK_LAYER_OBS_HOOK`) is installed on this machine; disabling it
  changes nothing measurable.
- **The game is slower per frame at real speed than flat out**: host time per guest frame p50
  9-11 ms, p99 ~16 ms at real speed, against ~4.4 ms average unthrottled (`DREAM_PROFILE=1`:
  guest code, audio and render submission all 2-2.6x slower). A busy-wait instead of the pacing
  sleep brings it to p50 4.75 / p99 9.2 ms, so it is the CPU clocking down while the thread sleeps
  (i5-1335U, Balanced plan), not GPU waits. Opting out of power throttling and MMCSS "Games" made
  no difference. Not fixed: spinning a core costs a laptop power and heat, and with today's
  presentation the frame time still fits (all frames shown once).

## Display sync (setting `frame_timing`, default `display`; D-008)

The pacing clock runs at the display's rate when that is within 0.5 % of the game's (DWM's
measured period: 60.003 Hz here, so x1.00087), aimed 0.01 % under it so the swapchain queue stays
drained. Result over 60 s (PresentMon): **0 repeated frames** (2 without), frame time sd 0.59 ms
(0.67), latency 48.0 ms and GPU busy 8.2 ms unchanged. On a 120/144 Hz display, with
interpolation, or off Windows (no vblank grid yet) the guest clock paces as before.

Tried and dropped (2026-10-06):

| Attempt | Result |
|---|---|
| Phase lock: present a margin (1-12 ms) before each vblank, emulation scheduled just in time | 40-75 repeated frames per 30 s (emulation time varies 4-16 ms and there is no queue slack left); with a final 2-4 ms busy-wait 3-5 per 30 s; display latency 48.0 ms at every margin: on this copy path the moment of presentation inside a refresh does not change latency |
| `VK_EXT_full_screen_exclusive` | `ALLOWED`: still the GDI copy; `APPLICATION_CONTROLLED`: acquire fails (-3) and the swapchain is rebuilt every frame |
| Power-throttling opt-out, MMCSS "Games" for the game thread | no change in host time per frame |
| DXGI flip model (standalone D3D11 probe, not the game) | "Hardware Composed: Independent Flip", windowed and fullscreen, display latency 31.9 ms against the game's 48: about one refresh less. A Vulkan -> DXGI present path is the candidate next step |

## Changes

| Date | Change | Result |
|---|---|---|
| 2026-10-06 | Pacing sleep fixed (dangling `paused_for`; engine-changes.md) | runs without vsync held to 1.0x |
| 2026-10-06 | Display sync, setting `frame_timing` (above) | 0 repeated frames in 60 s (was 2) |
| 2026-10-06 | Pads read after the pacing sleep, one window poll per frame (engine-changes.md) | input age at present: mean 9.5 ms, p50 8.7 (was one full frame, ~16.7 ms, by construction); scripted fight pixel-identical at frame 2500; Escape still opens the menu |

## Known issue

- Quit Game from the in-game menu during a fight once left the process running with no window,
  one thread spinning in game code (2026-10-06, older build; dump `work/hang-exit.dmp`, map
  `work/hang-old.map`). Not reproduced: closing the window during boot, mid-fight, with the menu
  open, and fullscreen all exit.
