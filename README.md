# Mabecs International Superstar Soccer 2000 - Widescreen build

Native Linux port of **International Superstar Soccer 2000** (Nintendo 64, USA Rev 1) made by static
recompilation with [N64Recomp](https://github.com/N64Recomp/N64Recomp), running on
[N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) with the [RT64](https://github.com/rt64/rt64)
renderer.

**No game data is included.** You need your own ROM: `International Superstar Soccer 2000 (USA) (En,Es) (Rev 1)`,
SHA-1 `47a993fe1316dfd9e208603370d3dddae05f1d1b`.

## Features
- Widescreen with widened culling (no player pop-in) and a stretched stadium sky
- Window-scaled resolution, 4x MSAA
- Controller Pak saves (raw 32 KB `.mpk`, Project64-compatible)
- Launcher: ROM selection (file picker or drag & drop), Fullscreen/Widescreen options
- Gamepad and keyboard input

## Download
Get the AppImage from [Releases](../../releases). Runs on Debian 12+, Ubuntu 22.04+ and newer distros
(glibc 2.34+, Vulkan GPU). Data is stored in `~/.local/share/iss2000recomp`.

## Known issues
- **Fullscreen on high-refresh monitors can run the game at double speed.** Use windowed mode for now.
- No rumble; no controller remapping yet.

## Building
See [NOTES.md](NOTES.md) for the full pipeline (overlay discovery, symbol generation, recompilation, hooks,
Debian 12 portable build, AppImage packaging). Third-party sources go in `ext/` (N64Recomp, N64ModernRuntime,
RT64; see NOTES.md). Generated code is produced locally from your ROM and is not part of this repository.

## License
GPL-3.0 (see `LICENSE`), as the program includes N64ModernRuntime and code adapted from Zelda64Recomp.
International Superstar Soccer is a trademark of Konami. Not affiliated with Konami or Nintendo.
