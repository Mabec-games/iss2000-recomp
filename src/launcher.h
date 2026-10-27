#ifndef ISS_LAUNCHER_H
#define ISS_LAUNCHER_H

#include <filesystem>
#include <string>

#include "librecomp/game.hpp"
#include "iss_netplay.h"

#define ISS_APP_NAME "Mabecs International Superstar Soccer 2000 - Widescreen build"

struct SDL_Window;
struct IssVersion;

namespace iss {
    void set_window_icon(SDL_Window* window);

    struct LauncherSettings {
        bool fullscreen = false;
        bool widescreen = true;
        bool full_detail = true;  // players always use their best model (no distance LOD)
        std::string version;  // id of the version last played (src/iss_version.cpp)

        // Netplay settings
        iss::net::NetMode net_mode = iss::net::NET_MODE_OFFLINE;
        std::string room_words;
        std::string region = "EU";
        std::string broker_addr = "127.0.0.1:9099";
    };

    LauncherSettings load_launcher_settings(const std::filesystem::path& data_dir);
    void save_launcher_settings(const std::filesystem::path& data_dir, const LauncherSettings& settings);

    // Validates a ROM against every supported version and stores it for the one it matches.
    // Returns that version, or nullptr with the most specific error in err.
    const IssVersion* select_rom_any_version(const std::filesystem::path& path, recomp::RomValidationError& err);

    // The version to play without the launcher: settings.version if its ROM is stored, else the first stored one.
    const IssVersion* stored_version(const LauncherSettings& settings);

    // Shows the launcher window (logo background, ROM selection, version choice when several ROMs are stored,
    // options, and P2P Online matchmaking). Returns the version to play, nullptr when they quit.
    const IssVersion* run_launcher(const std::filesystem::path& data_dir, LauncherSettings& settings);
}

#endif
