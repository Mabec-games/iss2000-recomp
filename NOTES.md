# ISS 2000 (USA Rev 1) static recomp — research notes

ROM: `International Superstar Soccer 2000 (USA) (En,Es) (Rev 1).z64` (symlinked as `baserom.z64`)
SHA1 47a993fe1316dfd9e208603370d3dddae05f1d1b · CIC-6102 · libultra 2.0I · code NISE

## Main segment
- ROM 0x1000–0xA1200 → VRAM 0x80000400–0x800A0600 (.text ends ~0x800691D0, then data)
- BSS 0x800A0600 size 0x10F780 (→ 0x801AFD80); stack top 0x800CEC50
- Entry 0x80000400 clears BSS, jumps to 0x800007B8
- Gfx ucode: F3DEX 2.06 + F3DLX.Rej 2.06 (strings @ ROM 0x9D618 / 0x9DA38)
- Uses 64-bit GPR ops (libgcc __divdi3 etc. near end of text)

## Key functions
- func_80003964(romAddr, ramDst, size)  — game ROM read wrapper (all overlay loads go through it)
- func_8005C5A0(addr, size)             — bzero (used right after a load to clear overlay BSS)
- func_80058780                          — osPiWriteIo (used in IS-Viewer/KMC devkit probe @ 0x80003500)
- func_80057578 / func_80055A2C / ...   — mode-specific overlay loaders

## Overlays (from tools/ovl_scan.py → analysis/overlays.tsv: rom, vram, size, bss)
~39 loads, ~30 contain code. Shared VRAM slots:
- 0x801B00B0: 0x25D3D0, 0x2CB900, 0x2D58A0, 0x2E0D90 (largest, 0x35930)
- 0x801ED9F0: 0x29A360, 0x29F940, 0x2B2D80, 0x2C6860 (+ data 0x2BE640)
- 0x803EB000: 0x2D8500, 0x2D9450
- 0x80271F40: 0x4E23D0, 0x4F0210
- 0x8041xxxx–0x8058xxxx: Expansion-Pak-region overlays (0x519050, 0x53AAB0, 0x5F7410, 0x604CD0 ...)

## Status (2026-10-16)
- Boots on RT64/Vulkan; intro + audio confirmed working by user.
- 81 overlay loads found (tools/ovl_closure.py, overlays load overlays); 65 code sections, 12,215 functions.
- Symbol file: tools/gen_syms.py (linear sweep + jump tables + per-section jal seeds) + recomp/libultra_names.txt.
- Recomp: recomp/iss2000.us.rev1.toml (N64Recomp), recomp/aspMain.us.rev1.toml (RSPRecomp, stock 16-cmd aspMain).
- Host: src/ (SDL window/audio/input, RT64 context adapted from Zelda64Recomp, overlay tracking hook on
  func_80003894). Build: cmake -S . -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++.
- Run: build/ISS2000Recompiled baserom.z64 (first run stores ROM), ISS_DEBUG=1 logs overlay loads.
- Graphics defaults set in src/main.cpp (runtime zero-inits to 320x240/4:3/no AA): window-scaled res,
  widescreen expand, MSAA 4x. Env overrides ISS_RES / ISS_ASPECT / ISS_MSAA / ISS_FULLSCREEN; F11 toggles fullscreen.
- Jump tables in shared-vram overlays must be read from the overlay being swept (fixed 2026-10-16;
  caused "Switch-case out of bounds" crash on New Game).
- GitHub git transport is very slow here: use tools/fetch_submodules.sh (codeload tarballs).

- Widescreen culling fix: hooks after func_80022E58 / in func_800228D0 scale horizontal frustum extents
  0x800A0984 (tan) and 0x800AA104 (far half-width) by window aspect / (4:3) * 1.05 (src/iss_widescreen.cpp).

- Controller Pak: game's own libultra Pfs code is recompiled (pfs_* names in libultra_names.txt); only the
  block layer (__osContRamRead/Write, __osPfsGetStatus, osPfsIsPlug) is emulated in src/iss_controller_pak.cpp,
  backed by raw 32 KB iss2000recomp_data/controllerpak_pN.mpk (Project64-compatible). ISS_PAKS=N (default 1).
  (Briefly reverted and restored on 2026-10-16: the fast/4:3 match was the in-game Resolution=High option.)

## Known gaps
- In-game Resolution=High (640x480, framebuffers moved to 0x805xxxxx): user later confirmed both Low and High
  run at correct speed with widescreen; the earlier "too fast" report did not reproduce (both ~30 fps in matches).
- Sky backdrop: func_80035640 (called once from func_80000D90 @0x80000F60) draws the stadium sky as 4:3 2D
  texrect strips in both modes. Fixed by hooks around that call emitting RT64 extended GBI (gEXEnable +
  SetRectAspect STRETCH / AUTO) into the main DL (write ptr 0x800A09B0). Confirmed by user in Low and High.
  Hi-res flag: byte 0x8006C5F1 -> game state (*0x800A0914)+0x12, applied in func_80001E04.

## Debug tooling
- ISS_DEBUG=1: overlay load/unload + display-list count log.
- ISS_INPUT_FILE=path: controller 1 reads held buttons from a text file ("A START UP", "STICK x y").
- ISS_FORCE_RES=0|1: override the in-game Resolution option.
- ISS_DL_DUMP=<file>: touch <file> to dump the next frame's 2D commands (fills/texrects/TIMG addrs) to <file>.txt.
- ISS_SPRITE_LOG=<file>: touch <file> to log 2D sprite objects passed to func_800414C0.
- Trace build also supports ISS_GFXTRACE=<file>: logs "function DL-write-pointer" per call to find who
  emits a given display-list command (how the sky drawer was found).
- Trace build: N64Recomp recomp/iss2000.trace.toml -> recomp/RecompiledFuncsTrace, then
  cmake -B build-trace -DISS_TRACE=ON; ISS_TRACE_OUT=file dumps per-function call counts every 5 s.
- State-11 handler call to 0x801EFED0 NOPed (unknown overlay).
- Unattributed 2.8 KB code at ROM 0x32E590.
- No rumble, fixed keyboard/pad mapping, no config UI.

## Open questions
- Dynamic loads (args unresolved) at 0x80027FEC, 0x80028498, 0x800531E0, 0x80055728/70, 0x80008440, 0x8004AED8
- ROM gaps not yet attributed: 0xA1200–0x25D3D0, 0x2A52D0–0x2B2D80, 0x2C8F20–0x2CB900, 0x31B780–0x32A680,
  0x32E590–0x379BE0, 0x3AFF70–0x4A6D00, 0x606A00+ (assets / audio / compressed? entropy ~7.3 from 0x640000)
- What lives at 0x801B0000–0x801B00B0
- Audio microcode not identified yet

## Launcher & AppImage (2026-10-16)
- Name: "Mabecs International Superstar Soccer 2000 - Widescreen build" (ISS_APP_NAME in src/launcher.h).
- src/launcher.cpp: SDL 2D-renderer window before recomp::start; darkened logo (assets/launcher_logo.png, embedded
  with DejaVuSans-Bold via tools/embed.py -> src/generated/launcher_assets.h), Select ROM (nfd via
  xdg-desktop-portal, NFD_PORTAL=ON so no GTK), drag & drop ROM, Play, Fullscreen/Widescreen (launcher.ini), Quit.
  `--no-launcher` skips it; a ROM path argument is validated/stored as before.
- Data dir is now $ISS_DATA_DIR / $XDG_DATA_HOME/iss2000recomp / ~/.local/share/iss2000recomp (was ./iss2000recomp_data).
- Portable build (default): compiled inside a rootless Debian 12 root (ext/debian12/root, linuxcontainers
  rootfs, run via tools/debian12.sh = bwrap) with clang-16:
      tools/debian12.sh cmake -S . -B build-deb12 -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_C_COMPILER=clang-16 -DCMAKE_CXX_COMPILER=clang++-16
      tools/debian12.sh cmake --build build-deb12
      tools/make_appimage.sh            # (tools/make_appimage.sh host = package the CachyOS build instead)
  Requires glibc >= 2.34 / GLIBCXX 3.4.30 -> Debian 12+, Ubuntu 22.04+, Fedora 35+. Bundles Debian's SDL2 +
  libdecor, libsamplerate, libXss; libstdc++ deliberately NOT bundled (old copy breaks newer GPU drivers).
  Verified: runs on CachyOS; launcher and game boot inside the Debian 12 root (lavapipe).
