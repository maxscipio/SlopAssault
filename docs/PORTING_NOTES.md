# Porting notes: MechAssault (Xbox) on XboxXBERecomp

Third port with this method (after Crimson Skies and Baldur's Gate: Dark Alliance). Brief: `..\MARecomp.txt`, method: `..\PORTING_GUIDE.md`.

## 1. State

| Milestone | State |
|-----------|-------|
| 1. XBE identified, symbols resolved | done (sections 3, 4) |
| 2. First build | done (section 5) |
| 3. Boots to the menu | done: cold start (empty `hdd\Z`) reaches the intro movie, the profile name screen and the hangar briefing in about 15 s (section 6) |
| 4. A level plays with sound and input | in progress: first mission loads, mech/dropship render lit (lighting fix, section 4); sound and input still to check |
| 5. Stable 10+ minute run | |
| 6. Performance | |
| 7. Quality-of-life features | |

## 2. Setup

- Repo `ma-recomp` next to the SDK; `sdk` is a junction to `..\XboxXBERecomp` (ignored by git).
- `assets\default.xbe` (SHA-1 `682DDD1E...51777B`, the root one, not `XDemos\default.xbe`) and `assets\MechAssault.iso` (hardlink of the disc image).
- `src\common` came from the BGDA port (overlay, pad mapper, graphics panel, enhance); `game_info.cpp` renamed (`ma` log prefix, `MechAssault` exe). The F8 graphics panel, F9 pad mapper, quit dialog and `enhance` (SSAO, soft shadows, shadow size, extra edge AA) were removed for the SDK's F1 window; only the F10 FPS counter stays.
- SDK: merged upstream `91146ff` (2 commits after `0fd0044`: point sprites as squares of the program's point size, refresh-rate cap from the monitor)
  into branch `baseline`, with the user's approval. The merge was clean. Upstream's point sprite change overlaps with the BGDA
  `oPts` patch carried in the baseline: check particles once something renders.

## 3. XBE facts

See `..\PORTING_GUIDE.md` section 1. `.text` 0x227460, XDK 4831 for all libraries (the SDK ships `xdk\4831` signatures).

## 4. Hand fixes in `config/game.toml`

`xbe_symbols` result: 138 of 248 functions, 12 of 13 data items (as predicted).

| Item | Result |
|------|--------|
| `_setjmp3` | 0x21D8B8 (`[xbe] setjmp`). Found by the `0x56433230` ("VC20") store; `longjmp` is 0x21D83C. Called only from D3DX image loaders (0x27A2D5, 0x27AC75, 0x280147). |
| `XInputGetCapabilities` | Set to 0. The "constants only" candidate 0x2E443F is an XPP device-request allocator (single caller at 0x2DF388), a false match as in BGDA. A call traps by name. |
| `D3DDevice_SetLight` / `LightEnable` / `SetMaterial` | 0x264C30 / 0x264E10 / 0x2642E0. The fuzzy library search missed them; found by disassembling around the device global `[0x2756A0]` (SetLight copies 0x68 bytes into the device, SetMaterial copies 0x11 dwords to device+0xAB0 and sets dirty bit 0x1000 in `[0x2756AC]`). Left at 0 they ran the guest code, which writes the pushbuffer, so the HLE renderer never saw any light or material: lit fixed-function models (mech, dropship) came out black. |
| Other draw entry points (`DrawVerticesUP`, `SetPalette`, `SetTile`, `RunPushBuffer`, `BeginPush`, `EndPush`) | Left at 0. A fuzzy search against the 5659 `d3d8.lib` finds no candidate for any of them, so the linker did not include them. |
| crt math (`crt_sin/cos/pow/...`) | Left at 0 (inlined x87 or unlinked). `_CIacos`/`_CIasin` resolve. Watch for NaNs. |
| Bink (`BinkDoFrame` 0x2CAA40, `BinkNextFrame` 0x2CC660, `BinkWait` 0x2CB180) | Found from the movie player's update at 0x206D90 (wait, do frame, copy to buffer, goto at the last frame, next frame) and mapped so the SDK's pacing (`xdk\bink.cpp`) holds a frame until the movie's own clock says it is due. Without it a movie steps one frame per game frame, which is 60 fps with the cap off. `BinkGetSummary` stays 0. |
| DSOUND / XNet / WSA | Left at 0; they trap by name in the log if called. |

## 5. Build

First build: `cmake --build out\build\release` (390 steps, 64 partitions, about 20 minutes), log in `out\logs\build1.log`.
A runtime-only change (SDK `runtime\*`) rebuilds in under a minute; a `game.toml` change rebuilds all 64 partitions (about 15 minutes).
Deploy with `tools\iter.ps1 -NoRun`, run from `out\run`.

## 6. Boot triage (milestone 3)

Debugging aids: `XBE_LOG=1` (release build logs every level, flushed per line), `XBE_CAPTURE_DIR` for F3 captures, `tools\debug\shot.ps1`
(screenshot), `tools\debug\rip.ps1 -ProcId <pid> -Exe MechAssault [-Tid n | -All]` (thread sampling). The crash log dumps the registers, the memory
and vtables behind them, and the stack. `tools\xdis.py <hexva> <len>` and `xref.py` disassemble and find callers in `default.xbe`.

| Symptom | Cause | Fix |
|---------|-------|-----|
| Crashes on DSOUND thunks (`IDirectSoundBuffer_*`, `Stream_*`) | The XDK 4831 DSOUND wrappers were found by `xbe_symbols` as raw thunk addresses; some were unmapped | Nine entries in `game.toml` (`SetMixBinHeadroom` 0x29313F, `Pause` 0x29322F, `SetFormat` 0x293B8A, `SetFilter` 0x293BC2, `StopEx` 0x29320B, `Stream_FlushEx` 0x292E40, `Stream_SetMixBinVolumes` 0x2930C6, `SetRolloffFactor` 0x294316, `Buffer_Lock` 0x2932A3). `tools\debug\dsthunks.py` lists the DSOUND thunk table. Still unmapped, map them if they trap: `Unlock`, `SetEG`, `SetLoopRegion`, `SetPlayRegion`, `SetOutputBuffer`, `SetAllParameters`, stream thunks 0x2932D3/D8/DD/E2/E7, 0x293328, 0x293CD4. |
| Hangar loading screen never advances | At start the game copies `D:\Common.mgf` (zlib blocks, 17 MB) to `Z:\filecopy.tmp`, decompressing it to a 96 MB `Z:\fbd13caa.cache`, then renames the file. `NtSetInformationFile` rename failed because the host `FILE*` was still open (Windows) | SDK `files.cpp`: close, rename, reopen |
| After the rename: the cache holds only the first 1 MB, then `C0000005` at guest 0x84BB0 (`menus\main\main.ggf` not found) | The copier issues ReadFile on an overlapped handle through XAPI (0x177D3). The console returns `STATUS_PENDING` there and the copier only decodes a chunk when `ReadFile` reports `ERROR_IO_PENDING` (flag 0x20 at 0x29E1A). The SDK completed the read and returned `STATUS_SUCCESS`, so the copier kept reading all of `Common.mgf` and decoded nothing | SDK `files.cpp`: transfers on handles opened without `FILE_SYNCHRONOUS_IO_*` return `STATUS_PENDING` after completing (status block and event already set) |

How the second one was found: the game's own checks were fine (all 736 blocks of `Common.mgf` inflate in Python; header at +0x20, 12-byte entries
`offset, packed size, crc`, magic `0xC5735A0F`). Temporary tracing in the generated partition (`x86_recomp.2.cpp`, function 0x29190) showed the decode
function was entered once and never again. Generated code cannot call into the runtime (the code DLL has no imports from the exe): trace through
`kernel32` from the partition and restore the file afterwards.

Open items from this milestone:
- First play test, mission 1: crash entering the mission (guest `C0000005`, write at 0xC): the game calls the unmapped stream thunk `0x293CD4`
  (`CDirectSoundStream_SetFormat`, a `jmp` to 0x293B20) with a 6 channel 48 kHz PCM format, so DSOUND's own code ran against the HLE stream object (null
  inner pointer). Mapped in `game.toml`. Not yet re-tested in the mission.
- Speed: the game is a 30 fps title (`CreateDevice` interval 0x80000002 = every second vblank, or immediate) and steps logic and audio per presented frame. The
  SDK's display cap defaulted to 60, so everything ran at double speed and the sound was distorted. The default cap is now 30 (first run, Defaults, Original
  rendering). The engine has no frame timer of the Crimson kind (analysed in the next entry), so `game_fps` / `kTimerPtr` stay unused here and 60 is the
  display limit alone.
- Engine clock (default.xbe, found with `tools\xref.py` / `xdis.py`): the frame loop is at `0x14900`. Its limiter (busy-wait at `0x14a40`, flag `[0x351278]`,
  minimum frame time `[0x351260]`) is dead code: nothing writes either BSS global, and a runtime log read both as 0. The clock is `0x1f280` (tick) over
  `0x1f070` (`KeQueryPerformanceCounter`): `[0x364600]` dt (double, seconds, smoothed over the 64 timestamps at `0x3643e0`), `[0x364610]` total time, `[0x33d8c0]`
  time scale, `[0x364620]` frames. `framerate` in the config parser (`0x207c50`) only fills a min-fps field (default 1/15) of a config object. Measured at
  `fps_cap = 60` in the intro and menus: dt 0.01667, game time tracks wall time 1:1 over 44 s, 60 frames per second. So the engine is variable-timestep and
  the clock needs no patch; the original 30 comes from `CreateDevice` interval 0x80000002 (vsync every second vblank), which the SDK does not emulate.
  Not yet checked: gameplay at 60 (physics, AI, animation and audio pacing in a mission). Anything that still runs per frame would need its own hook.
- The name entry screen showed a stray `m` in "Mech Warriorm" in one run. Input injection or a real key press; not looked into yet.
- "audio: output starved, queueing 40 ms" appears in the log while the music streams. Check once audio is judged.
- Particles: the baseline carries the BGDA `oPts` patch and upstream has its own point sprite change. Check when particles are on screen.

## 7. Picture aspect, HUD anchoring and the CRT look

The SDK now does the generic parts: F1 has the aspect setting (including ultrawide), **Console look** (4:3 at the game's own resolution) and a **CRT filter**. The port no longer registers a `gpu::Extension` (the SDK stretches the screen shaped targets for ultrawide only without one), and the old patch's own aspect picker, HUD anchoring and CRT pass are kept as `games\mechassault\own-aspect-and-crt.patch` in the SDK, to apply on top of `11e0415` if the HUD needs its anchoring. What it found:

- **Hook (`src\game\hooks.cpp`):** `sub_000390F0` runs the camera's projection and, through `sub_000381B0`, its culling planes; both read the rectangle `[cam+0x9C] / [cam+0xA0]`. The hook widens the width by `max(1, app::Aspect() * 0.75)` and the horizontal field at `[cam+0x88]` by the same factor (the vertical field is that divided by the aspect) while the original runs, and restores them. `0x381B0` has no other caller. The same function sets the device viewport from the rectangle through `sub_00037D80`; a second hook keeps that at 640 wide, as the renderer stretches the viewport across the screen.
- **Ultrawide:** `OnInit` in `game_info.cpp` sets `app::g_aspect` to 21:9 when the aspect setting is "Fill the screen" and the output is narrower, so a 16:9 screen gets bars above and below. On a 21:9 or wider output it is left unset and the SDK fills the screen.
- The game renders a fixed 640 x 480 with a 4:3 projection (focal 496 px; the matrix comes from the camera method at `0x390F0` through `SetTransform(1)`, m00 / m11 = 0.75).
  The patch leaves the projection alone: `ShadowHooks::Position()` squeezes clip space about the centre by `4/3 / aspect` and the stretch to the window widens it, so a 16:9 or 21:9 window shows more world at the sides (Hor+) with the vertical field unchanged.
- The HUD is drawn pretransformed (FVF with rhw, vs handle `0x144` / `0x44`), in raw 640 x 480 pixels. `Anchor()` classifies each box: at least 0.85 of the width = full screen (cover), ending in the left 42 % = left corner (radar), starting in the right 42 % = right corner (health, ammo), anything else is centred (crosshair). A pretransformed vertex has `w = 1 / rhw = 1`, which is how the hook tells a 2D quad from the world.
- Culling: the camera builds its frustum planes in `0x381B0` (perspective) from the 4:3 rectangle at `[cam+0x9C]`. The patch's override widens that width by `aspect * 0.75` while the planes are built, then restores it; without it objects at the sides pop out of view.
- Original rendering: the patch's CRT pass was promoted to the SDK (`crt`, `crt_scanlines`, `crt_softness`, `crt_mask`, `crt_curve` in `settings.toml`).
- Not checked: the HUD, menus, loading screens and movies at 16:9 and 21:9 with the hook above (the HUD is 4:3 and the SDK squeezes 2D into a centred 16:9 part), and the 3D at the sides.

## 8. Flickering dark and light wedges on the road

Symptom: hard edged, triangle shaped dark and light patches on the road and ground that changed with the camera and the mech, on D3D12 and Vulkan, at any aspect. Not seen on the console.

- **Pass:** each frame draws the static 256 x 256 lightmap (texture `837CE000`, world XZ over 2048 units, building and cliff shadows) into a render target, the terrain's pixel shader samples it, and the road pieces are then redrawn with vertex shaders `B0906E61` and its variants. Those draws multiply the lightmap in (blend DESTCOLOR * ZERO, fixed function pixel stage, stencil GEQUAL 1 / DECR_WRAP so each pixel is modulated once, depth LEQUAL with no depth write).
- **Cause:** the redraw tests LEQUAL against the depth the first road pass wrote through another transform (fixed function against the game's own matrix), so the depths differ in the last bits. The console's 24 bit depth buffer absorbs that; the host's reversed float depth does not, so the multiply failed per pixel in patches that follow the triangles.
- **Bisect:** skipping vertex shader `B0906E61` removed the near patches; disabling its stencil did not; disabling its depth test removed them; a depth offset toward the camera did. Ruled out: the stencil shadow volumes (`B0906441`), depth clip, the unit texcoord clamp heuristic, the lightmap contents and the upstream merge.
- **Fix (SDK `d7bb693`):** a depth tested LEQUAL draw with no depth write moves 64 console depth steps toward the camera (`kRedrawDepthSteps`, `FillDepthConstants` in both `d3d12.cpp` and `vulkan.cpp`; z buffers only). 512 host depth ULPs fixed the near patches but not the far ones; 64 console steps fixed both. A toward the camera offset also now stops at the near plane (`ReversedClip` in `shaders.cpp`): without it the menu, whose quads sit at the near plane, was clipped away and drawn black.
- **Tried, no effect:** xemu's fix for this game's shadow at certain angles (PR #3056, clear handling of the X1A7R8G8B8 formats) is a clear on the NV2A register level and has no counterpart in the SDK's clears.
- **Debug aids that found it:** `XBE_CAPTURE_DIR` / `XBE_CAPTURE_EVERY` frame captures (`frame.txt` lists the draws with their vertex shader handle, state and stencil), and temporary hooks skipping a draw by vertex shader handle or changing its state, read from a text file every few frames. Give such a file an absolute path: the game is often launched from somewhere else.
