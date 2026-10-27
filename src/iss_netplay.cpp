#include "iss_netplay.h"
#include "iss_words.h"
#include "iss_version.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <array>
#include <algorithm>
#include <random>
#include <atomic>
#include <condition_variable>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define CLOSESOCKET(s) closesocket(s)
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define CLOSESOCKET(s) close(s)
#endif

namespace iss {
namespace net {

namespace {

constexpr uint32_t NET_MAGIC = 0x4E535349; // 'I', 'S', 'S', 'N' in little endian
constexpr size_t BUFFER_SIZE = 512;
constexpr size_t BUFFER_MASK = BUFFER_SIZE - 1;
constexpr size_t REDUNDANT_INPUTS = 12;

enum PacketType : uint8_t {
    PKT_NONE = 0,
    PKT_PUNCH = 1,
    PKT_PUNCH_ACK = 2,
    PKT_HANDSHAKE = 3,
    PKT_HANDSHAKE_ACK = 4,
    PKT_INPUT = 5,
    PKT_DISCONNECT = 6,
    PKT_STATUS = 7,
    PKT_VI_STATUS = 8
};

#pragma pack(push, 1)
struct NetPacket {
    uint32_t magic;
    uint8_t type;
    uint32_t base_frame;
    uint8_t input_count;
    NetInputState inputs[REDUNDANT_INPUTS];
    char rom_version[8];
    uint32_t seed; // Per-session token after handshake; zero before negotiation.
    uint32_t sender_lan_ip;
    uint16_t sender_lan_port;
    uint32_t check_frame;     // PKT_INPUT: frame of check_hash (desync detection)
    uint32_t check_hash;      // PKT_INPUT: game RNG states at the start of check_frame
};

// Dolphin Traversal Protocol definitions
constexpr size_t NETPLAY_CODE_SIZE = 8;

enum class TraversalPacketType : uint8_t {
    Ack = 0,
    Ping = 1,
    HelloFromClient = 2,
    HelloFromServer = 3,
    ConnectPlease = 4,
    PleaseSendPacket = 5,
    ConnectReady = 6,
    ConnectFailed = 7,
    TestPlease = 8
};

struct TraversalInetAddress {
    uint8_t isIPV6;
    uint32_t address[4];
    uint16_t port;
};

struct TraversalPacket {
    TraversalPacketType type;
    uint64_t requestId;
    union {
        struct {
            uint8_t ok;
        } ack;
        struct {
            char hostId[NETPLAY_CODE_SIZE];
        } ping;
        struct {
            uint8_t protoVersion;
        } helloFromClient;
        struct {
            uint8_t ok;
            char yourHostId[NETPLAY_CODE_SIZE];
            TraversalInetAddress yourAddress;
        } helloFromServer;
        struct {
            char hostId[NETPLAY_CODE_SIZE];
        } connectPlease;
        struct {
            TraversalInetAddress address;
        } pleaseSendPacket;
        struct {
            uint64_t requestId;
            TraversalInetAddress address;
        } connectReady;
        struct {
            uint64_t requestId;
            uint8_t reason;
        } connectFailed;
        struct {
            char hostId[NETPLAY_CODE_SIZE];
        } testPlease;
    };
};
#pragma pack(pop)

NetConfig g_config;
std::atomic<NetState> g_state{STATE_DISCONNECTED};
SOCKET g_socket = INVALID_SOCKET;
sockaddr_in g_peer_addr{};
sockaddr_in g_peer_lan_addr{};
sockaddr_in g_traversal_addr{};
bool g_has_peer_addr = false;
bool g_has_peer_lan_addr = false;
bool g_traversal_registered = false;
std::atomic<bool> g_return_to_launcher{false};
std::atomic<int64_t> g_last_peer_activity_ns{0};
std::atomic<int64_t> g_peer_quit_time_ns{0};
std::atomic<bool> g_network_worker_running{false};
std::thread g_network_worker;
std::recursive_mutex g_network_mutex;
std::condition_variable_any g_network_cv;
std::atomic<int64_t> g_last_status_sent_ns{0};
std::string g_traversal_host_id;
uint64_t g_traversal_connect_request_id = 0;
std::string g_status_info = "Ready";

std::string g_rom_version;
std::atomic<uint32_t> g_current_frame{0};
uint32_t g_target_frame = 0;
uint32_t g_remote_highest_frame = 0;
uint32_t g_session_token = 0;
std::atomic<uint32_t> g_local_vi_count{0};
uint32_t g_remote_vi_count = 0;
bool g_vi_clock_started = false;
std::chrono::steady_clock::time_point g_last_ping_time{};
std::chrono::steady_clock::time_point g_last_broadcast_time{};
std::chrono::steady_clock::time_point g_session_start_time{};

std::array<NetInputState, BUFFER_SIZE> g_local_buffer{};
std::array<NetInputState, BUFFER_SIZE> g_remote_buffer{};
std::array<uint32_t, BUFFER_SIZE> g_remote_received_frame{};
std::array<bool, BUFFER_SIZE> g_remote_has_frame{};

// Desync detection: both peers hash the game's RNG states when they poll input for a frame; the
// hashes travel in input packets and are compared once both sides have one for the same frame.
uint8_t* g_rdram = nullptr;
std::array<uint32_t, BUFFER_SIZE> g_local_hash{};
std::array<uint32_t, BUFFER_SIZE> g_local_hash_frame{};
std::array<bool, BUFFER_SIZE> g_local_has_hash{};
std::array<uint32_t, BUFFER_SIZE> g_remote_hash{};
std::array<uint32_t, BUFFER_SIZE> g_remote_hash_frame{};
std::array<bool, BUFFER_SIZE> g_remote_has_hash{};
bool g_desync_reported = false;

uint32_t game_state_hash() {
    if (g_rdram == nullptr) return 0;
    uint32_t h = 2166136261u;
    for (uint32_t addr : iss_version().rng_states) {
        // RDRAM words are stored natively (same layout MEM_W uses), so an aligned word reads directly.
        uint32_t v;
        memcpy(&v, g_rdram + (addr - 0x80000000u), sizeof(v));
        h = (h ^ v) * 16777619u;
    }
    return h;
}

// Called with g_network_mutex held whenever either side's hash for a frame arrives.
void compare_hashes(uint32_t frame) {
    size_t slot = frame & BUFFER_MASK;
    if (g_desync_reported || !g_local_has_hash[slot] || !g_remote_has_hash[slot] ||
        g_local_hash_frame[slot] != frame || g_remote_hash_frame[slot] != frame) return;
    if (g_local_hash[slot] != g_remote_hash[slot]) {
        g_desync_reported = true;
        fprintf(stderr, "[Netplay] DESYNC detected at input frame %u (local %08X, remote %08X)\n",
                frame, g_local_hash[slot], g_remote_hash[slot]);
        g_status_info = "Desync detected at frame " + std::to_string(frame);
    }
}

uint64_t generate_request_id() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    return gen();
}

int64_t steady_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void mark_peer_activity() {
    g_last_peer_activity_ns.store(steady_now_ns(), std::memory_order_relaxed);
    g_network_cv.notify_all();
}

bool is_private_ip(uint32_t ip_network_order) {
    uint32_t ip = ntohl(ip_network_order);
    if ((ip & 0xFF000000) == 0x0A000000) return true; // 10.0.0.0/8
    if ((ip & 0xFFF00000) == 0xAC100000) return true; // 172.16.0.0/12
    if ((ip & 0xFFFF0000) == 0xC0A80000) return true; // 192.168.0.0/16
    if ((ip & 0xFF000000) == 0x7F000000) return true; // 127.0.0.0/8
    return false;
}

uint32_t get_local_lan_ip() {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) return 0;
    sockaddr_in probe{};
    probe.sin_family = AF_INET;
    probe.sin_port = htons(6262);
    inet_pton(AF_INET, "1.1.1.1", &probe.sin_addr);
    connect(s, (sockaddr*)&probe, sizeof(probe));
    sockaddr_in local{};
    socklen_t len = sizeof(local);
    uint32_t out_ip = 0;
    if (getsockname(s, (sockaddr*)&local, &len) == 0) {
        out_ip = local.sin_addr.s_addr;
    }
    CLOSESOCKET(s);
    return out_ip;
}

bool init_socket_subsystem() {
#ifdef _WIN32
    static bool wsa_inited = false;
    if (!wsa_inited) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            fprintf(stderr, "[Netplay] Failed to initialize Winsock\n");
            return false;
        }
        wsa_inited = true;
    }
#endif
    return true;
}

bool set_nonblocking(SOCKET sock) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool parse_address(const std::string& addr_str, sockaddr_in& out_addr, uint16_t default_port = 9000) {
    std::string host = addr_str;
    uint16_t port = default_port;

    size_t colon = addr_str.find(':');
    if (colon != std::string::npos) {
        host = addr_str.substr(0, colon);
        port = (uint16_t)std::atoi(addr_str.substr(colon + 1).c_str());
    }

    memset(&out_addr, 0, sizeof(out_addr));
    out_addr.sin_family = AF_INET;
    out_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &out_addr.sin_addr) <= 0) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) {
            return false;
        }
        out_addr.sin_addr = ((sockaddr_in*)res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }
    return true;
}

void send_raw(const void* data, size_t size, const sockaddr_in& dest) {
    if (g_socket == INVALID_SOCKET) return;
    sendto(g_socket, (const char*)data, (int)size, 0, (const sockaddr*)&dest, sizeof(dest));
}

void send_traversal_packet(const TraversalPacket& pkt) {
    send_raw(&pkt, sizeof(TraversalPacket), g_traversal_addr);
}

void send_game_packet(const NetPacket& pkt) {
    std::lock_guard lock(g_network_mutex);
    if (g_socket == INVALID_SOCKET || !g_has_peer_addr) return;
    NetPacket outgoing = pkt;
    if (outgoing.type == PKT_INPUT || outgoing.type == PKT_PUNCH ||
        outgoing.type == PKT_PUNCH_ACK || outgoing.type == PKT_DISCONNECT ||
        outgoing.type == PKT_STATUS || outgoing.type == PKT_VI_STATUS) {
        outgoing.seed = g_session_token;
    }
    send_raw(&outgoing, sizeof(NetPacket), g_peer_addr);
    if (g_has_peer_lan_addr) {
        send_raw(&outgoing, sizeof(NetPacket), g_peer_lan_addr);
    }
}

void send_status_if_due() {
    if (g_state != STATE_CONNECTED) return;
    int64_t now = steady_now_ns();
    int64_t last = g_last_status_sent_ns.load(std::memory_order_relaxed);
    if (now - last < 100'000'000LL ||
        !g_last_status_sent_ns.compare_exchange_strong(last, now, std::memory_order_relaxed)) return;
    NetPacket status{};
    status.magic = NET_MAGIC;
    status.type = PKT_STATUS;
    status.base_frame = g_current_frame;
    send_game_packet(status);
}

void send_lan_broadcast(const std::string& msg) {
    if (g_socket == INVALID_SOCKET) return;
    sockaddr_in bcast{};
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(9099);
    bcast.sin_addr.s_addr = INADDR_BROADCAST;
    send_raw(msg.data(), msg.size(), bcast);
}

// Drain incoming socket packets and process them
void process_incoming() {
    if (g_socket == INVALID_SOCKET) return;

    uint8_t buf[2048];
    sockaddr_in from_addr{};
    socklen_t from_len = sizeof(from_addr);

    while (true) {
        int recvd = recvfrom(g_socket, (char*)buf, sizeof(buf), 0, (sockaddr*)&from_addr, &from_len);
        if (recvd <= 0) break;

        // 1. Check for Traversal Server packet (exactly 37 bytes from traversal server)
        if (from_addr.sin_addr.s_addr == g_traversal_addr.sin_addr.s_addr &&
            from_addr.sin_port == g_traversal_addr.sin_port &&
            recvd == (int)sizeof(TraversalPacket)) {
            const TraversalPacket* tp = reinterpret_cast<const TraversalPacket*>(buf);
            switch (tp->type) {
                case TraversalPacketType::Ack:
                    break;
                case TraversalPacketType::HelloFromServer: {
                    if (tp->helloFromServer.ok) {
                        char hid[NETPLAY_CODE_SIZE + 1] = {};
                        memcpy(hid, tp->helloFromServer.yourHostId, NETPLAY_CODE_SIZE);
                        g_traversal_host_id = hid;
                        g_traversal_registered = true;
                        g_config.room_words = make_room_code(g_config.region, g_traversal_host_id);
                        g_status_info = "Room code ready: " + g_config.room_words;
                        fprintf(stderr, "[Netplay STUN] Got Host ID: %s -> %s\n", g_traversal_host_id.c_str(), g_config.room_words.c_str());

                    }
                    break;
                }
                case TraversalPacketType::PleaseSendPacket: {
                    // Traversal server tells host: client is trying to connect!
                    if (!tp->pleaseSendPacket.address.isIPV6) {
                        memset(&g_peer_addr, 0, sizeof(g_peer_addr));
                        g_peer_addr.sin_family = AF_INET;
                        g_peer_addr.sin_addr.s_addr = tp->pleaseSendPacket.address.address[0];
                        g_peer_addr.sin_port = tp->pleaseSendPacket.address.port;
                        g_has_peer_addr = true;

                        char ip_str[INET_ADDRSTRLEN] = {};
                        inet_ntop(AF_INET, &g_peer_addr.sin_addr, ip_str, sizeof(ip_str));
                        fprintf(stderr, "[Netplay STUN] Peer connecting from WAN %s:%u\n", ip_str, ntohs(g_peer_addr.sin_port));

                        // Send UDP punch packets directly to client
                        NetPacket punch{};
                        punch.magic = NET_MAGIC;
                        punch.type = PKT_PUNCH;
                        punch.sender_lan_ip = get_local_lan_ip();
                        punch.sender_lan_port = htons(g_config.local_port);
                        for (int i = 0; i < 4; i++) {
                            send_raw(&punch, sizeof(punch), g_peer_addr);
                        }
                    }
                    break;
                }
                case TraversalPacketType::ConnectReady: {
                    // Traversal server tells guest: host's address is ready!
                    if (tp->connectReady.requestId == g_traversal_connect_request_id &&
                        !tp->connectReady.address.isIPV6) {
                        memset(&g_peer_addr, 0, sizeof(g_peer_addr));
                        g_peer_addr.sin_family = AF_INET;
                        g_peer_addr.sin_addr.s_addr = tp->connectReady.address.address[0];
                        g_peer_addr.sin_port = tp->connectReady.address.port;
                        g_has_peer_addr = true;

                        char ip_str[INET_ADDRSTRLEN] = {};
                        inet_ntop(AF_INET, &g_peer_addr.sin_addr, ip_str, sizeof(ip_str));
                        fprintf(stderr, "[Netplay STUN] Host WAN address received: %s:%u\n", ip_str, ntohs(g_peer_addr.sin_port));

                        // Send punch and handshake to host
                        NetPacket punch{};
                        punch.magic = NET_MAGIC;
                        punch.type = PKT_PUNCH;
                        punch.sender_lan_ip = get_local_lan_ip();
                        punch.sender_lan_port = htons(g_config.local_port);
                        for (int i = 0; i < 4; i++) {
                            send_raw(&punch, sizeof(punch), g_peer_addr);
                        }

                        NetPacket hs{};
                        hs.magic = NET_MAGIC;
                        hs.type = PKT_HANDSHAKE;
                        hs.seed = is_host() ? g_session_token : 0;
                        hs.sender_lan_ip = punch.sender_lan_ip;
                        hs.sender_lan_port = punch.sender_lan_port;
                        strncpy(hs.rom_version, g_rom_version.c_str(), sizeof(hs.rom_version) - 1);
                        send_raw(&hs, sizeof(hs), g_peer_addr);
                    }
                    break;
                }
                case TraversalPacketType::ConnectFailed: {
                    if (tp->connectFailed.requestId != g_traversal_connect_request_id) break;
                    fprintf(stderr, "[Netplay STUN] Traversal connection failed (reason: %u)\n", tp->connectFailed.reason);
                    g_status_info = "STUN code not found. Checking local network...";
                    break;
                }
                default:
                    break;
            }
            if (tp->type != TraversalPacketType::Ack) {
                TraversalPacket ack{};
                ack.type = TraversalPacketType::Ack;
                ack.requestId = tp->requestId;
                ack.ack.ok = 1;
                send_traversal_packet(ack);
            }
            continue;
        }

        // 2. Check for LAN text announcements/queries
        if (recvd > 14 && memcmp(buf, "ISS_LAN_", 8) == 0) {
            std::string text((const char*)buf, recvd);
            if (is_host() && text.rfind("ISS_LAN_QUERY ", 0) == 0) {
                std::string qwords = text.substr(14);
                if (normalize_words(qwords) == normalize_words(g_config.room_words)) {
                    // Match found on local network!
                    g_peer_addr = from_addr;
                    g_has_peer_addr = true;
                    char ip_str[64];
                    inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, sizeof(ip_str));
                    fprintf(stderr, "[Netplay LAN] Found matching guest directly on local LAN (%s:%u)!\n", ip_str, ntohs(from_addr.sin_port));

                    NetPacket punch{};
                    punch.magic = NET_MAGIC;
                    punch.type = PKT_PUNCH;
                    punch.sender_lan_ip = get_local_lan_ip();
                    punch.sender_lan_port = htons(g_config.local_port);
                    for (int i = 0; i < 4; i++) send_raw(&punch, sizeof(punch), from_addr);
                }
            } else if (!is_host() && text.rfind("ISS_LAN_ANNOUNCE ", 0) == 0) {
                char awords[128] = {};
                int aport = 9099;
                if (sscanf(text.c_str(), "ISS_LAN_ANNOUNCE %127s %d", awords, &aport) >= 1) {
                    if (normalize_words(awords) == normalize_words(g_config.room_words)) {
                        g_peer_addr = from_addr;
                        g_peer_addr.sin_port = htons((uint16_t)aport);
                        g_has_peer_addr = true;
                        char ip_str[64];
                        inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, sizeof(ip_str));
                        fprintf(stderr, "[Netplay LAN] Found matching host directly on local LAN (%s:%u)!\n", ip_str, aport);

                        NetPacket punch{};
                        punch.magic = NET_MAGIC;
                        punch.type = PKT_PUNCH;
                        punch.sender_lan_ip = get_local_lan_ip();
                        punch.sender_lan_port = htons(g_config.local_port);
                        for (int i = 0; i < 4; i++) send_raw(&punch, sizeof(punch), g_peer_addr);

                        NetPacket hs{};
                        hs.magic = NET_MAGIC;
                        hs.type = PKT_HANDSHAKE;
                        hs.seed = is_host() ? g_session_token : 0;
                        hs.sender_lan_ip = punch.sender_lan_ip;
                        hs.sender_lan_port = punch.sender_lan_port;
                        strncpy(hs.rom_version, g_rom_version.c_str(), sizeof(hs.rom_version) - 1);
                        send_raw(&hs, sizeof(hs), g_peer_addr);
                    }
                }
            }
            continue;
        }

        // 3. Binary Game Packets
        if (recvd != (int)sizeof(NetPacket)) continue;
        const NetPacket* pkt = reinterpret_cast<const NetPacket*>(buf);
        if (pkt->magic != NET_MAGIC) continue;

        if (g_state == STATE_CONNECTED) {
            bool known_peer = from_addr.sin_addr.s_addr == g_peer_addr.sin_addr.s_addr &&
                              from_addr.sin_port == g_peer_addr.sin_port;
            if (g_has_peer_lan_addr) {
                known_peer = known_peer ||
                    (from_addr.sin_addr.s_addr == g_peer_lan_addr.sin_addr.s_addr &&
                     from_addr.sin_port == g_peer_lan_addr.sin_port);
            }
            bool authenticated_rebind = pkt->type == PKT_PUNCH && pkt->seed == g_session_token;
            if (!known_peer && !authenticated_rebind) continue;
            if (!known_peer) {
                g_peer_addr = from_addr;
                g_has_peer_addr = true;
            }
        }
        if (g_state == STATE_CONNECTED && pkt->type != PKT_HANDSHAKE &&
            pkt->type != PKT_HANDSHAKE_ACK && pkt->seed != g_session_token) continue;

        // Auto-upgrade to local LAN address if peer is on the same private network
        if (is_private_ip(from_addr.sin_addr.s_addr)) {
            if (!is_private_ip(g_peer_addr.sin_addr.s_addr)) {
                char old_ip[64], new_ip[64];
                inet_ntop(AF_INET, &g_peer_addr.sin_addr, old_ip, sizeof(old_ip));
                inet_ntop(AF_INET, &from_addr.sin_addr, new_ip, sizeof(new_ip));
                fprintf(stderr, "[Netplay] Upgraded connection to zero-latency local LAN: %s -> %s:%u\n",
                        old_ip, new_ip, ntohs(from_addr.sin_port));
            }
            g_peer_addr = from_addr;
            g_has_peer_addr = true;
        } else if (!g_has_peer_addr) {
            g_peer_addr = from_addr;
            g_has_peer_addr = true;
        }

        // Check if packet advertises peer's LAN address
        if (pkt->sender_lan_ip != 0 && is_private_ip(pkt->sender_lan_ip)) {
            g_peer_lan_addr.sin_family = AF_INET;
            g_peer_lan_addr.sin_addr.s_addr = pkt->sender_lan_ip;
            g_peer_lan_addr.sin_port = pkt->sender_lan_port ? pkt->sender_lan_port : g_peer_addr.sin_port;
            g_has_peer_lan_addr = true;
        }

        switch (pkt->type) {
            case PKT_PUNCH: {
                if (g_state == STATE_CONNECTED) mark_peer_activity();
                NetPacket ack{};
                ack.magic = NET_MAGIC;
                ack.type = PKT_PUNCH_ACK;
                ack.seed = g_session_token;
                ack.sender_lan_ip = get_local_lan_ip();
                ack.sender_lan_port = htons(g_config.local_port);
                send_raw(&ack, sizeof(ack), from_addr);
                break;
            }
            case PKT_PUNCH_ACK: {
                if (g_state == STATE_CONNECTED) mark_peer_activity();
                if (g_state == STATE_CONNECTING || g_state == STATE_WAITING_FOR_PEER) {
                    NetPacket hs{};
                    hs.magic = NET_MAGIC;
                    hs.type = PKT_HANDSHAKE;
                    hs.seed = is_host() ? g_session_token : 0;
                    hs.sender_lan_ip = get_local_lan_ip();
                    hs.sender_lan_port = htons(g_config.local_port);
                    strncpy(hs.rom_version, g_rom_version.c_str(), sizeof(hs.rom_version) - 1);
                    send_game_packet(hs);
                }
                break;
            }
            case PKT_HANDSHAKE: {
                if (strncmp(pkt->rom_version, g_rom_version.c_str(), 7) != 0) {
                    fprintf(stderr, "[Netplay Error] ROM mismatch! Remote: %s, Local: %s\n", pkt->rom_version, g_rom_version.c_str());
                    g_status_info = "ROM version mismatch between players!";
                    g_state = STATE_ERROR;
                } else {
                    if (!is_host()) {
                        if (pkt->seed == 0) break;
                        g_session_token = pkt->seed;
                    }
                    NetPacket ack{};
                    ack.magic = NET_MAGIC;
                    ack.type = PKT_HANDSHAKE_ACK;
                    ack.seed = g_session_token;
                    ack.sender_lan_ip = get_local_lan_ip();
                    ack.sender_lan_port = htons(g_config.local_port);
                    strncpy(ack.rom_version, g_rom_version.c_str(), sizeof(ack.rom_version) - 1);
                    send_raw(&ack, sizeof(ack), from_addr);
                    if (!is_host()) {
                        mark_peer_activity();
                        if (g_state != STATE_CONNECTED) {
                            g_state = STATE_CONNECTED;
                            g_status_info = "Connected to opponent! Starting match...";
                            fprintf(stderr, "[Netplay] Handshake completed successfully!\n");
                        }
                    }
                }
                break;
            }
            case PKT_HANDSHAKE_ACK: {
                if (pkt->seed == 0 || pkt->seed != g_session_token) break;
                mark_peer_activity();
                if (g_state != STATE_CONNECTED) {
                    g_state = STATE_CONNECTED;
                    g_status_info = "Connected to opponent! Starting match...";
                    fprintf(stderr, "[Netplay] Handshake confirmed by peer!\n");
                }
                break;
            }
            case PKT_INPUT: {
                uint32_t base = pkt->base_frame;
                uint8_t count = pkt->input_count;
                if (count == 0 || count > REDUNDANT_INPUTS) break;
                if (g_state != STATE_CONNECTED) break;
                mark_peer_activity();
                if (base > g_remote_highest_frame) {
                    g_remote_highest_frame = base;
                }
                for (uint8_t i = 0; i < count; i++) {
                    if (base >= i) {
                        uint32_t f = base - i;
                        size_t slot = f & BUFFER_MASK;
                        g_remote_buffer[slot] = pkt->inputs[i];
                        g_remote_received_frame[slot] = f;
                        g_remote_has_frame[slot] = true;
                    }
                }
                {
                    size_t hslot = pkt->check_frame & BUFFER_MASK;
                    g_remote_hash[hslot] = pkt->check_hash;
                    g_remote_hash_frame[hslot] = pkt->check_frame;
                    g_remote_has_hash[hslot] = true;
                    compare_hashes(pkt->check_frame);
                }
                break;
            }
            case PKT_STATUS: {
                if (g_state != STATE_CONNECTED) break;
                mark_peer_activity();
                if (pkt->base_frame > g_remote_highest_frame) {
                    g_remote_highest_frame = pkt->base_frame;
                }
                break;
            }
            case PKT_VI_STATUS: {
                if (g_state != STATE_CONNECTED) break;
                mark_peer_activity();
                if (pkt->base_frame > g_remote_vi_count) {
                    g_remote_vi_count = pkt->base_frame;
                }
                break;
            }
            case PKT_DISCONNECT: {
                // Preserve the disconnect time separately so queued heartbeat packets cannot
                // accidentally cancel the five-second exit grace period.
                g_peer_quit_time_ns.store(steady_now_ns(), std::memory_order_relaxed);
                break;
            }
            default:
                break;
        }
    }
}

void start_network_worker() {
    if (g_socket == INVALID_SOCKET || g_config.mode == NET_MODE_OFFLINE ||
        g_config.mode == NET_MODE_LOCAL_2P || g_network_worker.joinable()) return;
    g_network_worker_running.store(true, std::memory_order_relaxed);
    g_network_worker = std::thread([] {
        while (g_network_worker_running.load(std::memory_order_relaxed)) {
            {
                std::lock_guard lock(g_network_mutex);
                process_incoming();
            }
            g_network_cv.notify_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
}

void stop_network_worker() {
    g_network_worker_running.store(false, std::memory_order_relaxed);
    g_network_cv.notify_all();
    if (g_network_worker.joinable()) g_network_worker.join();
}

} // namespace

bool is_active() {
    return g_config.mode != NET_MODE_OFFLINE;
}

bool is_2p_active() {
    return g_config.mode != NET_MODE_OFFLINE;
}

bool is_host() {
    return g_config.mode == NET_MODE_HOST || g_config.mode == NET_MODE_HOST_DIRECT;
}

bool is_online() {
    return g_config.mode == NET_MODE_HOST || g_config.mode == NET_MODE_JOIN ||
           g_config.mode == NET_MODE_HOST_DIRECT || g_config.mode == NET_MODE_JOIN_DIRECT;
}

bool get_game_count(uint32_t* count) {
    if (!is_online() || g_state != STATE_CONNECTED) return false;
    // Per-thread call index within the current input frame: the game thread makes the same calls in
    // the same order on both peers, and other threads cannot disturb its sequence.
    thread_local uint32_t t_frame = UINT32_MAX;
    thread_local uint32_t t_calls = 0;
    const uint32_t frame = g_current_frame.load(std::memory_order_relaxed);
    if (frame != t_frame) {
        t_frame = frame;
        t_calls = 0;
    }
    // CPU count runs at 46.875 MHz; one input frame is a 30 fps game frame (1562500 counts).
    *count = g_session_token + frame * 1562500u + ++t_calls * 64u;
    return true;
}

void set_rdram(uint8_t* rdram) {
    g_rdram = rdram;
}

bool is_traversal_ready() {
    return g_traversal_registered;
}

NetState get_state() {
    return g_state;
}

const NetConfig& get_config() {
    return g_config;
}

std::string get_active_room_words() {
    return g_config.room_words;
}

std::string get_status_info() {
    return g_status_info;
}

bool should_return_to_launcher() {
    if (g_return_to_launcher.load(std::memory_order_relaxed)) return true;
    const int64_t now = steady_now_ns();
    const int64_t quit_time = g_peer_quit_time_ns.load(std::memory_order_relaxed);
    const int64_t last_activity = g_last_peer_activity_ns.load(std::memory_order_relaxed);
    const bool peer_quit_expired = quit_time != 0 && now - quit_time >= 5'000'000'000LL;
    const bool link_timeout = g_state == STATE_CONNECTED && last_activity != 0 &&
                              now - last_activity >= 5'000'000'000LL;
    if (peer_quit_expired || link_timeout) {
        g_return_to_launcher.store(true, std::memory_order_relaxed);
        return true;
    }
    return false;
}

bool start_session(const NetConfig& config, const std::string& rom_version_id) {
    shutdown();

    g_config = config;
    g_config.frame_delay = std::clamp(g_config.frame_delay, 1, 12);
    if (!is_room_region(g_config.region)) g_config.region = "EU";
    g_rom_version = rom_version_id;
    g_current_frame = 0;
    g_target_frame = 0;
    g_remote_highest_frame = 0;
    g_session_token = (uint32_t)generate_request_id();
    if (g_session_token == 0) g_session_token = 1;
    g_has_peer_addr = false;
    g_has_peer_lan_addr = false;
    g_traversal_registered = false;
    g_return_to_launcher = false;
    g_last_peer_activity_ns.store(0, std::memory_order_relaxed);
    g_peer_quit_time_ns.store(0, std::memory_order_relaxed);
    g_last_status_sent_ns.store(0, std::memory_order_relaxed);
    g_local_vi_count.store(0, std::memory_order_relaxed);
    g_remote_vi_count = 0;
    g_vi_clock_started = false;
    g_traversal_host_id.clear();
    g_traversal_connect_request_id = 0;
    g_session_start_time = std::chrono::steady_clock::now();
    g_last_ping_time = g_session_start_time;
    g_last_broadcast_time = g_session_start_time;
    g_status_info = "Initializing netplay...";

    for (size_t i = 0; i < BUFFER_SIZE; i++) {
        g_local_buffer[i] = {};
        g_remote_buffer[i] = {};
        g_remote_received_frame[i] = 0;
        g_remote_has_frame[i] = false;
        g_local_has_hash[i] = false;
        g_remote_has_hash[i] = false;
    }
    g_desync_reported = false;

    if (g_config.mode == NET_MODE_OFFLINE) {
        g_state = STATE_DISCONNECTED;
        return true;
    }

    if (g_config.mode == NET_MODE_LOCAL_2P) {
        g_state = STATE_CONNECTED;
        g_status_info = "Local 2-Player mode active";
        fprintf(stderr, "[Netplay] Local 2-Player mode enabled\n");
        return true;
    }

    if (!init_socket_subsystem()) {
        g_state = STATE_ERROR;
        g_status_info = "Failed to initialize networking subsystem";
        return false;
    }

    g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_socket == INVALID_SOCKET) {
        fprintf(stderr, "[Netplay] Failed to create UDP socket\n");
        g_state = STATE_ERROR;
        g_status_info = "Failed to create UDP socket";
        return false;
    }

    if (!set_nonblocking(g_socket)) {
        fprintf(stderr, "[Netplay] Failed to set UDP socket non-blocking\n");
        CLOSESOCKET(g_socket);
        g_socket = INVALID_SOCKET;
        g_state = STATE_ERROR;
        g_status_info = "Failed to configure network socket";
        return false;
    }

    // Enable UDP broadcast
    int broadcast_on = 1;
    setsockopt(g_socket, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast_on, sizeof(broadcast_on));

    // Try binding to local port (port 9099 default for host, 0 for guest or fallback)
    uint16_t bind_port = is_host() ? g_config.local_port : 0;
    sockaddr_in local_bind{};
    local_bind.sin_family = AF_INET;
    local_bind.sin_addr.s_addr = INADDR_ANY;
    local_bind.sin_port = htons(bind_port);

    if (bind(g_socket, (sockaddr*)&local_bind, sizeof(local_bind)) == SOCKET_ERROR) {
        if (bind_port != 0) {
            // Port was busy, bind to any free port
            local_bind.sin_port = 0;
            if (bind(g_socket, (sockaddr*)&local_bind, sizeof(local_bind)) == SOCKET_ERROR) {
                fprintf(stderr, "[Netplay] Failed to bind local socket\n");
                CLOSESOCKET(g_socket);
                g_socket = INVALID_SOCKET;
                g_state = STATE_ERROR;
                g_status_info = "Failed to bind network socket";
                return false;
            }
        }
    }

    // Get assigned local port
    socklen_t addr_len = sizeof(local_bind);
    getsockname(g_socket, (sockaddr*)&local_bind, &addr_len);
    g_config.local_port = ntohs(local_bind.sin_port);
    fprintf(stderr, "[Netplay] Bound local socket on UDP port %u\n", g_config.local_port);

    // Resolve Traversal Server (stun.dolphin-emu.org:6262)
    if (!parse_address(g_config.traversal_server, g_traversal_addr, g_config.traversal_port)) {
        fprintf(stderr, "[Netplay] Warning: Could not resolve traversal server %s:%u\n",
                g_config.traversal_server.c_str(), g_config.traversal_port);
    }

    if (g_config.mode == NET_MODE_HOST_DIRECT) {
        fprintf(stderr, "[Netplay] Hosting direct UDP match on port %d...\n", g_config.local_port);
        g_state = STATE_WAITING_FOR_PEER;
        g_status_info = "Waiting for direct UDP connection on port " + std::to_string(g_config.local_port);
        return true;
    }

    if (g_config.mode == NET_MODE_JOIN_DIRECT) {
        if (!parse_address(g_config.direct_addr, g_peer_addr)) {
            g_state = STATE_ERROR;
            g_status_info = "Invalid direct host address: " + g_config.direct_addr;
            return false;
        }
        g_has_peer_addr = true;
        fprintf(stderr, "[Netplay] Joining direct UDP match at %s...\n", g_config.direct_addr.c_str());
        g_state = STATE_CONNECTING;
        g_status_info = "Connecting to " + g_config.direct_addr;
        return true;
    }

    if (g_config.mode == NET_MODE_HOST) {
        g_config.room_words.clear();
        g_state = STATE_WAITING_FOR_PEER;
        g_status_info = "Connecting to STUN traversal server...";

        // Send HelloFromClient to stun.dolphin-emu.org
        TraversalPacket hello{};
        hello.type = TraversalPacketType::HelloFromClient;
        hello.requestId = generate_request_id();
        hello.helloFromClient.protoVersion = 0;
        send_traversal_packet(hello);
        return true;
    }

    if (g_config.mode == NET_MODE_JOIN) {
        std::string raw_target = g_config.room_words;

        // Try decoding as 4 soccer words
        std::string host_id = decode_room_code(raw_target);
        if (host_id.empty()) {
            if (parse_address(raw_target, g_peer_addr, 9099)) {
                g_has_peer_addr = true;
                g_config.mode = NET_MODE_JOIN_DIRECT;
                g_state = STATE_CONNECTING;
                g_status_info = "Connecting to direct host " + raw_target + "...";
                return true;
            }
            g_state = STATE_ERROR;
            g_status_info = "Enter a region-tagged pass phrase, four soccer words, or a valid IP/hostname.";
            return false;
        }

        g_traversal_host_id = host_id;
        g_config.room_words = normalize_words(raw_target);
        g_state = STATE_CONNECTING;
        g_status_info = "Connecting via STUN traversal & searching local network...";

        // Send ConnectPlease to STUN server
        TraversalPacket cpkt{};
        cpkt.type = TraversalPacketType::ConnectPlease;
        cpkt.requestId = generate_request_id();
        g_traversal_connect_request_id = cpkt.requestId;
        memcpy(cpkt.connectPlease.hostId, host_id.data(), NETPLAY_CODE_SIZE);
        send_traversal_packet(cpkt);

        // Also query on LAN in parallel
        send_lan_broadcast("ISS_LAN_QUERY " + g_config.room_words);
        return true;
    }

    return true;
}

NetState update_session() {
    if (g_state == STATE_CONNECTED || g_state == STATE_DISCONNECTED || g_state == STATE_ERROR) {
        return g_state;
    }

    process_incoming();

    auto now = std::chrono::steady_clock::now();

    if (g_config.mode == NET_MODE_JOIN_DIRECT) {
        if (now - g_last_ping_time > std::chrono::milliseconds(200)) {
            NetPacket hs{};
            hs.magic = NET_MAGIC;
            hs.type = PKT_HANDSHAKE;
            hs.seed = is_host() ? g_session_token : 0;
            hs.sender_lan_ip = get_local_lan_ip();
            hs.sender_lan_port = htons(g_config.local_port);
            strncpy(hs.rom_version, g_rom_version.c_str(), sizeof(hs.rom_version) - 1);
            send_raw(&hs, sizeof(hs), g_peer_addr);
            g_last_ping_time = now;
        }
        return g_state;
    }

    if (g_config.mode == NET_MODE_HOST_DIRECT) {
        // The guest's first handshake gives us its address; answer with ours, which carries the session token.
        if (g_has_peer_addr && now - g_last_ping_time > std::chrono::milliseconds(200)) {
            NetPacket hs{};
            hs.magic = NET_MAGIC;
            hs.type = PKT_HANDSHAKE;
            hs.seed = g_session_token;
            hs.sender_lan_ip = get_local_lan_ip();
            hs.sender_lan_port = htons(g_config.local_port);
            strncpy(hs.rom_version, g_rom_version.c_str(), sizeof(hs.rom_version) - 1);
            send_game_packet(hs);
            g_last_ping_time = now;
        }
        return g_state;
    }

    if (g_config.mode == NET_MODE_HOST) {
        // Keepalive with STUN server
        if (now - g_last_ping_time > std::chrono::seconds(3)) {
            if (g_traversal_registered && !g_traversal_host_id.empty()) {
                TraversalPacket ping{};
                ping.type = TraversalPacketType::Ping;
                ping.requestId = generate_request_id();
                memcpy(ping.ping.hostId, g_traversal_host_id.data(), NETPLAY_CODE_SIZE);
                send_traversal_packet(ping);
            } else {
                // Re-send HelloFromClient if not yet registered
                TraversalPacket hello{};
                hello.type = TraversalPacketType::HelloFromClient;
                hello.requestId = generate_request_id();
                hello.helloFromClient.protoVersion = 0;
                send_traversal_packet(hello);
            }
            g_last_ping_time = now;
        }

        // LAN Broadcast announcement every 1.2 seconds
        if (!g_has_peer_addr && now - g_last_broadcast_time > std::chrono::milliseconds(1200)) {
            if (!g_config.room_words.empty()) {
                send_lan_broadcast("ISS_LAN_ANNOUNCE " + g_config.room_words + " " + std::to_string(g_config.local_port));
            }
            g_last_broadcast_time = now;
        }

        // Send handshake once peer address is known
        if (g_has_peer_addr && now - g_last_ping_time > std::chrono::milliseconds(200)) {
            NetPacket hs{};
            hs.magic = NET_MAGIC;
            hs.type = PKT_HANDSHAKE;
            hs.seed = is_host() ? g_session_token : 0;
            hs.sender_lan_ip = get_local_lan_ip();
            hs.sender_lan_port = htons(g_config.local_port);
            strncpy(hs.rom_version, g_rom_version.c_str(), sizeof(hs.rom_version) - 1);
            send_game_packet(hs);
            g_last_ping_time = now;
        }
    } else if (g_config.mode == NET_MODE_JOIN) {
        // Retry ConnectPlease with STUN and LAN query every 1.2 seconds if waiting
        if (!g_has_peer_addr && now - g_last_ping_time > std::chrono::milliseconds(1200)) {
            if (!g_traversal_host_id.empty()) {
                TraversalPacket cpkt{};
                cpkt.type = TraversalPacketType::ConnectPlease;
                cpkt.requestId = generate_request_id();
                g_traversal_connect_request_id = cpkt.requestId;
                memcpy(cpkt.connectPlease.hostId, g_traversal_host_id.data(), NETPLAY_CODE_SIZE);
                send_traversal_packet(cpkt);
            }
            send_lan_broadcast("ISS_LAN_QUERY " + g_config.room_words);
            g_last_ping_time = now;
        }

        if (g_has_peer_addr && now - g_last_ping_time > std::chrono::milliseconds(200)) {
            NetPacket hs{};
            hs.magic = NET_MAGIC;
            hs.type = PKT_HANDSHAKE;
            hs.seed = is_host() ? g_session_token : 0;
            hs.sender_lan_ip = get_local_lan_ip();
            hs.sender_lan_port = htons(g_config.local_port);
            strncpy(hs.rom_version, g_rom_version.c_str(), sizeof(hs.rom_version) - 1);
            send_game_packet(hs);
            g_last_ping_time = now;
        }
    }

    return g_state;
}

bool init(const NetConfig& config, const std::string& rom_version_id) {
    if (g_state == STATE_CONNECTED) {
        start_network_worker();
        return true;
    }
    if (!start_session(config, rom_version_id)) {
        return false;
    }
    auto start = std::chrono::steady_clock::now();
    int timeout_sec = (config.mode == NET_MODE_HOST) ? 120 : 30;
    while (g_state != STATE_CONNECTED && g_state != STATE_ERROR &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(timeout_sec)) {
        update_session();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (g_state == STATE_CONNECTED) start_network_worker();
    return g_state == STATE_CONNECTED;
}

void on_vi_tick() {
    if (g_config.mode == NET_MODE_OFFLINE || g_config.mode == NET_MODE_LOCAL_2P ||
        g_state != STATE_CONNECTED || should_return_to_launcher()) return;

    uint32_t local_count;
    {
        std::lock_guard<std::recursive_mutex> lock(g_network_mutex);
        if (!g_vi_clock_started) {
            g_local_vi_count.store(g_remote_vi_count, std::memory_order_relaxed);
            g_vi_clock_started = true;
        }
        local_count = g_local_vi_count.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    NetPacket status{};
    status.magic = NET_MAGIC;
    status.type = PKT_VI_STATUS;
    status.base_frame = local_count;
    send_game_packet(status);

    std::unique_lock<std::recursive_mutex> lock(g_network_mutex);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const uint32_t max_vi_lead = (uint32_t)std::max(3, g_config.frame_delay + 2);
    while (local_count > g_remote_vi_count + max_vi_lead && g_state == STATE_CONNECTED &&
           !should_return_to_launcher()) {
        if (g_network_cv.wait_until(lock, deadline) == std::cv_status::timeout) {
            g_status_info = "Connection lost. Returning to launcher...";
            g_return_to_launcher.store(true, std::memory_order_relaxed);
            break;
        }
    }
}

void poll_and_sync(const NetInputState& local_input) {
    if (g_config.mode == NET_MODE_OFFLINE || g_config.mode == NET_MODE_LOCAL_2P) {
        return;
    }

    // 1. Record local input for current frame
    size_t local_slot = g_current_frame & BUFFER_MASK;
    const uint32_t state_hash = game_state_hash();
    {
        std::lock_guard<std::recursive_mutex> lock(g_network_mutex);
        g_local_buffer[local_slot] = local_input;
        g_local_hash[local_slot] = state_hash;
        g_local_hash_frame[local_slot] = g_current_frame;
        g_local_has_hash[local_slot] = true;
        compare_hashes(g_current_frame);
    }
    static const bool sync_log = std::getenv("ISS_NET_DEBUG") != nullptr;
    if (sync_log && g_current_frame % 300 == 0) {
        fprintf(stderr, "[Netplay] frame %u state hash %08X\n", g_current_frame.load(), state_hash);
    }

    // 2. Build and transmit packet with redundant past 12 inputs (instant packet-loss recovery)
    NetPacket pkt{};
    pkt.magic = NET_MAGIC;
    pkt.type = PKT_INPUT;
    pkt.seed = g_session_token;
    pkt.base_frame = g_current_frame;
    uint8_t count = (g_current_frame < REDUNDANT_INPUTS) ? (uint8_t)(g_current_frame + 1) : (uint8_t)REDUNDANT_INPUTS;
    pkt.input_count = count;
    pkt.check_frame = g_current_frame;
    pkt.check_hash = state_hash;

    {
        std::lock_guard<std::recursive_mutex> lock(g_network_mutex);
        for (uint8_t i = 0; i < count; i++) {
            pkt.inputs[i] = g_local_buffer[(g_current_frame - i) & BUFFER_MASK];
        }
    }
    send_game_packet(pkt);
    send_status_if_due();

    // 3. Determine target frame for lockstep
    {
        std::lock_guard<std::recursive_mutex> lock(g_network_mutex);
        g_target_frame = (g_current_frame >= (uint32_t)g_config.frame_delay)
                             ? (g_current_frame - (uint32_t)g_config.frame_delay)
                             : 0;
    }

    size_t target_slot = g_target_frame & BUFFER_MASK;

    // 4. Do not advance the game with guessed input. Wait for the delayed target frame while
    // servicing the socket; this keeps both simulations on the same input history.
    {
        std::unique_lock<std::recursive_mutex> lock(g_network_mutex);
        if (!g_remote_has_frame[target_slot] || g_remote_received_frame[target_slot] != g_target_frame) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            auto next_probe = std::chrono::steady_clock::now();
            while ((!g_remote_has_frame[target_slot] || g_remote_received_frame[target_slot] != g_target_frame) &&
                   std::chrono::steady_clock::now() < deadline && !should_return_to_launcher()) {
                g_network_cv.wait_for(lock, std::chrono::milliseconds(10));
                lock.unlock();
                if (std::chrono::steady_clock::now() >= next_probe) {
                    NetPacket probe{};
                    probe.magic = NET_MAGIC;
                    probe.type = PKT_PUNCH;
                    probe.sender_lan_ip = get_local_lan_ip();
                    probe.sender_lan_port = htons(g_config.local_port);
                    send_game_packet(probe);
                    send_status_if_due();
                    next_probe = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
                }
                lock.lock();
            }
        }
        if (!g_remote_has_frame[target_slot] || g_remote_received_frame[target_slot] != g_target_frame) {
            fprintf(stderr, "[Netplay] Remote input frame %u did not arrive within 5 seconds\n", g_target_frame);
            g_status_info = "Connection lost. Returning to launcher...";
            g_state = STATE_DISCONNECTED;
            g_return_to_launcher.store(true, std::memory_order_relaxed);
            return;
        }
    }

    g_current_frame++;
}

bool get_synced_input(int controller_num, uint16_t* buttons, float* x, float* y) {
    if (controller_num > 1) {
        return false;
    }

    if (g_config.mode == NET_MODE_LOCAL_2P) {
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(g_network_mutex);
    size_t target_slot = g_target_frame & BUFFER_MASK;
    NetInputState chosen{};

    if (is_host()) {
        chosen = (controller_num == 0) ? g_local_buffer[target_slot] : g_remote_buffer[target_slot];
    } else {
        chosen = (controller_num == 0) ? g_remote_buffer[target_slot] : g_local_buffer[target_slot];
    }

    *buttons = chosen.buttons;
    *x = (float)chosen.stick_x / 127.0f;
    *y = (float)chosen.stick_y / 127.0f;
    return true;
}

void shutdown() {
    stop_network_worker();
    if (g_socket != INVALID_SOCKET) {
        if (g_has_peer_addr) {
            NetPacket bye{};
            bye.magic = NET_MAGIC;
            bye.type = PKT_DISCONNECT;
            send_game_packet(bye);
        }
        CLOSESOCKET(g_socket);
        g_socket = INVALID_SOCKET;
    }
    g_state = STATE_DISCONNECTED;
    g_config.mode = NET_MODE_OFFLINE;
    g_return_to_launcher = false;
    g_has_peer_addr = false;
    g_has_peer_lan_addr = false;
    g_last_peer_activity_ns.store(0, std::memory_order_relaxed);
    g_peer_quit_time_ns.store(0, std::memory_order_relaxed);
}

} // namespace net
} // namespace iss
