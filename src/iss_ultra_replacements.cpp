// Replacements for libultra-level functions that talk to hardware directly and aren't provided by
// N64ModernRuntime. Controller Pak block I/O lives in iss_controller_pak.cpp.
#include <span>

#include "recomp.h"
#include "librecomp/game.hpp"
#include "iss_netplay.h"

extern "C" uint32_t osGetCount();

extern "C" void __osSiDeviceBusy_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 0;
}

// osPiReadIo(devAddr, *data). Only the game's IS-Viewer debug printer calls this (dev board at
// 0x1FF00000+, never present on retail). Serve real cartridge words and read anything else as 0.
extern "C" void osPiReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint32_t phys = (uint32_t)ctx->r4 & 0x1FFFFFFF;
    gpr data_ptr = ctx->r5;
    std::span<const uint8_t> rom = recomp::get_rom();
    uint32_t offset = phys - 0x10000000;
    if (phys >= 0x10000000 && offset + 4 <= rom.size()) {
        recomp::do_rom_pio(rdram, data_ptr, phys);
    }
    else {
        MEM_W(0, data_ptr) = 0;
    }
    ctx->r2 = 0;
}

// osPiWriteIo(devAddr, data): only used for IS-Viewer output; dropped.
extern "C" void osPiWriteIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 0;
}

// Game code's osGetCount (renamed from osGetCount_recomp in the recompiled libraries, see CMakeLists.txt).
// The game seeds its RNGs from it (func_80008F20, func_80016448 and in the match overlay), so online
// play substitutes a clock both peers agree on.
extern "C" void iss_game_osGetCount_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint32_t count;
    if (!iss::net::get_game_count(&count)) {
        count = osGetCount();
    }
    ctx->r2 = count;
}
