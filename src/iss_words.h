#ifndef ISS_WORDS_H
#define ISS_WORDS_H

#include <string>
#include <string_view>
#include <vector>
#include <random>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <optional>

namespace iss {
namespace net {

// 256 unique, authentic soccer-themed words for room codes.
// Order is preserved so every 8-hex-digit Traversal HostId (4 bytes)
// maps deterministically to exactly 4 soccer words.
inline const char* const SOCCER_WORDS[256] = {
    // Players & Roles (32)
    "STRIKER", "KEEPER", "WINGER", "DEFENDER", "MIDFIELD", "CAPTAIN", "SWEEPER", "PLAYMAKER",
    "FORWARD", "STOPPER", "REFEREE", "COACH", "SUBSTITUTE", "LINESMAN", "GOALIE", "TARGETMAN",
    "POACHER", "LIBERO", "ANCHOR", "TREQUARTISTA", "REGISTA", "MARKER", "FULLBACK", "WINGBACK",
    "CENTREBACK", "ATTACKER", "ROOKIE", "VETERAN", "MANAGER", "OFFICIAL", "SKIPPER", "LEGEND",

    // Skills & Moves (32)
    "DRIBBLE", "VOLLEY", "HEADER", "TACKLE", "BACKHEEL", "NUTMEG", "BICYCLE", "CHIP",
    "LOB", "SLIDE", "PASS", "ASSIST", "ROULETTE", "STEPPING", "FLICK", "DUMMY",
    "CROSS", "SWERVE", "CURL", "STRIKE", "SPRINT", "INTERCEPT", "BLOCK", "SAVE",
    "PUNCH", "DIVING", "JUKE", "FEINT", "DRIVE", "ONETOUCH", "WALLPASS", "CLEARANCE",

    // Match Events & Rules (32)
    "GOAL", "OFFSIDE", "PENALTY", "CORNER", "FREEKICK", "THROWIN", "FOUL", "HANDBALL",
    "YELLOW", "REDCARD", "WHISTLE", "KICKOFF", "HALFTIME", "FULLTIME", "EXTRATIME", "SHOOTOUT",
    "HATTRICK", "BRACE", "CLEANSHEET", "OWNGOAL", "REBOUND", "ADVANTAGE", "STOPPAGE", "ADDEDTIME",
    "GOLDENGOAL", "SUDDENDEATH", "INJURYTIME", "BOOKING", "CAUTION", "SENDOFF", "WALL", "SUBSTITUTION",

    // Field & Stadium (32)
    "STADIUM", "ARENA", "PITCH", "FIELD", "CROSSBAR", "POST", "NET", "GOALLINE",
    "BOX", "SIXYARD", "SPOT", "HALFWAY", "CIRCLE", "TOUCHLINE", "BYLINE", "BENCH",
    "DUGOUT", "TUNNEL", "STANDS", "TERRACE", "SCOREBOARD", "LOCKER", "FLOODLIGHT", "TURF",
    "GRASS", "CENTER", "FLANK", "CORNERFLAG", "PENALTYBOX", "CENTERCIRCLE", "GOALMOUTH", "SIDELINE",

    // Kit & Equipment (32)
    "BALL", "JERSEY", "BOOTS", "CLEATS", "SHINPAD", "GLOVES", "SHORTS", "SOCKS",
    "ARMBAND", "PENNANT", "TROPHY", "CUP", "MEDAL", "CREST", "BADGE", "BANNER",
    "FLAG", "CONE", "WHISTLEBLOW", "KIT", "STRIP", "SCARF", "PATCH", "SHIRT",
    "LEATHER", "STUD", "LACES", "VEST", "JACKET", "TOWEL", "CHALK", "BIB",

    // Tactics & Formations (32)
    "TIKITAKA", "COUNTER", "ATTACK", "DEFENSE", "PRESSING", "HIGHLINE", "OFFSIDETRAP", "ZONAL",
    "MANMARK", "FORMATION", "FOURFOURTWO", "TOTALFOOTBALL", "CATENACCIO", "PARKTHEBUS", "CROSSING", "OVERLOAD",
    "POSSESSION", "BUILDUP", "SWITCH", "TRANSITION", "TEMPO", "PACE", "STRATEGY", "DIRECT",
    "LONGBALL", "WINGPLAY", "HIGHPRESS", "LOWBLOCK", "COMPACT", "FLUID", "OVERLAP", "UNDERLAP",

    // Game Moments & Glory (32)
    "CHAMPION", "VICTORY", "DERBY", "FINAL", "SEMIFINAL", "DERBYMATCH", "CLASSICO", "PLAYOFF",
    "LEAGUE", "TOURNAMENT", "TITLE", "SUPERSTAR", "INTERNATIONAL", "PROLEAGUE", "SILVERWARE", "GOLDMEDAL",
    "MATCHDAY", "WORLDCUP", "COMEBACK", "EQUALIZER", "WINNER", "DECIDER", "THRILLER", "DERBYCUP",
    "OPENER", "UPSET", "RIVALRY", "DERBYDAY", "QUALIFIER", "CHAMPIONSHIP", "GLORY", "CELEBRATION",

    // Football Heritage & Konami ISS Legends (32)
    "KONAMI", "RONALDINHO", "RONALDO", "ZIDANE", "MARADONA", "PELE", "MESSI", "CRUYFF",
    "BECKENBAUER", "MULLER", "BAGGIO", "MALCOLM", "CARBONI", "SIEKE", "MURILLO", "KASTLE",
    "REDONDO", "BATISTUTA", "ROMARIO", "BEBETO", "DELPIERO", "TOTTI", "KAHN", "BUFFON",
    "CHILAVERT", "VALDERRAMA", "MATTHAUS", "KLINSMANN", "BERGKAMP", "HENRY", "SHEVCHENKO", "NEDVED"
};

constexpr size_t SOCCER_WORDS_COUNT = 256;

inline bool is_room_region(const std::string& region) {
    static const char* const regions[] = {"EU", "NA", "SA", "ASIA", "OC", "AF"};
    for (const char* candidate : regions) {
        if (region.size() != std::char_traits<char>::length(candidate)) continue;
        bool matches = true;
        for (size_t i = 0; i < region.size(); i++) {
            if (std::toupper((unsigned char)region[i]) != candidate[i]) {
                matches = false;
                break;
            }
        }
        if (matches) return true;
    }
    return false;
}

inline int hex_digit_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline int find_soccer_word_index(const std::string& word) {
    for (size_t i = 0; i < SOCCER_WORDS_COUNT; i++) {
        const char* candidate = SOCCER_WORDS[i];
        if (word.size() != std::string_view(candidate).size()) continue;
        bool match = true;
        for (size_t j = 0; j < word.size(); j++) {
            if (std::toupper((unsigned char)word[j]) != candidate[j]) {
                match = false;
                break;
            }
        }
        if (match) return (int)i;
    }
    return -1;
}

// Encode an 8-hex-character host code (e.g. "121382fc") into 4 soccer words: "VOLLEY-HEADER-..."
inline std::string encode_room_code(const std::string& host_id) {
    if (host_id.size() != 8) return "";
    std::string result;
    for (size_t i = 0; i < 8; i += 2) {
        int hi = hex_digit_val(host_id[i]);
        int lo = hex_digit_val(host_id[i + 1]);
        if (hi < 0 || lo < 0) return "";
        size_t idx = (size_t)(hi * 16 + lo);
        if (!result.empty()) result += "-";
        result += SOCCER_WORDS[idx % SOCCER_WORDS_COUNT];
    }
    return result;
}

inline std::string make_room_code(const std::string& region, const std::string& host_id) {
    std::string words = encode_room_code(host_id);
    if (words.empty() || !is_room_region(region)) return words;
    std::string prefix = region;
    std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char c) {
        return (char)std::toupper(c);
    });
    return prefix + "-" + words;
}

// Decode a 4-word soccer phrase into an 8-hex-character host code
inline std::string decode_room_code(const std::string& room_code) {
    std::vector<std::string> words;
    std::string cur;
    for (char c : room_code) {
        if (c == '-' || c == '_' || c == ' ' || c == '\t' || c == ',') {
            if (!cur.empty()) {
                words.push_back(cur);
                cur.clear();
            }
        } else if (std::isalnum((unsigned char)c)) {
            cur += c;
        }
    }
    if (!cur.empty()) words.push_back(cur);

    if (words.size() == 5 && is_room_region(words.front())) {
        words.erase(words.begin());
    }
    if (words.size() != 4) return "";

    static const char hex_chars[] = "0123456789abcdef";
    std::string host_id;
    for (const auto& w : words) {
        int idx = find_soccer_word_index(w);
        if (idx < 0) return "";
        host_id += hex_chars[(idx / 16) & 0x0F];
        host_id += hex_chars[idx & 0x0F];
    }
    return host_id;
}

// Normalize user-typed pass phrase to uppercase hyphenated format
inline std::string normalize_words(const std::string& input) {
    std::vector<std::string> words;
    std::string cur;
    for (char c : input) {
        if (c == '-' || c == '_' || c == ' ' || c == '\t' || c == ',') {
            if (!cur.empty()) {
                words.push_back(cur);
                cur.clear();
            }
        } else if (std::isalnum((unsigned char)c)) {
            cur += (char)std::toupper((unsigned char)c);
        }
    }
    if (!cur.empty()) words.push_back(cur);

    std::string result;
    for (size_t i = 0; i < words.size(); i++) {
        if (i > 0) result += "-";
        result += words[i];
    }
    return result;
}

// Generate 4 random soccer words for local/offline testing
inline std::string generate_words() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, SOCCER_WORDS_COUNT - 1);

    size_t w1 = dist(gen);
    size_t w2 = dist(gen);
    while (w2 == w1) w2 = dist(gen);
    size_t w3 = dist(gen);
    while (w3 == w1 || w3 == w2) w3 = dist(gen);
    size_t w4 = dist(gen);
    while (w4 == w1 || w4 == w2 || w4 == w3) w4 = dist(gen);

    return std::string(SOCCER_WORDS[w1]) + "-" + SOCCER_WORDS[w2] + "-" + SOCCER_WORDS[w3] + "-" + SOCCER_WORDS[w4];
}

} // namespace net
} // namespace iss

#endif // ISS_WORDS_H
