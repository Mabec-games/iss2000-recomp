// Widescreen culling fix.
//
// The game hides objects (players, ball, ...) outside its own 4:3 view frustum before they reach the
// renderer, so with RT64's widescreen expansion they pop in and out at the screen edges. The frustum
// test (func_80022FD4 and friends) compares view-space x against two horizontal extents computed per
// camera update:
//   0x800A0984  tan of the horizontal half-FOV        (set by func_80022E58)
//   0x800AA104  horizontal half-width at the far plane (set by func_80022E58 and func_800228D0)
// The hooks below scale just those two by the window's aspect relative to 4:3. The projection and
// the vertical extents (derived from registers, not from these globals) are left untouched.
// Addresses and function names in this file are the US ones; the other versions' are in src/iss_version.cpp.
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "recomp.h"
#include "iss_hooks.h"
#include "iss_version.h"

namespace {
    gpr horizontal_tan_addr() { return (gpr)(int32_t)iss_version().horizontal_tan; }
    gpr far_half_width_addr() { return (gpr)(int32_t)iss_version().far_half_width; }
    // Small margin so objects whose bounding sphere just grazes the edge don't flicker.
    constexpr float margin = 1.05f;

    void scale_float(uint8_t* rdram, gpr addr, float factor) {
        uint32_t bits = MEM_W(0, addr);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        value *= factor;
        std::memcpy(&bits, &value, sizeof(value));
        MEM_W(0, addr) = bits;
    }
}

extern "C" void iss_widen_cull_frustum(uint8_t* rdram, recomp_context* ctx) {
    float factor = iss_cull_widen_factor();
    if (factor > 1.0f) {
        scale_float(rdram, horizontal_tan_addr(), factor * margin);
        scale_float(rdram, far_half_width_addr(), factor * margin);
    }
}

extern "C" void iss_widen_cull_far_plane(uint8_t* rdram, recomp_context* ctx) {
    float factor = iss_cull_widen_factor();
    if (factor > 1.0f) {
        scale_float(rdram, far_half_width_addr(), factor * margin);
    }
}

// In-game Resolution option. The game's High mode (640x480 interlaced, Expansion Pak) only exists to raise the
// N64's output resolution; RT64 already renders at the window's resolution, so it adds nothing here, and switching
// to it mid-session leaves the game too fast / in 4:3. Every request to change the option goes through
// func_80001D1C(value) (Options menu and save loading), so the requested value is turned into Low there.
//   ISS_ALLOW_HIRES=1   let the game use High mode as chosen (debugging)
extern "C" void iss_resolution_option(uint8_t* rdram, recomp_context* ctx) {
    static const bool allow_hires = std::getenv("ISS_ALLOW_HIRES") != nullptr;
    if (!allow_hires) {
        ctx->r4 = 0;
    }
}

// Debug: ISS_FORCE_RES=0|1 forces the mode right before func_80001E04 applies the option byte (0x8006C5F1).
extern "C" void iss_debug_force_res(uint8_t* rdram, recomp_context* ctx) {
    static const char* force = std::getenv("ISS_FORCE_RES");
    if (force != nullptr) {
        MEM_B(0, (gpr)(int32_t)iss_version().hires_option) = (uint8_t)std::atoi(force);
    }
}

// Debug: while the file named by ISS_SPRITE_LOG exists, append each 2D sprite object passed to the image
// drawer (func_800414C0) to <file>.txt; the trigger is deleted after 200 entries (a few frames).
extern "C" void iss_debug_sprite(uint8_t* rdram, recomp_context* ctx) {
    static const char* trigger = std::getenv("ISS_SPRITE_LOG");
    static int logged = 0;
    if (trigger == nullptr || !std::filesystem::exists(trigger)) {
        logged = 0;
        return;
    }
    FILE* f = fopen((std::string(trigger) + ".txt").c_str(), "a");
    if (f != nullptr) {
        gpr obj = ctx->r4;
        fprintf(f, "%08X:", (uint32_t)obj);
        for (int i = 0; i < 0x40; i += 4) {
            fprintf(f, " %08X", MEM_W(i, obj));
        }
        uint32_t desc = MEM_W(0x30, obj);
        if (desc >= 0x80000000u && desc < 0x80800000u) {
            fprintf(f, " | desc:");
            for (int i = 0; i < 0x20; i += 4) {
                fprintf(f, " %08X", MEM_W(i, (gpr)(int32_t)desc));
            }
        }
        fprintf(f, "\n");
        fclose(f);
    }
    if (++logged >= 200) {
        std::filesystem::remove(trigger);
    }
}

// Sky backdrop stretch.
//
// func_80035640 draws the stadium sky as a strip of 2D texture rectangles laid out for the 4:3 screen, so
// with widescreen expansion it leaves black bars at the sides. Around its single call (in func_80000D90)
// we append RT64 extended-GBI commands to the game's display list (write pointer at 0x800A09B0):
// enable the extended GBI, set rect aspect to STRETCH, and afterwards back to AUTO. RT64 then maps
// those rectangles across the full window width; everything else keeps its normal placement.
namespace {
    constexpr uint32_t rt64_hook_opcode = 0xE0;          // G_SPNOOP on F3DEX2
    constexpr uint32_t rt64_hook_magic = 0x525464;       // "RT" 0x64
    constexpr uint32_t rt64_hook_op_enable = 0x1;
    constexpr uint32_t rt64_extended_opcode = 0x64;
    constexpr uint32_t g_ex_setrectaspect_v1 = 0x000033;
    constexpr uint32_t g_ex_aspect_auto = 0x0;
    constexpr uint32_t g_ex_aspect_stretch = 0x1;

    void emit(uint8_t* rdram, uint32_t w0, uint32_t w1) {
        gpr gfx_pointer_addr = (gpr)(int32_t)iss_version().gfx_pointer;
        gpr gfx = (gpr)(int32_t)MEM_W(0, gfx_pointer_addr);
        MEM_W(0, gfx) = w0;
        MEM_W(4, gfx) = w1;
        MEM_W(0, gfx_pointer_addr) = (uint32_t)(gfx + 8);
    }

    void emit_rect_aspect(uint8_t* rdram, uint32_t aspect) {
        emit(rdram, (rt64_hook_opcode << 24) | rt64_hook_magic, (rt64_hook_op_enable << 28) | rt64_extended_opcode);
        emit(rdram, (rt64_extended_opcode << 24) | g_ex_setrectaspect_v1, aspect);
    }
}

extern "C" void iss_sky_stretch_begin(uint8_t* rdram, recomp_context* ctx) {
    if (iss_cull_widen_factor() > 1.0f) {
        emit_rect_aspect(rdram, g_ex_aspect_stretch);
    }
}

extern "C" void iss_sky_stretch_end(uint8_t* rdram, recomp_context* ctx) {
    if (iss_cull_widen_factor() > 1.0f) {
        emit_rect_aspect(rdram, g_ex_aspect_auto);
    }
}

// Debug: ISS_SWITCH_RES_AT=<seconds>:<value> changes the Resolution option once, <seconds> after the first frame,
// by calling the game's own Options handler func_80001D1C(value) (registers are saved and restored around it).
extern "C" void iss_debug_memdump(uint8_t* rdram);
extern "C" void iss_debug_switch_res(uint8_t* rdram, recomp_context* ctx) {
    iss_debug_memdump(rdram);
    static const char* spec = std::getenv("ISS_SWITCH_RES_AT");
    static bool done = false;
    static auto start = std::chrono::steady_clock::now();
    if (spec == nullptr || done) return;
    float at = 0; int value = 1;
    if (sscanf(spec, "%f:%d", &at, &value) < 1) return;
    if (std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count() < at) return;
    done = true;
    fprintf(stderr, "[debug] calling the Options resolution handler (%d)\n", value);
    recomp_context saved = *ctx;
    ctx->r4 = value;
    iss_version().set_resolution(rdram, ctx);
    *ctx = saved;
}

// Debug: ISS_MEMDUMP_AT=<seconds>:<file> writes main RAM 0x80000000-0x801B0000 (as stored) to <file> once.
extern "C" void iss_debug_memdump(uint8_t* rdram) {
    static const char* spec = std::getenv("ISS_MEMDUMP_AT");
    static bool done = false;
    static auto start = std::chrono::steady_clock::now();
    if (spec == nullptr || done) return;
    float at = 0; char path[512] = {};
    if (sscanf(spec, "%f:%511s", &at, path) < 2) return;
    if (std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count() < at) return;
    done = true;
    if (FILE* f = fopen(path, "wb")) { fwrite(rdram, 1, 0x1B0000, f); fclose(f); fprintf(stderr, "[debug] memory dumped\n"); }
}
