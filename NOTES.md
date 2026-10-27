# ISS 2000 static recomp (USA Rev 1 + Europe En,De / Fr,It) — research notes

European versions: see "European versions" below. The sections before it describe the US version.

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

## Game fixes (hooks in recomp/iss2000.us.rev1.toml, code in src/iss_widescreen.cpp)
- Widescreen culling: after func_80022E58 / inside func_800228D0, scale horizontal frustum extents
  0x800A0984 (tan) and 0x800AA104 (far half-width) by window aspect / (4:3) * 1.05.
- Sky backdrop: func_80035640 (called once from func_80000D90 @0x80000F60) draws the stadium sky as 4:3 2D
  texrect strips. Hooks around that call emit RT64 extended GBI (gEXEnable + SetRectAspect STRETCH / AUTO) into
  the main DL (write ptr 0x800A09B0).
- Resolution option: High (640x480 interlaced) is redirected to Low at the entry of func_80001D1C (the only
  setter: Options menu + save load). Forcing Low later (in func_80001E04) caused a black match screen; switching
  to real High mid-session caused 4:3 + too fast. ISS_ALLOW_HIRES=1 restores real High.
  Hi-res flag: byte 0x8006C5F1 -> game state (*0x800A0914)+0x12, applied in func_80001E04 via a 25-frame
  countdown at 0x8006C5F0 run by func_80000B28.

- Player LOD (2026-10-27): func_8002E504(player, level) [ende 8002EB64, frit 8002EB84] sets a player's 19 body-part
  display lists for detail level 0 (best) .. 6 (levels 3-6 drop parts; head from per-player table 0x800B9590).
  func_8002FA70 picks the level each frame via func_8002E3F0(depth at obj+0x20): linear between near 100 / far 750
  (0x8007DC18/1C), from a minimum 0x8007DC14 (set by func_80030E58: 2 in matches, 0 in close-ups) up to 6.
  Hook at func_8002E504 entry forces level 0 (iss_player_lod) when the launcher's "Detail: Full" option is on
  (launcher.ini full_detail, default 1; "Detail: N64" = original). Debug override: ISS_PLAYER_LOD=<0-6> or =game.
  Level-0 data is resident in matches (close-ups use it); verified in the attract demo, no missing parts.

## Host fixes (src/main.cpp)
- Audio: get_frames_remaining reports 2 VIs less than queued (ISS_AUDIO_CUSHION overrides). Reporting the exact
  level let the output run dry 7-8x/s = crackles. Game sample rate is 44100.
- Controller: ensure_controller() polls each frame; the launcher consumed SDL_CONTROLLERDEVICEADDED.
- Esc quits, F11 toggles fullscreen.

## Frame pacing (investigated, not a bug in the end)
- Game uses the libultra sample scheduler: retrace message every VI to the main loop (queue 0x800A7238, type 1 ->
  func_80000B28), which builds a frame when one of its two DL buffers (busy flags 0x800D2C00) is free;
  func_8001DA28 frees it on DP done and calls osViSwapBuffer. Matches/menus run 30 fps, intro bursts 60 fps.
- "Too fast" reports were the High-mode switch, not the display/fullscreen. ISS_MAX_FPS cap (matches only, via
  overlay 0x32F090 residency) is useless: that overlay stays loaded in menus. Left env-gated, off by default.
- Local-only edit in ext/N64ModernRuntime/ultramodern/src/events.cpp: ISS_EARLY_DP=1 sends dp_complete before
  rendering (experiment, off by default). Saved as patches/N64ModernRuntime-early-dp.patch (git apply in ext/N64ModernRuntime).

## Known gaps
- State-11 handler call to 0x801EFED0 NOPed (unknown overlay).
- Unattributed 2.8 KB code at ROM 0x32E590.
- No rumble, fixed keyboard/pad mapping, no config UI.
- Upscaled 2D texture packs: RT64 supports replacement packs; dump/load hooks not wired up yet.

## Debug tooling (all env-gated)
- ISS_DEBUG=1: overlay load/unload + display-list count log.   ISS_FPS=1: fps + audio buffer every 2 s.
- ISS_TIMING=1: per-frame DL arrival / RT64 time vs VI (25-28 s window).   ISS_AUDIO_DEBUG=1: audio queue stats.
- ISS_INPUT_FILE=path: controller 1 reads held buttons from a text file ("A START UP", "STICK x y").
  Menus need ~0.3 s holds (drive/hold.sh pattern); the save remembers the last menu position.
- ISS_FORCE_RES=0|1: force mode at func_80001E04.   ISS_SWITCH_RES_AT=<s>:<v>: call func_80001D1C(v) at <s>.
- ISS_MEMDUMP_AT=<s>:<file>[:<count>:<interval>]: dump all 8 MB RDRAM (several dumps -> <file>.0, .1, ...).
  Object draw lists: heads at 0x800CED88[i], node = {obj, next}; obj+0x24 DL, +0x28 flags.   ISS_DL_DELAY_MS / ISS_BUFFERING / ISS_PRESENT: RT64 tests.
- ISS_DL_DUMP=<file>: touch <file> to dump the next frame's 2D commands (fills/texrects/TIMG addrs) to <file>.txt.
- ISS_SPRITE_LOG=<file>: touch <file> to log 2D sprite objects passed to func_800414C0.
- Trace build: N64Recomp recomp/iss2000.trace.toml -> recomp/RecompiledFuncsTrace, then
  cmake -B build-trace -DISS_TRACE=ON; ISS_TRACE_OUT=file dumps per-function call counts every 5 s;
  ISS_GFXTRACE=<file> logs "function DL-write-pointer" per call (how the sky drawer was found).
- Regenerate the trace toml from the main one after adding hooks (sed output_func_path + trace_mode = true).

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

## Releases & publishing
- GitHub: github.com/Mabec-games/iss2000-recomp (public; gh CLI logged in as Mabec-games). v1.0 (2026-10-17),
  v1.1 (audio, controller, resolution, Esc). Repo excludes ROM, recomp/RecompiledFuncs*, recomp/rsp, analysis/, ext/.
  Launcher assets header is generated at build time (tools/embed.py via CMake).
- Release steps: build-deb12 (tools/debian12.sh cmake --build build-deb12) -> tools/make_appimage.sh ->
  git commit/push -> gh release create vX dist/Mabecs_ISS2000_Widescreen-x86_64.AppImage --notes-file ...
- make_appimage.sh writes to a temp name and renames, so it works while the old AppImage is running.

## European versions (2026-10-18)
One executable supports USA Rev 1 (`us`), Europe En,De (`ende`, game code NISX, .n64 byte order) and Europe Fr,It
(`frit`, NISY). The PAL builds are separate compiles: every address moved (BSS 0x8009DD20 / 0x8009DE00, entry
jumps to 0x80000814) and the two PAL ROMs also differ by 0x20+ from each other. ~95% of US main functions match
PAL with relocations masked; En,De vs Fr,It differ in only 4 main functions. aspMain is identical (shared).
- Pipeline per version (US commands unchanged): tools/versions.py holds the per-version constants (ROM read /
  bzero / chunk / file loaders, text end, BSS), picked by the ROM header game code, so the tools take any ROM:
      .venv/bin/python -I tools/to_z64.py "...(En,De).n64" baserom.eu.ende.z64     # Fr,It: symlink baserom.eu.frit.z64
      .venv/bin/python -I tools/ovl_closure.py baserom.eu.ende.z64 analysis/ende/overlays.tsv   # start from empty tsv
      .venv/bin/python -I tools/gen_syms.py baserom.eu.ende.z64 analysis/ende/overlays.tsv \
          recomp/iss2000.eu.ende.syms.toml recomp/libultra_names.eu.ende.txt
      ext/N64Recomp/build/N64Recomp recomp/iss2000.eu.ende.toml        # -> recomp/RecompiledFuncs_ende
      .venv/bin/python -I tools/gen_sections_header.py recomp/iss2000.eu.ende.syms.toml src/generated/iss_sections_ende.h ende
  Re-run cmake configure after regenerating a RecompiledFuncs_* dir (sources are globbed).
- libultra names were mapped by function matching (normalised instruction compare, ambiguous hits resolved by the
  neighbours' offset). PAL also calls osViSetXScale (En,De 0x8005F080), which US doesn't: unnamed, it ran as game
  code on VI state the runtime never sets up and crashed on boot.
- gen_syms.py fix: register constants are dropped on any other write (a stale `lui v0` overrode the jump-table
  base in `addu at, at, v0`, splitting func_8021347C_ovl_523120 in En,De). US output unchanged.
- One binary: each version's generated code is a static lib whose defined globals get a prefix after archiving
  (tools/prefix_syms.py via nm/objcopy: us_func_80000400, ende_recomp_entrypoint, ...). src/register_overlays.cpp
  is compiled into each lib (-> <id>_iss_register_overlays). src/iss_version.cpp has the per-version table (ROM
  hash, game id, sections, pak suffix, hook RAM addresses); hooks read iss_version(). All three games are
  registered with librecomp; only the chosen version's overlays are registered, before recomp::start.
- Hook addresses per version: see the [[patches.hook]] comments in recomp/iss2000.eu.*.toml (US name in brackets).
  Hooked function equivalents: frame handler US func_80000B28 = PAL func_80002A10 (grew by a display-list
  overflow check, no speed compensation), sky caller US func_80000D90 = PAL func_80000C30.
- PAL-only hooks: recomp_entrypoint sets osTvType = PAL (librecomp writes NTSC before the entrypoint);
  func_8000164C (game patches libultra's PAL LAN1 mode for a PAL TV: vStart 0x0043024C, field origin 0x519,
  osViSetXScale 0.96 / osViSetYScale 0.88) is replaced by writing NTSC LAN1 timings into that mode. With the PAL
  window RT64 drew the picture squashed, and the odd origin didn't match its framebuffer -> garbled raw-RDRAM output.
- 50 Hz: patches/N64ModernRuntime-vi-rate.patch (REQUIRED to build; git apply in ext/N64ModernRuntime) adds ultramodern::set_vi_rate/get_vi_rate (VI thread and audio
  use it); the host sets 50 for PAL. Measured: PAL match clock runs in real time at 50 Hz, and at 60 Hz the game's
  audio code (882 samples per frame) overfills the queue and computes a negative buffer length -> crash. So PAL
  can't run at 60 Hz; queue_samples now drops such bogus lengths. Debug: ISS_VI_RATE=<hz>, ISS_TV_NTSC=1.
- Controller Pak files per version: controllerpak_pN.mpk (US, unchanged), controllerpak_pN_ende.mpk, _frit.mpk.
- Launcher: Detail Full/N64 button (player LOD, v1.3); buttons are sized to their labels.
- Launcher: a ROM is checked against every version (iss::select_rom_any_version); a version button cycles between
  stored versions; the choice is saved as `version <id>` in launcher.ini. `--no-launcher` uses that (or the first
  stored one); a ROM path argument selects its version.
- Verified headless for all three: boot, intro, title, Controller Pak note creation/saving, menus (French text on
  Fr,It), language option, a match in widescreen at 25 fps, audio steady (0-1 underruns per few minutes).

## Headless testing (no windows on the desktop)
`gamescope --backend headless -W 1280 -H 720 -- <cmd>` runs the game off-screen (NVIDIA works);
`gamescopectl screenshot <file>` inside the session captures frames. Use ISS_DATA_DIR=<scratch dir> so test runs
don't touch the real ROM store / paks, and ISS_INPUT_FILE to press buttons. A driver script (start game, timed
`hold <buttons>` / `shot <name>` steps) worked well; title screen START is around 40 s after boot.
gdb: ptrace_scope=1 blocks attaching, so run the game under gdb inside gamescope (pkill -INT to break in).

## Windows build (2026-10-18)
- Toolchain: VS 2022 Build Tools 14.44 with its bundled clang-cl 19.1.5, CMake and LLVM tools; Ninja; Python 3.12.
  `tools\build_windows.bat` finds VS with vswhere, enters vcvars64, puts VC\Tools\Llvm\x64\bin on PATH (RT64's
  shader preprocessing calls `clang` directly) and builds Release into build-win\. Full build ~25 min on 4 cores.
- SDL2: the VC dev package RT64 ships (ext/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3). Its headers have
  no SDL2/ directory, so CMake copies them to build-win/sdl2-include/SDL2. RT64 links SDL2 by bare name with a
  directory-scoped link_directories(), so the exe adds that lib dir itself.
- prefix_syms.py works unchanged on COFF archives with llvm-nm / llvm-objcopy (CMake leaves CMAKE_NM/OBJCOPY
  empty for clang-cl; found next to the compiler). Same symbol counts as on Linux.
- Exe: WIN32 subsystem + SDL2main (WinMain, UTF-8 argv), NOMINMAX (windows.h min/max broke std::min/max), icon
  from assets/iss2000recomp.png (tools/png_to_ico.py, PNG-in-ICO) via an .rc. Post-build copies SDL2.dll,
  dxcompiler.dll, dxil.dll next to the exe. -march=nehalem is passed as /clang:-march=nehalem.
- Host (src/main.cpp): window handle = HWND + GetCurrentThreadId() (no SDL_WINDOW_VULKAN; RT64 picks D3D12 or
  Vulkan), data dir %APPDATA%\iss2000recomp (ISS_DATA_DIR still wins), paths passed as UTF-8, per-monitor-v2 DPI
  awareness, stderr goes to the parent console when started from a terminal and not redirected. nfd uses the
  native Windows dialog. Launcher code unchanged.
- Verified: USA boot/intro/title at the usual 60/30 fps with steady audio; Europe Fr,It at 50 Hz settling at 25 fps.
  Launcher confirmed by the user on Windows (from the release zip): ROM selection and an EU ROM loading and playing.
- Package: tools\make_windows_zip.ps1 -> dist\Mabecs_ISS2000_Widescreen-Windows-x64.zip (exe, 3 DLLs, licenses).

## Possible next steps
- Rumble Pak option, controller remapping, texture-pack dump/load (ISS_DEV / ISS_TEXTURE_PACK idea).

## Online 1 vs 1 (v1.4, 2026-10-27)
- src/iss_netplay.cpp: Dolphin traversal (stun.dolphin-emu.org:6262) + LAN broadcast (UDP 9099), delayed-input
  lockstep with 12-frame redundancy, VI lead limit. Details and history in online.md.
- patches/N64ModernRuntime-netplay.patch (REQUIRED, after vi-rate): relative VI pacing while a vi_callback is set,
  run-state reset in recomp::start (launcher -> game -> launcher).
- Determinism: game osGetCount calls are renamed to iss_game_osGetCount_recomp (objcopy in CMakeLists.txt); online
  it returns token + frame*1562500 + per-thread call*64 (RNG seeds: US func_80008F20 -> 0x800A7878,
  func_80016448 -> 0x800A7AE8; IssVersion::rng_states). Online: pak port 1 = controllerpak_online_p1*.mpk,
  fixed 21:9 cull, Full LOD, 6-VI audio cushion. Desync check hashes rng_states per input frame (ISS_NET_DEBUG=1).
