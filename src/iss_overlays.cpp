// Overlay tracking for ISS 2000.
//
// Every ROM read in the game goes through func_80003894(handle, dir, ram, rom, size); N64Recomp
// injects a call to iss_on_rom_read() at its entry (see recomp/iss2000.us.rev1.toml; other versions likewise). The runtime
// doesn't track overlays on its own, so this keeps its function map in sync with what the game
// has loaded:
//   * a read starting exactly at a code section's ROM address loads that section at the target RAM
//     (the game's chunked loader, func_8005568C, issues the first chunk at the section start, so
//     size is deliberately ignored);
//   * any other read that lands on the text of a loaded section unloads it (code was overwritten),
//     unless it is the continuation of that same section's chunked load.
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <atomic>

#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "iss_version.h"

// True while the match engine overlay (US ROM 0x32F090, loaded at 0x803AC000) is resident, i.e. during a match.
std::atomic<bool> iss_in_match{ false };

namespace {

    bool debug_enabled() {
        static const bool enabled = std::getenv("ISS_DEBUG") != nullptr;
        return enabled;
    }

    struct LoadedSection {
        const IssCodeSection* section;
        int32_t ram;
    };

    std::vector<LoadedSection> loaded;

    const IssCodeSection* find_section_by_rom(uint32_t rom) {
        const IssVersion& v = iss_version();
        for (size_t i = 0; i < v.num_sections; i++) {
            if (v.sections[i].rom == rom) {
                return &v.sections[i];
            }
        }
        return nullptr;
    }

    bool overlaps(int32_t a_start, uint32_t a_size, int32_t b_start, uint32_t b_size) {
        return a_start < (int32_t)(b_start + b_size) && b_start < (int32_t)(a_start + a_size);
    }

    void unload_overlapping(int32_t ram, uint32_t size, uint32_t rom) {
        for (auto it = loaded.begin(); it != loaded.end();) {
            const IssCodeSection* s = it->section;
            bool continuation = rom > s->rom && rom < s->rom + s->text_size && (ram - it->ram) == (int32_t)(rom - s->rom);
            if (!continuation && overlaps(ram, size, it->ram, s->text_size)) {
                if (debug_enabled()) {
                    fprintf(stderr, "[ovl] unload %s @ %08X (overwritten by rom %06X)\n", s->name, (uint32_t)it->ram, rom);
                }
                unload_overlays(it->ram, s->text_size);
                if (s->rom == iss_version().match_engine_rom) iss_in_match = false;
                it = loaded.erase(it);
                continue;
            }
            ++it;
        }
    }
}

extern "C" void iss_on_rom_read(uint8_t* rdram, recomp_context* ctx) {
    uint32_t direction = (uint32_t)ctx->r5;
    int32_t ram = (int32_t)ctx->r6;
    uint32_t rom = (uint32_t)ctx->r7;
    uint32_t size = MEM_W(0x10, ctx->r29);

    if (direction != 0) {
        return;
    }

    const IssCodeSection* s = find_section_by_rom(rom);
    if (s != nullptr) {
        unload_overlapping(ram, s->text_size, rom);
        load_overlays(s->rom, ram, s->text_size);
        if (debug_enabled()) {
            fprintf(stderr, "[ovl] load %s @ %08X\n", s->name, (uint32_t)ram);
        }
        loaded.push_back({ s, ram });
        if (s->rom == iss_version().match_engine_rom) iss_in_match = true;
    }
    else {
        unload_overlapping(ram, size, rom);
    }
}
