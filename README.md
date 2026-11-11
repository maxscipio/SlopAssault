# MechAssault - XBE Recomp

An unofficial native PC port of the original Xbox version of MechAssault, made by statically recompiling the game's code with [Xbox XBE Recomp](https://github.com/TGP482/XboxXBERecomp). The game runs on the SDK's own kernel, Direct3D 12 and audio layers.

> [!NOTE]
> This repository contains no game files and no game code. You bring your own legally acquired copy of the game: the disc image and its `default.xbe`. Building recompiles that `default.xbe` on your PC, and nothing derived from it is ever committed.

## Status

Work in progress. The game boots to the menu, the profile screen and the hangar, and the first mission loads with lit models. Sound, input and a stable 10+ minute run in a mission are still being checked. See [docs/PORTING_NOTES.md](docs/PORTING_NOTES.md) for what is known to work, what is not, and how each problem was found.

## What you need

- Windows 10 or 11, 64-bit, and a GPU with Direct3D 12 support.
- A disc image of the Xbox version of MechAssault (`.iso`, a whole dump or the game partition). The build expects the retail `default.xbe` of the disc, SHA-1 `682ddd1ecda1e5110f9339dab0c31d296d51777b`, not the one in `XDemos`.
- [Git](https://git-scm.com/download/win).
- Visual Studio 2022 with **Desktop development with C++**, **C++ Clang tools for Windows** and **C++ CMake tools for Windows**. `build.bat` offers to add the last two.
- Python 3, only to take `default.xbe` out of the disc image (below).
- About 10 GB of free disk space and 30 to 90 minutes for the first build.

## Building

1. Clone this repository.
2. Put the disc image in the `assets` folder, and `default.xbe` beside it (the build recompiles it). To take it out of the image:

   ```
   python tools\xiso.py assets\MechAssault.iso extract assets default.xbe
   ```

   (This also writes the demo's `assets\XDemos\default.xbe`, which is not used and can be deleted.)
3. Run `build.bat`.

The first run downloads and patches the SDK into `sdk\` (XboxXBERecomp at the commit `patches\sdk.patch` was made against, see [patches/README.md](patches/README.md)), fetches the libraries it uses, builds the recompiler, recompiles `default.xbe` into `generated\` and compiles it. Later runs only rebuild what changed. It ends by setting up `out\run`, with the executable and a hard link to your disc image, and offers to start the game.

Play with `out\run\MechAssault.exe`, not the one in `out\build\release`. The exe looks for an `assets` folder beside itself or in a parent folder; if that folder holds a `default.xbe` it treats it as extracted game files and ignores the disc image, which crashes the game. `out\run\assets` holds only the image. Settings, logs, saves and the shader cache are kept beside the exe.

`clean.bat` removes everything the build and the game generate (`out\` and `generated\`) and keeps your `assets` folder; `clean.bat all` removes the game files too. `release.bat` packs a zip with an empty `assets` folder, ready to share without any game content.

## Controls and settings

| Key | Action |
|-----|--------|
| F8 | Graphics panel: window mode, resolution, render scale, vsync, display limit, anti-aliasing, SSAO, soft shadows, shadow map size, anisotropic filtering, shader cache, an "Original rendering" switch and Defaults |
| F9 | Controller setup |
| F10 | FPS counter |
| F12 | Quit (also Start + Back held on a controller) |

Xbox, PlayStation and Switch controllers work, with hot plugging and rumble. Everything is saved in `settings.toml` beside the exe.

MechAssault was made for 30 frames per second, so the display limit defaults to 30. The engine's clock is time-based, so the limit can be raised in the F8 panel; how the rest of the game behaves above 30 has not been checked in a mission yet.

## Repository layout

| Folder | Content |
|--------|---------|
| `src\` | Host code: the F8/F9/F10/F12 screens and game hooks (`src\game\hooks.cpp`) |
| `config\game.toml` | Function and data addresses found for `default.xbe`, with the hand fixes |
| `patches\` | The changes to the SDK, as one diff and a table |
| `docs\` | Porting notes |
| `tools\` | Scripts to inspect `default.xbe` and the disc image, and to rebuild and run |

## Credits

- [Xbox XBE Recomp](https://github.com/TGP482/XboxXBERecomp), the recompiler and runtime this is built on.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [Zydis](https://github.com/zyantific/zydis) for decoding x86 instructions, [toml++](https://github.com/marzer/tomlplusplus) for the configs, [Dear ImGui](https://github.com/ocornut/imgui) for the settings overlay and [SDL](https://github.com/libsdl-org/SDL) for the window, input and controllers.

MechAssault is a trademark of Microsoft Corporation. This project is not affiliated with or endorsed by Microsoft or the original developers.
