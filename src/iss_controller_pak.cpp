// Controller Pak (memory pak) emulation.
//
// Only the block layer is emulated here; the game's own libultra filesystem code (osPfsInitPak,
// osPfsReadWriteFile, ...) is recompiled unchanged and runs on top of it (see the "pfs_" names in
// recomp/libultra_names.txt). The pak is a raw 32 KB image (same layout as Project64 .mpk files),
// stored next to the other runtime data, one file per controller port and ROM version, so saves of different
// versions never mix:
//     controllerpak_p1.mpk (USA), controllerpak_p1_ende.mpk (Europe En,De), ...
// Port 1 has a pak by default; ISS_PAKS=0 disables it, ISS_PAKS=2 also inserts one in port 2, etc.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include "recomp.h"
#include "iss_version.h"

namespace {
    constexpr int32_t PFS_ERR_NOPACK = 1;
    constexpr size_t pak_size = 0x8000;        // 32 KB, one bank
    constexpr uint32_t block_size = 32;
    constexpr uint32_t num_blocks = pak_size / block_size;

    struct Pak {
        bool loaded = false;
        std::filesystem::path path;
        std::array<uint8_t, pak_size> data{};
    };

    std::mutex pak_mutex;
    std::filesystem::path data_dir;
    std::array<Pak, 4> paks;

    int inserted_pak_count() {
        static const int count = [] {
            const char* v = std::getenv("ISS_PAKS");
            int n = v != nullptr ? std::atoi(v) : 1;
            return n < 0 ? 0 : (n > 4 ? 4 : n);
        }();
        return count;
    }

    void put16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = v & 0xFF; }
    void put32(uint8_t* p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v & 0xFFFF); }

    // Lay out an empty, formatted pak the same way libultra's format routine does:
    //   page 0: label (block 0) + pack ID at blocks 1, 3, 4 and 6
    //   page 1: inode table, page 2: its backup, pages 3-4: directory (16 empty entries)
    void format(std::array<uint8_t, pak_size>& d) {
        d.fill(0);

        uint8_t id[32]{};
        put32(id + 0x00, 0xFFFFFFFF);   // repaired
        put32(id + 0x04, 0x051A5F13);   // random
        // serial_mid / serial_low (0x08-0x17) stay zero
        put16(id + 0x18, 0x0001);       // deviceid: bit 0 set = valid
        id[0x1A] = 1;                   // banks
        id[0x1B] = 0;                   // version
        uint16_t sum = 0;
        for (int i = 0; i < 28; i += 2) {
            sum += (uint16_t)((id[i] << 8) | id[i + 1]);
        }
        put16(id + 0x1C, sum);
        put16(id + 0x1E, (uint16_t)(0xFFF2 - sum));
        for (uint32_t block : { 1u, 3u, 4u, 6u }) {
            std::memcpy(&d[block * block_size], id, sizeof(id));
        }

        // Inode table: pages 0-4 are the system area, 5-127 free (PFS_PAGE_NOT_USED = 3).
        // Entry 0's page byte holds the byte-sum of the entries from the first data page on.
        constexpr int inode_start_page = 5;
        uint8_t* inode = &d[0x100];
        uint8_t inode_sum = 0;
        for (int page = inode_start_page; page < 128; page++) {
            put16(inode + page * 2, 0x0003);
            inode_sum += inode[page * 2] + inode[page * 2 + 1];
        }
        inode[1] = inode_sum;
        std::memcpy(&d[0x200], inode, 0x100);
    }

    Pak* get_pak(int channel) {
        if (channel < 0 || channel >= inserted_pak_count()) {
            return nullptr;
        }
        Pak& pak = paks[channel];
        if (!pak.loaded) {
            pak.path = data_dir / ("controllerpak_p" + std::to_string(channel + 1) + iss_version().pak_suffix + ".mpk");
            std::ifstream in(pak.path, std::ios::binary);
            if (in && in.read(reinterpret_cast<char*>(pak.data.data()), pak_size)) {
                fprintf(stderr, "[pak] port %d: loaded %s\n", channel + 1, pak.path.string().c_str());
            }
            else {
                format(pak.data);
                std::ofstream out(pak.path, std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char*>(pak.data.data()), pak_size);
                fprintf(stderr, "[pak] port %d: created formatted pak %s\n", channel + 1, pak.path.string().c_str());
            }
            pak.loaded = true;
        }
        return &pak;
    }

    void persist_block(const Pak& pak, uint32_t block) {
        std::fstream f(pak.path, std::ios::binary | std::ios::in | std::ios::out);
        if (!f) {
            f.open(pak.path, std::ios::binary | std::ios::out | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(pak.data.data()), pak_size);
            return;
        }
        f.seekp(block * block_size);
        f.write(reinterpret_cast<const char*>(&pak.data[block * block_size]), block_size);
    }
}

extern "C" void iss_controller_pak_set_data_dir(const char* dir) {
    std::lock_guard lock{ pak_mutex };
    data_dir = dir;
}

extern "C" bool iss_controller_pak_inserted(int channel) {
    return channel >= 0 && channel < inserted_pak_count();
}

// s32 __osPfsGetStatus(OSMesgQueue* mq, int channel)
extern "C" void __osPfsGetStatus_recomp(uint8_t* rdram, recomp_context* ctx) {
    std::lock_guard lock{ pak_mutex };
    ctx->r2 = get_pak((int)ctx->r5) != nullptr ? 0 : PFS_ERR_NOPACK;
}

// s32 osPfsIsPlug(OSMesgQueue* mq, u8* pattern)
extern "C" void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint8_t pattern = 0;
    for (int channel = 0; channel < 4; channel++) {
        if (iss_controller_pak_inserted(channel)) {
            pattern |= 1 << channel;
        }
    }
    MEM_B(0, ctx->r5) = pattern;
    ctx->r2 = 0;
}

// s32 __osContRamRead(OSMesgQueue* mq, int channel, u16 address, u8* buffer)
// address is in 32-byte blocks; 0x400+ is the bank latch / accessory area, which reads as zeros.
extern "C" void __osContRamRead_recomp(uint8_t* rdram, recomp_context* ctx) {
    std::lock_guard lock{ pak_mutex };
    Pak* pak = get_pak((int)ctx->r5);
    if (pak == nullptr) {
        ctx->r2 = PFS_ERR_NOPACK;
        return;
    }
    uint32_t block = (uint32_t)ctx->r6 & 0xFFFF;
    gpr buffer = ctx->r7;
    for (uint32_t i = 0; i < block_size; i++) {
        MEM_B(i, buffer) = block < num_blocks ? pak->data[block * block_size + i] : 0;
    }
    ctx->r2 = 0;
}

// s32 __osContRamWrite(OSMesgQueue* mq, int channel, u16 address, u8* buffer, int force)
// Writes to the bank latch are ignored: a 32 KB pak has a single bank, every bank mirrors it.
extern "C" void __osContRamWrite_recomp(uint8_t* rdram, recomp_context* ctx) {
    std::lock_guard lock{ pak_mutex };
    Pak* pak = get_pak((int)ctx->r5);
    if (pak == nullptr) {
        ctx->r2 = PFS_ERR_NOPACK;
        return;
    }
    uint32_t block = (uint32_t)ctx->r6 & 0xFFFF;
    gpr buffer = ctx->r7;
    if (block < num_blocks) {
        for (uint32_t i = 0; i < block_size; i++) {
            pak->data[block * block_size + i] = MEM_B(i, buffer);
        }
        persist_block(*pak, block);
    }
    ctx->r2 = 0;
}
