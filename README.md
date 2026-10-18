<p align="center"><img src="docs/logo.png" alt="Mabecs International Superstar Soccer 2000" width="640"></p>

# Mabecs International Superstar Soccer 2000 - Widescreen build

**A true widescreen version of the Nintendo 64 classic.** The original game was made for 4:3 TVs; this build renders
the full 16:9 (or wider) view at your screen's resolution. Players and the ball stay visible all the way to the edges
of the screen, and the stadium sky fills the whole width instead of leaving black bars.

<p align="center"><img src="docs/screenshot-widescreen.jpg" alt="Widescreen match: Nigeria vs Germany" width="100%"><br>
<em>A match in native 16:9 widescreen at 2560x1440, with the original HUD.</em></p>

Native Linux port of **International Superstar Soccer 2000** (Nintendo 64: USA Rev 1 and both European versions) made by static
recompilation with [N64Recomp](https://github.com/N64Recomp/N64Recomp), running on
[N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) with the [RT64](https://github.com/rt64/rt64)
renderer.

**No game data is included.** You need your own ROM. One AppImage supports all three versions; the launcher detects
which one you give it:

| ROM | SHA-1 |
|---|---|
| `International Superstar Soccer 2000 (USA) (En,Es) (Rev 1)` | `47a993fe1316dfd9e208603370d3dddae05f1d1b` |
| `International Superstar Soccer 2000 (Europe) (En,De)` | `e7078e5f1e899382b1cc145df43ee8cfaa924297` (as .z64) |
| `International Superstar Soccer 2000 (Europe) (Fr,It)` | `b0505e13ba0f029ea735378c50b224fd618be302` |

`.z64`, `.n64` and `.v64` byte orders are all accepted. The European versions have their own languages (set under
Option > Language) and a harder single-player game.

## New in v1.2: European versions
v1.2 adds support for both European (PAL) releases of the game, in the same AppImage as the USA version:
- **Europe (En,De)**: English and German. **Europe (Fr,It)**: French and Italian (menus, messages and commentary).
  The language is set in the game under Option > Language.
- **European-only content**: the **International Cup** tournament mode, and a harder single-player game.
- Same widescreen, sky and culling fixes as the USA version.
- Runs at 50 Hz like on a PAL console; speed, match clock and audio are what the European game was made for.
- The launcher detects which ROM you give it; with several ROMs stored, the button next to "Change ROM..." switches
  between versions. Controller Pak saves are kept separate per version.

<p align="center"><img src="docs/screenshot-international-cup.jpg" alt="International Cup, European versions" width="80%"><br>
<em>The International Cup, only in the European versions.</em></p>

## Features
- **Widescreen**: 16:9 and wider, with the game's culling widened (no player pop-in at the edges) and the stadium sky
  extended to the full width; HUD stays in its original layout
- Window-scaled resolution, 4x MSAA
- Controller Pak saves (raw 32 KB `.mpk`, Project64-compatible), kept separately per version
- Launcher: ROM selection (file picker or drag & drop), version choice when several ROMs are stored,
  Fullscreen/Widescreen options
- Gamepad and keyboard input

## Download
Get the AppImage from [Releases](../../releases). Runs on Debian 12+, Ubuntu 22.04+ and newer distros
(glibc 2.34+, Vulkan GPU). Data is stored in `~/.local/share/iss2000recomp`.

## Controls
Gamepad or keyboard (arrows/WASD stick, X = A, C = B, Z = Z, Enter = Start, Q/E = L/R, IJKL = C buttons).
**F11** toggles fullscreen, **Esc** quits.

## Notes
- The European versions run at 50 Hz like on a PAL console (matches at 25 fps instead of 30). Their game speed, match
  clock and audio are tuned for that, so they play at the intended speed; they can't run at 60 Hz.
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
RT64; see NOTES.md); apply `patches/N64ModernRuntime-vi-rate.patch` to N64ModernRuntime (50 Hz support for the
European versions). Generated code is produced locally from your ROMs and is not part of this repository.

## License
GPL-3.0 (see `LICENSE`), as the program includes N64ModernRuntime and code adapted from Zelda64Recomp.
International Superstar Soccer is a trademark of Konami. Not affiliated with Konami or Nintendo.
