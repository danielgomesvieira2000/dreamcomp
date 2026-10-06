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
  has no new frame and shows one twice. Fixing that means running the game at the display's rate
  (+0.1 %), a choice against "never change the logic rate"; not done.
- **Performance has headroom** at every scale on Iris Xe: GPU at most 9 ms of 16.7.
- **Latency is the weak point**: 48 ms frame start to photons. Intel's Vulkan driver presents
  windowed *and* borderless-fullscreen swapchains as "Composed: Copy with GPU GDI" (a copy, then
  DWM composition), about two refreshes from present to photons. The driver offers only FIFO and
  IMMEDIATE (mailbox falls back to FIFO), and `VK_EXT_full_screen_exclusive`.
- OBS's implicit Vulkan layer (`VK_LAYER_OBS_HOOK`) is installed on this machine; disabling it
  changes nothing measurable.

## Changes

| Date | Change | Result |
|---|---|---|
| 2026-10-06 | Pacing sleep fixed (dangling `paused_for`; engine-changes.md) | runs without vsync held to 1.0x |
| 2026-10-06 | Pads read after the pacing sleep, one window poll per frame (engine-changes.md) | input age at present: mean 9.5 ms, p50 8.7 (was one full frame, ~16.7 ms, by construction); scripted fight pixel-identical at frame 2500; Escape still opens the menu |

## Known issue

- Quit Game from the in-game menu during a fight once left the process running with no window,
  one thread spinning in game code (2026-10-06, older build; dump `work/hang-exit.dmp`, map
  `work/hang-old.map`). Not reproduced: closing the window during boot, mid-fight, with the menu
  open, and fullscreen all exit.
