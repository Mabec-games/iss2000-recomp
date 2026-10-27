#include <cstdlib>

#include "iss_version.h"
#include "generated/iss_sections_us.h"
#include "generated/iss_sections_ende.h"
#include "generated/iss_sections_frit.h"

// Prefixed symbols of the per-version recompiled libraries.
extern "C" {
    IssRecompFunc us_recomp_entrypoint, us_func_80001D1C;
    IssRecompFunc ende_recomp_entrypoint, ende_func_80001D44;
    IssRecompFunc frit_recomp_entrypoint, frit_func_80001D44;
    void us_iss_register_overlays();
    void ende_iss_register_overlays();
    void frit_iss_register_overlays();
}

const IssVersion iss_versions[] = {
    {
        .id = "us", .game_id = u8"iss2000.n64.us.1.1", .mod_game_id = "iss2000", .name = "USA (Rev 1)", .short_name = "USA",
        .rom_hash = 0xA8D9809FB90E36AAULL,  // SHA1 47a993fe1316dfd9e208603370d3dddae05f1d1b
        .pal = false,
        .entrypoint = us_recomp_entrypoint, .register_overlays = us_iss_register_overlays,
        .sections = iss_code_sections_us, .num_sections = sizeof(iss_code_sections_us) / sizeof(IssCodeSection),
        .pak_suffix = "",
        .match_engine_rom = 0x32F090, .horizontal_tan = 0x800A0984, .far_half_width = 0x800AA104,
        .gfx_pointer = 0x800A09B0, .hires_option = 0x8006C5F1, .set_resolution = us_func_80001D1C,
        .pal_vi_mode = 0,
        .rng_states = { 0x800A7878, 0x800A7AE8 },
    },
    {
        .id = "ende", .game_id = u8"iss2000.n64.eu.ende", .mod_game_id = "iss2000-eu-ende", .name = "Europe (English, German)",
        .short_name = "Europe En/De",
        .rom_hash = 0x61AFD6C58EF8F358ULL,  // SHA1 e7078e5f1e899382b1cc145df43ee8cfaa924297 (as .z64)
        .pal = true,
        .entrypoint = ende_recomp_entrypoint, .register_overlays = ende_iss_register_overlays,
        .sections = iss_code_sections_ende, .num_sections = sizeof(iss_code_sections_ende) / sizeof(IssCodeSection),
        .pak_suffix = "_ende",
        .match_engine_rom = 0x32AE00, .horizontal_tan = 0x8009E09C, .far_half_width = 0x800A7824,
        .gfx_pointer = 0x8009E0C8, .hires_option = 0x8006CE71, .set_resolution = ende_func_80001D44,
        .pal_vi_mode = 0x80098DC0,
        .rng_states = { 0x800A4F98, 0x800A5208 },
    },
    {
        .id = "frit", .game_id = u8"iss2000.n64.eu.frit", .mod_game_id = "iss2000-eu-frit", .name = "Europe (French, Italian)",
        .short_name = "Europe Fr/It",
        .rom_hash = 0x60E798A5B85C32E8ULL,  // SHA1 b0505e13ba0f029ea735378c50b224fd618be302
        .pal = true,
        .entrypoint = frit_recomp_entrypoint, .register_overlays = frit_iss_register_overlays,
        .sections = iss_code_sections_frit, .num_sections = sizeof(iss_code_sections_frit) / sizeof(IssCodeSection),
        .pak_suffix = "_frit",
        .match_engine_rom = 0x329FA0, .horizontal_tan = 0x8009E17C, .far_half_width = 0x800A7904,
        .gfx_pointer = 0x8009E1A8, .hires_option = 0x8006CE91, .set_resolution = frit_func_80001D44,
        .pal_vi_mode = 0x80098EA0,
        .rng_states = { 0x800A5078, 0x800A52E8 },
    },
};
const size_t iss_num_versions = sizeof(iss_versions) / sizeof(iss_versions[0]);

namespace {
    const IssVersion* current = &iss_versions[0];
}

const IssVersion& iss_version() {
    return *current;
}

void iss_select_version(const IssVersion& version) {
    current = &version;
}

// PAL versions: hooked at the entrypoint (recomp/iss2000.eu.*.toml). ISS_TV_NTSC=1 keeps NTSC (debugging).
extern "C" void iss_set_tv_type_pal(uint8_t* rdram, recomp_context* ctx) {
    static const bool keep_ntsc = std::getenv("ISS_TV_NTSC") != nullptr;
    if (keep_ntsc) return;
    constexpr gpr os_tv_type = (gpr)(int32_t)0x80000300;
    MEM_W(0, os_tv_type) = 0;  // OS_TV_PAL
}

// PAL picture position. The PAL versions adjust libultra's PAL LAN1 video mode once at boot (Europe En,De:
// func_8000164C) to centre and stretch the 240-line picture on a 288-line PAL TV: earlier vStart, a field origin
// of 0x519 and osViSetXScale(0.96) / osViSetYScale(0.88). RT64 takes that VI window literally, so the picture
// filled only part of the window and, with the odd origin, didn't match the rendered framebuffer (garbled
// output). The hook replaces that function: the mode gets the NTSC LAN1 timings the US version uses (from its
// mode table; the type byte stays PAL), which RT64 shows full-window. Game speed is set by the VI rate (50 Hz)
// independently of these registers.
extern "C" void iss_pal_vi_mode(uint8_t* rdram, recomp_context* ctx) {
    static constexpr uint32_t ntsc_lan1[19] = {
        0x0000311E, 0x00000140, 0x03E52239, 0x0000020D, 0x00000C15, 0x0C150C15, 0x006C02EC, 0x00000200, 0x00000000,
        0x00000280, 0x00000400, 0x002501FF, 0x000E0204, 0x00000002,
        0x00000280, 0x00000400, 0x002501FF, 0x000E0204, 0x00000002,
    };
    gpr mode = (gpr)(int32_t)iss_version().pal_vi_mode;
    for (int i = 0; i < 19; i++) {
        MEM_W(4 + i * 4, mode) = ntsc_lan1[i];  // after the type word
    }
}
