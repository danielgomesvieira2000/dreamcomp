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
| DXGI flip model (standalone D3D11 probe, not the game) | "Hardware Composed: Independent Flip", windowed and fullscreen, display latency 31.9 ms against the game's 48: about one refresh less. Built: next section |

## Latency setting: DXGI presentation (`latency = low`, opt-in; D-009)

`engine/render/src/vk/dxgi_present.cpp`: Vulkan renders into two textures shared with a D3D11 device
on the same GPU (matched by LUID; `VK_KHR_external_memory_win32`, keyed mutex), D3D11 copies the
finished one into a flip-model swapchain (5 buffers) and presents. `--present-path dxgi` /
`DREAM_PRESENT_PATH`; falls back to the Vulkan swapchain when anything fails to initialise. The
report prints a `dxgi:` line (skips, queue depth, time in AcquireSync / copy / Present).
Validation layers: no messages. Fullscreen on/off, resizes (DXGI buffers and shared textures
rebuilt), the in-game menu, `--screenshot-presented`, 120 fps interpolation and
`--present-mode immediate` all checked.

What it took to make it pace (60-90 s PresentMon runs, Iris Xe, i5-1335U):

| Configuration | Display latency | Repeated / dropped frames | Notes |
|---|---|---|---|
| Vulkan swapchain (default), display sync | 47.5-48.0 ms | 0-1 / 0 per minute | plugged in and on battery |
| DXGI, latency 1, waiting on the latency object | 32.2 ms | 3 / 0 per 30 s | blocked the game thread: 14 frames over 20 ms a minute |
| DXGI, sync interval 0 (mailbox) | 33.6 ms | 177 / 181 per minute | Intel drops "Hardware: Independent Flip" frames even with perfectly regular presents |
| DXGI, sync interval 1, queue unchecked | 52-58 ms | 0 / 0 | the queue fills to two and stays |
| DXGI, queued, skip when two wait, phase lock (*) | 34.7 ms | 2 / 0 per minute | input age 12.5 ms; power state not recorded, inferred plugged in (emulation p50 11.5 ms) |
| same + 3 ms busy-wait before each frame (*) | 35.8 ms | 1 / 0 per minute | input age 6.9 ms (emulation 5 ms at full clocks) |
| same as (*), on battery | 33.9-34.0 ms | 25-53 / 0 per minute | emulation p90 16.6-17.1 ms: frames miss their vblank |
| DXGI at depth 3 with phase lock, on battery | 47.4 ms | 7 per 90 s | fixed slots never make up a late frame |
| DXGI with the rate clock, on battery | 52.1 ms | 0 per 90 s | catches up, but the queue is deep |

Why: on this laptop a frame takes 11-17 ms to emulate at the clocks it runs at while pacing (5 ms
when kept busy), plus ~8 ms of GPU work. That fits a 16.7 ms refresh only with a frame queued: a
queue of ~3 frames never repeats one and costs ~48 ms whichever API presents it; one frame
queued is ~35 ms but repeats a frame whenever one is late.

So `latency = low` is opt-in. What it does: queued presents (sync interval 1); a present is
skipped (one repeat) when two frames already wait, so a late frame does not leave a permanent
extra refresh of latency; frames are phase-locked to the vblank grid (start so that the slowest
recent frame is presented 3 ms before its vblank; held until then if early; Windows
high-resolution waitable timer for the waits) while one frame waits. When skips come often (2 in
30 s) the presenter lets one more frame wait (up to 3) and pacing returns to the rate clock,
which catches up; it retries one less after 10 s, backing off to 4 minutes. `DREAM_SPIN_MS=N`
busy-waits the last N ms of each wait (fresher input, more power); `DREAM_DXGI_MIN_DEPTH`,
`DREAM_DXGI_LATEST=1`, `DREAM_PHASE_LOCK=0` exist for measurements.

Plugged in (charger confirmed, 90 s fight, latency per 10 s of the capture):

| Setting | Display latency | Repeated frames |
|---|---|---|
| Standard | 47.7-48.1 ms in every slice | 1 in 90 s |
| Low, first retry after 60 s | 50-60 ms for 45 s (deepened in the boot and loading screens), then 34.8-35.1 ms | 1 in 90 s |
| Low, first retry after 10 s (current) | 35.0 ms, one 20 s stretch at 44.8 ms (pre-round camera sweep), mean 37.0 ms | 3 in 90 s |

So plugged in, Low is ~11-13 ms less delay for about two more repeated frames a minute. On battery
it is not worth it (above). Not verified: a display above 60 Hz; other GPUs.

## Changes

| Date | Change | Result |
|---|---|---|
| 2026-10-06 | Pacing sleep fixed (dangling `paused_for`; engine-changes.md) | runs without vsync held to 1.0x |
| 2026-10-06 | Display sync, setting `frame_timing` (above) | 0 repeated frames in 60 s (was 2) |
| 2026-10-06 | DXGI presentation, setting `latency` (opt-in, above) | ~35 ms instead of ~48 where frames are on time; default unchanged |
| 2026-10-06 | Pads read after the pacing sleep, one window poll per frame (engine-changes.md) | input age at present: mean 9.5 ms, p50 8.7 (was one full frame, ~16.7 ms, by construction); scripted fight pixel-identical at frame 2500; Escape still opens the menu |

## Known issue

- Quit Game from the in-game menu during a fight once left the process running with no window,
  one thread spinning in game code (2026-10-06, older build; dump `work/hang-exit.dmp`, map
  `work/hang-old.map`). Not reproduced: closing the window during boot, mid-fight, with the menu
  open, and fullscreen all exit.
