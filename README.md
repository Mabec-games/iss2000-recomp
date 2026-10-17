<p align="center"><img src="docs/logo.png" alt="Mabecs International Superstar Soccer 2000" width="640"></p>

# Mabecs International Superstar Soccer 2000 - Widescreen build

**A true widescreen version of the Nintendo 64 classic.** The original game was made for 4:3 TVs; this build renders
the full 16:9 (or wider) view at your screen's resolution. Players and the ball stay visible all the way to the edges
of the screen, and the stadium sky fills the whole width instead of leaving black bars.

<p align="center"><img src="docs/screenshot-widescreen.jpg" alt="Widescreen match: Nigeria vs Germany" width="100%"><br>
<em>A match in native 16:9 widescreen at 2560x1440, with the original HUD.</em></p>

Native Linux port of **International Superstar Soccer 2000** (Nintendo 64, USA Rev 1) made by static
recompilation with [N64Recomp](https://github.com/N64Recomp/N64Recomp), running on
[N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) with the [RT64](https://github.com/rt64/rt64)
renderer.

**No game data is included.** You need your own ROM: `International Superstar Soccer 2000 (USA) (En,Es) (Rev 1)`,
SHA-1 `47a993fe1316dfd9e208603370d3dddae05f1d1b`.

## Features
- **Widescreen**: 16:9 and wider, with the game's culling widened (no player pop-in at the edges) and the stadium sky
  extended to the full width; HUD stays in its original layout
- Window-scaled resolution, 4x MSAA
- Controller Pak saves (raw 32 KB `.mpk`, Project64-compatible)
- Launcher: ROM selection (file picker or drag & drop), Fullscreen/Widescreen options
- Gamepad and keyboard input

## Download
Get the AppImage from [Releases](../../releases). Runs on Debian 12+, Ubuntu 22.04+ and newer distros
(glibc 2.34+, Vulkan GPU). Data is stored in `~/.local/share/iss2000recomp`.

## Controls
Gamepad or keyboard (arrows/WASD stick, X = A, C = B, Z = Z, Enter = Start, Q/E = L/R, IJKL = C buttons).
**F11** toggles fullscreen, **Esc** quits.

## Notes
- The in-game Resolution option always runs in the normal mode: RT64 already renders at your screen's
  resolution, and the N64's 640x480 High mode broke speed/widescreen when switched mid-game.

## Known issues
- **v1.0 only:** the in-game Resolution: High option works as intended in windowed mode only. In fullscreen on
  high-refresh-rate monitors (e.g. 144/240 Hz) it can make the game run at double speed. If you play fullscreen on
  v1.0, set Resolution to Low in the game's options (v1.1 and later do this automatically).
- No rumble; no controller remapping yet.

## Building
See [NOTES.md](NOTES.md) for the full pipeline (overlay discovery, symbol generation, recompilation, hooks,
Debian 12 portable build, AppImage packaging). Third-party sources go in `ext/` (N64Recomp, N64ModernRuntime,
RT64; see NOTES.md). Generated code is produced locally from your ROM and is not part of this repository.

## License
GPL-3.0 (see `LICENSE`), as the program includes N64ModernRuntime and code adapted from Zelda64Recomp.
International Superstar Soccer is a trademark of Konami. Not affiliated with Konami or Nintendo.
