#ifndef ISS_LAUNCHER_H
#define ISS_LAUNCHER_H

#include <filesystem>
#include <string>

#define ISS_APP_NAME "Mabecs International Superstar Soccer 2000 - Widescreen build"

struct SDL_Window;

namespace iss {
    void set_window_icon(SDL_Window* window);

    struct LauncherSettings {
        bool fullscreen = false;
        bool widescreen = true;
    };

    LauncherSettings load_launcher_settings(const std::filesystem::path& data_dir);
    void save_launcher_settings(const std::filesystem::path& data_dir, const LauncherSettings& settings);

    // Shows the launcher window (logo background, ROM selection, a few options). Returns true when the
    // player pressed Play with a valid ROM stored, false when they quit. Must run before recomp::start.
    bool run_launcher(const std::u8string& game_id, const std::filesystem::path& data_dir, LauncherSettings& settings);
}

#endif
