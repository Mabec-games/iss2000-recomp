#ifndef ISS_NETPLAY_H
#define ISS_NETPLAY_H

#include <cstdint>
#include <string>
#include <vector>

namespace iss {
namespace net {

enum NetMode {
    NET_MODE_OFFLINE,     // Single player (Controller 0 only)
    NET_MODE_LOCAL_2P,    // 2-player on same PC (P1 and P2 local)
    NET_MODE_HOST,        // Host online match (Dolphin STUN traversal + LAN broadcast)
    NET_MODE_JOIN,        // Join online match (4-word soccer pass phrase or IP)
    NET_MODE_HOST_DIRECT, // Host direct UDP (specify port)
    NET_MODE_JOIN_DIRECT  // Join direct UDP (specify ip:port)
};

enum NetState {
    STATE_DISCONNECTED,
    STATE_WAITING_FOR_PEER,
    STATE_CONNECTING,
    STATE_CONNECTED,
    STATE_ERROR
};

struct NetConfig {
    NetMode mode = NET_MODE_OFFLINE;
    std::string room_words;
    std::string region = "EU";
    std::string traversal_server = "stun.dolphin-emu.org";
    uint16_t traversal_port = 6262;
    std::string broker_addr = "127.0.0.1:9099";
    std::string direct_addr = "127.0.0.1:9099";
    uint16_t local_port = 9099;
    int frame_delay = 3; // 3-4 frames buffer to absorb network jitter without stutter
};

#pragma pack(push, 1)
struct NetInputState {
    uint16_t buttons = 0;
    int8_t stick_x = 0;
    int8_t stick_y = 0;
};
#pragma pack(pop)

// Public Netplay API
bool is_active();
bool is_2p_active();
bool is_host();
bool is_traversal_ready();
NetState get_state();
const NetConfig& get_config();
std::string get_active_room_words();
std::string get_status_info();

// Non-blocking session setup for GUI launchers
bool start_session(const NetConfig& config, const std::string& rom_version_id);
NetState update_session();
// True after a connected session has been lost for the configured grace period.
bool should_return_to_launcher();

// Blocking initialization (used by CLI or fallback)
bool init(const NetConfig& config, const std::string& rom_version_id);

// Called once per frame right before get_input()
void poll_and_sync(const NetInputState& local_input);
// Called once per emulated VI to keep both game clocks on the same tick.
void on_vi_tick();

// Query synchronized input for a given controller channel (0 or 1)
bool get_synced_input(int controller_num, uint16_t* buttons, float* x, float* y);

// True for the UDP modes (host/join), where both machines must run the game identically.
bool is_online();
// Game-visible osGetCount during an online match: derived from the synced input frame and the session
// token instead of the host clock, so the game's RNG seeds (taken from osGetCount) match on both peers.
// Returns false when the real clock should be used.
bool get_game_count(uint32_t* count);
// RDRAM base, for the desync check (reads the game's RNG states each synced frame).
void set_rdram(uint8_t* rdram);

// Clean up sockets
void shutdown();

} // namespace net
} // namespace iss

#endif // ISS_NETPLAY_H
