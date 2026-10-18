// Debug-only (trace build): per-function call counters, dumped every 5 s to $ISS_TRACE_OUT as
// "<seconds> <name> <calls>" lines, then reset. Used to compare game behaviour between modes.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <atomic>
#include <filesystem>

#include "trace.h"
#include "iss_version.h"

namespace {
    std::mutex trace_mutex;
    std::deque<std::pair<const char*, uint32_t>> counters;  // deque: stable element addresses

    void dump_loop(std::string path) {
        auto start = std::chrono::steady_clock::now();
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::lock_guard lock{ trace_mutex };
            FILE* f = fopen(path.c_str(), "a");
            if (f == nullptr) continue;
            for (auto& c : counters) {
                if (c.second != 0) {
                    fprintf(f, "%.0f %s %u\n", t, c.first, c.second);
                    c.second = 0;
                }
            }
            fclose(f);
        }
    }
}

extern "C" uint32_t* iss_trace_register(const char* name) {
    std::lock_guard lock{ trace_mutex };
    static bool started = false;
    if (!started) {
        started = true;
        const char* out = std::getenv("ISS_TRACE_OUT");
        std::thread(dump_loop, std::string(out ? out : "trace.txt")).detach();
    }
    counters.emplace_back(name, 0);
    return &counters.back().second;
}

// While the file named by ISS_GFXTRACE exists, log "<function> <display list write pointer>" for every
// function entry (main game display list pointer at 0x800A09B0), up to 300k lines, then delete the trigger.
extern "C" void iss_trace_gfx(const char* name, const uint8_t* rdram) {
    static const char* trigger = std::getenv("ISS_GFXTRACE");
    static std::atomic<bool> active{ false };
    static FILE* out = nullptr;
    static int lines = 0;
    static int check = 0;
    if (trigger == nullptr) return;
    if (!active) {
        if (++check % 4096 != 0) return;
        if (!std::filesystem::exists(trigger)) return;
        out = fopen((std::string(trigger) + ".txt").c_str(), "w");
        lines = 0;
        active = true;
    }
    uint32_t addr = iss_version().gfx_pointer & 0x7FFFFF;
    uint32_t ptr = (uint32_t)rdram[addr ^ 3] << 24 | (uint32_t)rdram[(addr + 1) ^ 3] << 16 | (uint32_t)rdram[(addr + 2) ^ 3] << 8 | rdram[(addr + 3) ^ 3];
    fprintf(out, "%s %08X\n", name, ptr);
    if (++lines >= 300000) {
        fclose(out);
        out = nullptr;
        active = false;
        std::filesystem::remove(trigger);
    }
}
