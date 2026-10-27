// Supported ROM versions. Each version's recompiled code is linked in as its own library whose symbols carry
// the version id as prefix (us_func_80000400, ende_recomp_entrypoint, ...; see CMakeLists.txt), so all of
// them live in one executable. The launcher picks the version from the stored ROM; the host hooks read the
// game addresses they need from iss_version().
#ifndef ISS_VERSION_H
#define ISS_VERSION_H

#include <cstddef>
#include <cstdint>

#include "recomp.h"

struct IssCodeSection { uint32_t rom; uint32_t vram; uint32_t text_size; const char* name; };

using IssRecompFunc = void(uint8_t* rdram, recomp_context* ctx);

struct IssVersion {
    const char* id;                 // "us", "ende", "frit": symbol prefix and file name suffix
    const char8_t* game_id;         // librecomp game id (name of the stored ROM)
    const char* mod_game_id;        // runtime --game name
    const char* name;               // shown in the launcher
    const char* short_name;         // launcher Version button
    uint64_t rom_hash;              // XXH3-64 of the big-endian (.z64) ROM
    bool pal;                       // 50 Hz VI, PAL TV type
    IssRecompFunc* entrypoint;
    void (*register_overlays)();
    const IssCodeSection* sections;
    size_t num_sections;
    const char* pak_suffix;         // Controller Pak file name suffix ("" for US: keeps existing saves)

    uint32_t match_engine_rom;      // overlay resident during matches (US ROM 0x32F090)
    uint32_t horizontal_tan;        // frustum culling: tan of the horizontal half-FOV (US 0x800A0984)
    uint32_t far_half_width;        // frustum culling: far-plane half-width (US 0x800AA104)
    uint32_t gfx_pointer;           // main display list write pointer (US 0x800A09B0)
    uint32_t hires_option;          // Resolution option byte, 1 = High (US 0x8006C5F1)
    IssRecompFunc* set_resolution;  // Options handler that sets it (US func_80001D1C), debug only
    uint32_t pal_vi_mode;           // PAL: libultra's PAL LAN1 OSViMode, given NTSC LAN1 timings (0 for US)
    uint32_t rng_states[2];         // game RNG states seeded from osGetCount (US 0x800A7878, 0x800A7AE8); netplay desync check
};

extern const IssVersion iss_versions[];
extern const size_t iss_num_versions;

// The version being played (US until iss_select_version is called).
const IssVersion& iss_version();
void iss_select_version(const IssVersion& version);

#endif
