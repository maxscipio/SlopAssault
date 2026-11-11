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
- `src\common` comes from the BGDA port (overlay F10/F12, pad mapper F9, graphics panel F8, enhance); `game_info.cpp` renamed (`ma` log prefix, `MechAssault` exe).
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
| Bink (`BinkDoFrame`, `BinkGetSummary`, `BinkNextFrame`, `BinkWait`) | Left at 0, as in Crimson where movies still played. Skipping them by HLE is the fallback. |
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
