#ifndef ISS_LAUNCHER_H
#define ISS_LAUNCHER_H

#include <filesystem>
#include <string>

#include "librecomp/game.hpp"

#define ISS_APP_NAME "Mabecs International Superstar Soccer 2000 - Widescreen build"

struct SDL_Window;
struct IssVersion;

namespace iss {
    void set_window_icon(SDL_Window* window);

    struct LauncherSettings {
        bool fullscreen = false;
        bool widescreen = true;
        std::string version;  // id of the version last played (src/iss_version.cpp)
    };

    LauncherSettings load_launcher_settings(const std::filesystem::path& data_dir);
    void save_launcher_settings(const std::filesystem::path& data_dir, const LauncherSettings& settings);

    // Validates a ROM against every supported version and stores it for the one it matches.
    // Returns that version, or nullptr with the most specific error in err.
    const IssVersion* select_rom_any_version(const std::filesystem::path& path, recomp::RomValidationError& err);

    // The version to play without the launcher: settings.version if its ROM is stored, else the first stored one.
    const IssVersion* stored_version(const LauncherSettings& settings);

    // Shows the launcher window (logo background, ROM selection, version choice when several ROMs are stored,
    // a few options). Returns the version to play when the player pressed Play, nullptr when they quit.
    // Must run before recomp::start.
    const IssVersion* run_launcher(const std::filesystem::path& data_dir, LauncherSettings& settings);
}

#endif
