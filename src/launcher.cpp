// Launcher window shown before the game starts: darkened logo background, ROM selection (file dialog via
// xdg-desktop-portal, or drag & drop onto the window), version choice when ROMs of several versions are
// stored, Fullscreen / Widescreen / player Detail options, Play and Quit.
// Rendering uses SDL's 2D renderer in its own window, which is destroyed before RT64 creates the game window.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "SDL2/SDL.h"
#include "nfd.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb/stb_image.h"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

#include "librecomp/game.hpp"

#include "launcher.h"
#include "iss_version.h"
#include "iss_words.h"
#include "generated/launcher_assets.h"

namespace {
    // ---- Text rendering (stb_truetype baked atlas) ----

    struct Font {
        SDL_Texture* texture = nullptr;
        std::array<stbtt_bakedchar, 96> chars{};
        float size = 0;
    };

    Font bake_font(SDL_Renderer* renderer, float pixel_height) {
        Font font;
        font.size = pixel_height;
        constexpr int atlas = 1024;
        std::vector<uint8_t> alpha(atlas * atlas);
        stbtt_BakeFontBitmap(launcher_font_ttf, 0, pixel_height, alpha.data(), atlas, atlas, 32, 96, font.chars.data());
        std::vector<uint32_t> rgba(atlas * atlas);
        for (size_t i = 0; i < rgba.size(); i++) {
            rgba[i] = 0x00FFFFFFu | ((uint32_t)alpha[i] << 24);
        }
        font.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, atlas, atlas);
        SDL_UpdateTexture(font.texture, nullptr, rgba.data(), atlas * 4);
        SDL_SetTextureBlendMode(font.texture, SDL_BLENDMODE_BLEND);
        return font;
    }

    float text_width(const Font& font, const std::string& text) {
        float x = 0, y = 0;
        for (unsigned char c : text) {
            if (c < 32 || c >= 128) continue;
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(font.chars.data(), 1024, 1024, c - 32, &x, &y, &q, 1);
        }
        return x;
    }

    // Draws text with its top-left at (x, y).
    void draw_text(SDL_Renderer* renderer, const Font& font, const std::string& text, float x, float y, SDL_Color color) {
        SDL_SetTextureColorMod(font.texture, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(font.texture, color.a);
        float baseline = y + font.size * 0.8f;
        for (unsigned char c : text) {
            if (c < 32 || c >= 128) continue;
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(font.chars.data(), 1024, 1024, c - 32, &x, &baseline, &q, 1);
            SDL_Rect src{ (int)(q.s0 * 1024), (int)(q.t0 * 1024), (int)((q.s1 - q.s0) * 1024), (int)((q.t1 - q.t0) * 1024) };
            SDL_FRect dst{ q.x0, q.y0, q.x1 - q.x0, q.y1 - q.y0 };
            SDL_RenderCopyF(renderer, font.texture, &src, &dst);
        }
    }

    void draw_text_centered(SDL_Renderer* renderer, const Font& font, const std::string& text, float cx, float y, SDL_Color color) {
        draw_text(renderer, font, text, cx - text_width(font, text) / 2, y, color);
    }

    // ---- Widgets ----

    struct Button {
        std::string label;
        SDL_FRect rect{};
        bool enabled = true;
        SDL_Color custom_bg{ 0, 0, 0, 0 };
    };

    bool inside(const SDL_FRect& r, float x, float y) {
        return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    }

    void fill_rect(SDL_Renderer* renderer, const SDL_FRect& r, SDL_Color c) {
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
        SDL_RenderFillRectF(renderer, &r);
    }

    void outline_rect(SDL_Renderer* renderer, const SDL_FRect& r, SDL_Color c, float thickness) {
        fill_rect(renderer, { r.x, r.y, r.w, thickness }, c);
        fill_rect(renderer, { r.x, r.y + r.h - thickness, r.w, thickness }, c);
        fill_rect(renderer, { r.x, r.y, thickness, r.h }, c);
        fill_rect(renderer, { r.x + r.w - thickness, r.y, thickness, r.h }, c);
    }

    std::string rom_error_text(recomp::RomValidationError err) {
        switch (err) {
            case recomp::RomValidationError::Good: return "ROM accepted.";
            case recomp::RomValidationError::FailedToOpen: return "Could not open that file.";
            case recomp::RomValidationError::NotARom: return "That file is not an N64 ROM.";
            case recomp::RomValidationError::IncorrectRom: return "That is a different game. Select International Superstar Soccer 2000.";
            case recomp::RomValidationError::NotYet: return "That game is not supported.";
            case recomp::RomValidationError::IncorrectVersion: {
                std::string text = "That version of the game is not supported. Supported:";
                for (size_t i = 0; i < iss_num_versions; i++) {
                    text += std::string(i == 0 ? " " : ", ") + iss_versions[i].name;
                }
                return text + ".";
            }
            default: return "Could not use that ROM.";
        }
    }

    // Window icon from the embedded 256x256 PNG (for X11; Wayland takes it from the .desktop file).
    void set_window_icon_impl(SDL_Window* window) {
        int w = 0, h = 0, channels = 0;
        uint8_t* pixels = stbi_load_from_memory(app_icon_png, sizeof(app_icon_png), &w, &h, &channels, 4);
        if (pixels == nullptr) return;
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels, w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
        if (surface != nullptr) {
            SDL_SetWindowIcon(window, surface);
            SDL_FreeSurface(surface);
        }
        stbi_image_free(pixels);
    }

    SDL_Texture* load_logo(SDL_Renderer* renderer, int& w, int& h) {
        int channels = 0;
        uint8_t* pixels = stbi_load_from_memory(launcher_logo_png, sizeof(launcher_logo_png), &w, &h, &channels, 4);
        if (pixels == nullptr) return nullptr;
        SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
        SDL_UpdateTexture(tex, nullptr, pixels, w * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        stbi_image_free(pixels);
        return tex;
    }
}

void iss::set_window_icon(SDL_Window* window) {
    set_window_icon_impl(window);
}

iss::LauncherSettings iss::load_launcher_settings(const std::filesystem::path& data_dir) {
    LauncherSettings s;
    std::ifstream in(data_dir / "launcher.ini");
    std::string key, value;
    while (in >> key >> value) {
        if (key == "fullscreen") s.fullscreen = value == "1";
        else if (key == "widescreen") s.widescreen = value == "1";
        else if (key == "full_detail") s.full_detail = value == "1";
        else if (key == "version") s.version = value;
        else if (key == "room_words") s.room_words = value;
        else if (key == "region" && iss::net::is_room_region(value)) s.region = value;
    }
    return s;
}

void iss::save_launcher_settings(const std::filesystem::path& data_dir, const LauncherSettings& s) {
    std::ofstream out(data_dir / "launcher.ini");
    out << "fullscreen " << (s.fullscreen ? 1 : 0) << "\n";
    out << "widescreen " << (s.widescreen ? 1 : 0) << "\n";
    out << "full_detail " << (s.full_detail ? 1 : 0) << "\n";
    if (!s.version.empty()) out << "version " << s.version << "\n";
    if (!s.room_words.empty()) out << "room_words " << s.room_words << "\n";
    out << "region " << s.region << "\n";
}

const IssVersion* iss::select_rom_any_version(const std::filesystem::path& path, recomp::RomValidationError& err) {
    err = recomp::RomValidationError::IncorrectRom;
    for (size_t i = 0; i < iss_num_versions; i++) {
        recomp::RomValidationError e = recomp::select_rom(path, iss_versions[i].game_id);
        if (e == recomp::RomValidationError::Good) {
            err = e;
            recomp::check_all_stored_roms();
            return &iss_versions[i];
        }
        if (e != recomp::RomValidationError::IncorrectRom) err = e;
    }
    return nullptr;
}

const IssVersion* iss::stored_version(const LauncherSettings& settings) {
    const IssVersion* first = nullptr;
    for (size_t i = 0; i < iss_num_versions; i++) {
        const IssVersion& v = iss_versions[i];
        if (!recomp::is_rom_valid(v.game_id)) continue;
        if (settings.version == v.id) return &v;
        if (first == nullptr) first = &v;
    }
    return first;
}

const IssVersion* iss::run_launcher(const std::filesystem::path& data_dir, LauncherSettings& settings) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "Launcher: SDL init failed: %s\n", SDL_GetError());
        return stored_version(settings);
    }
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    SDL_Window* window = SDL_CreateWindow(ISS_APP_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    set_window_icon_impl(window);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    int logo_w = 0, logo_h = 0;
    SDL_Texture* logo = load_logo(renderer, logo_w, logo_h);
    Font big = bake_font(renderer, 38);
    Font normal = bake_font(renderer, 22);
    Font small = bake_font(renderer, 17);

    bool nfd_ok = NFD_Init() == NFD_OKAY;
    const IssVersion* version = stored_version(settings);
    bool rom_ok = version != nullptr;
    std::string status = rom_ok ? std::string("Loaded: ") + version->name : "Select your ISS 2000 ROM to begin.";
    bool status_error = !rom_ok;

    auto stored_count = []() {
        size_t count = 0;
        for (size_t i = 0; i < iss_num_versions; i++) {
            if (recomp::is_rom_valid(iss_versions[i].game_id)) count++;
        }
        return count;
    };

    auto next_version = [&]() {
        size_t cur = version - iss_versions;
        for (size_t k = 1; k <= iss_num_versions; k++) {
            const IssVersion& v = iss_versions[(cur + k) % iss_num_versions];
            if (recomp::is_rom_valid(v.game_id)) return &v;
        }
        return version;
    };

    enum LauncherScreen {
        SCREEN_MAIN,
        SCREEN_ONLINE_MENU,
        SCREEN_HOST,
        SCREEN_JOIN
    };
    LauncherScreen screen = SCREEN_MAIN;

    enum MainButtons {
        BTN_PLAY,
        BTN_ONLINE,
        BTN_ROM,
        BTN_VERSION,
        BTN_FULLSCREEN,
        BTN_WIDESCREEN,
        BTN_DETAIL,
        BTN_QUIT,
        MAIN_BTN_COUNT
    };

    enum OnlineMenuButtons {
        ONL_BTN_HOST,
        ONL_BTN_JOIN,
        ONL_BTN_BACK,
        ONL_MENU_COUNT
    };

    enum HostButtons {
        HOST_BTN_REGION,
        HOST_BTN_COPY,
        HOST_BTN_REROLL,
        HOST_BTN_BACK,
        HOST_BTN_COUNT
    };

    enum JoinButtons {
        JOIN_BTN_CONNECT,
        JOIN_BTN_PASTE,
        JOIN_BTN_CLEAR,
        JOIN_BTN_BACK,
        JOIN_BTN_COUNT
    };

    std::vector<Button> buttons;
    int focus = rom_ok ? BTN_PLAY : BTN_ROM;
    SDL_GameController* pad = nullptr;

    std::string join_input = settings.room_words;
    bool join_connecting = false;
    std::chrono::steady_clock::time_point join_start_time{};
    std::chrono::steady_clock::time_point launch_trigger_time{};
    bool launch_pending = false;

    auto start_hosting = [&]() {
        settings.room_words.clear();
        settings.net_mode = iss::net::NET_MODE_HOST;

        iss::net::NetConfig cfg;
        cfg.mode = iss::net::NET_MODE_HOST;
        cfg.region = settings.region;
        iss::net::start_session(cfg, version->id);

        screen = SCREEN_HOST;
        focus = HOST_BTN_COPY;
        status = "Connecting to STUN server & waiting for opponent...";
        status_error = false;
        launch_pending = false;
    };

    auto start_joining = [&]() {
        screen = SCREEN_JOIN;
        focus = JOIN_BTN_CONNECT;
        join_connecting = false;
        launch_pending = false;
        status = "Paste the Host code or enter an IP address / hostname.";
        status_error = false;
        SDL_StartTextInput();
    };

    auto do_join_connect = [&]() {
        std::string clean = iss::net::normalize_words(join_input);
        if (clean.empty()) {
            status = "Enter a region-tagged Host code, four soccer words, or an IP/hostname.";
            status_error = true;
            return;
        }
        settings.room_words = join_input;
        settings.net_mode = iss::net::NET_MODE_JOIN;

        iss::net::NetConfig cfg;
        cfg.mode = iss::net::NET_MODE_JOIN;
        cfg.room_words = join_input;
        if (!iss::net::start_session(cfg, version->id)) {
            status = iss::net::get_status_info();
            status_error = true;
            return;
        }

        join_connecting = true;
        join_start_time = std::chrono::steady_clock::now();
        status = "Connecting via STUN traversal & searching local network...";
        status_error = false;
    };

    auto use_rom = [&](const std::filesystem::path& path) {
        recomp::RomValidationError err;
        if (const IssVersion* v = select_rom_any_version(path, err)) {
            version = v;
            rom_ok = true;
        }
        status = err == recomp::RomValidationError::Good ? std::string("ROM accepted: ") + version->name + ". Ready to play!" : rom_error_text(err);
        status_error = err != recomp::RomValidationError::Good;
        if (rom_ok) focus = BTN_PLAY;
    };

    auto pick_rom = [&]() {
        if (!nfd_ok) {
            status = "No file dialog available: drag and drop the ROM file onto this window.";
            status_error = true;
            return;
        }
        nfdu8filteritem_t filters[] = { { "N64 ROM", "z64,n64,v64" } };
        nfdu8char_t* out = nullptr;
        nfdresult_t result = NFD_OpenDialogU8(&out, filters, 1, nullptr);
        if (result == NFD_OKAY) {
            use_rom(std::filesystem::path(reinterpret_cast<const char8_t*>(out)));
            NFD_FreePathU8(out);
        }
        else if (result == NFD_ERROR) {
            status = std::string("File dialog failed: ") + NFD_GetError();
            status_error = true;
        }
    };

    bool play = false;
    bool running = true;

    auto activate = [&](int index) {
        if (screen == SCREEN_MAIN) {
            switch (index) {
                case BTN_PLAY:
                    if (rom_ok) {
                        settings.net_mode = iss::net::NET_MODE_OFFLINE;
                        play = true;
                        running = false;
                    }
                    break;
                case BTN_ONLINE:
                    if (rom_ok) {
                        screen = SCREEN_ONLINE_MENU;
                        focus = ONL_BTN_HOST;
                        status = "Choose Host to get soccer words, or Join to connect.";
                        status_error = false;
                    } else {
                        status = "Please select a ROM first.";
                        status_error = true;
                    }
                    break;
                case BTN_ROM: pick_rom(); break;
                case BTN_VERSION: if (rom_ok) version = next_version(); break;
                case BTN_FULLSCREEN: settings.fullscreen = !settings.fullscreen; break;
                case BTN_WIDESCREEN: settings.widescreen = !settings.widescreen; break;
                case BTN_DETAIL: settings.full_detail = !settings.full_detail; break;
                case BTN_QUIT: running = false; break;
            }
        }
        else if (screen == SCREEN_ONLINE_MENU) {
            switch (index) {
                case ONL_BTN_HOST:
                    start_hosting();
                    break;
                case ONL_BTN_JOIN:
                    start_joining();
                    break;
                case ONL_BTN_BACK:
                    screen = SCREEN_MAIN;
                    focus = BTN_ONLINE;
                    status = "Ready to play.";
                    break;
            }
        }
        else if (screen == SCREEN_HOST) {
            switch (index) {
                case HOST_BTN_REGION: {
                    static const std::array<const char*, 6> regions = {"EU", "NA", "SA", "ASIA", "OC", "AF"};
                    auto it = std::find_if(regions.begin(), regions.end(), [&](const char* region) {
                        return settings.region == region;
                    });
                    size_t next = it == regions.end() ? 0 : ((size_t)(it - regions.begin()) + 1) % regions.size();
                    settings.region = regions[next];
                    start_hosting();
                    break;
                }
                case HOST_BTN_COPY:
                    SDL_SetClipboardText(settings.room_words.c_str());
                    status = "Soccer pass phrase copied to clipboard! Share it with your opponent.";
                    break;
                case HOST_BTN_REROLL:
                    start_hosting();
                    break;
                case HOST_BTN_BACK:
                    iss::net::shutdown();
                    screen = SCREEN_ONLINE_MENU;
                    focus = ONL_BTN_HOST;
                    status = "Choose Host to get soccer words, or Join to connect.";
                    break;
            }
        }
        else if (screen == SCREEN_JOIN) {
            switch (index) {
                case JOIN_BTN_CONNECT:
                    do_join_connect();
                    break;
                case JOIN_BTN_PASTE:
                    if (SDL_HasClipboardText()) {
                        char* clip = SDL_GetClipboardText();
                        if (clip) {
                            for (char c : std::string(clip)) {
                                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == ' ' || c == '_' || c == '.' || c == ':') {
                                    join_input += (char)std::toupper((unsigned char)c);
                                }
                            }
                            SDL_free(clip);
                        }
                    }
                    break;
                case JOIN_BTN_CLEAR:
                    join_input.clear();
                    break;
                case JOIN_BTN_BACK:
                    iss::net::shutdown();
                    SDL_StopTextInput();
                    screen = SCREEN_ONLINE_MENU;
                    focus = ONL_BTN_JOIN;
                    status = "Choose Host to get soccer words, or Join to connect.";
                    break;
            }
        }
    };

    auto move_focus = [&](int dir) {
        int count = (int)buttons.size();
        if (count == 0) return;
        for (int i = 0; i < count; i++) {
            focus = (focus + dir + count) % count;
            if (buttons[focus].enabled) break;
        }
    };

    while (running) {
        // ---- Handle Netplay Connection Handshake in Host/Join Screens ----
        if (screen == SCREEN_HOST && iss::net::is_active()) {
            iss::net::NetState st = iss::net::update_session();
            std::string active_code = iss::net::get_active_room_words();
            if (!active_code.empty()) {
                settings.room_words = active_code;
            }
            if (st == iss::net::STATE_CONNECTED) {
                status = "Opponent connected! Launching 1vs1 match...";
                status_error = false;
                if (!launch_pending) {
                    launch_pending = true;
                    launch_trigger_time = std::chrono::steady_clock::now();
                } else if (std::chrono::steady_clock::now() - launch_trigger_time > std::chrono::milliseconds(500)) {
                    play = true;
                    running = false;
                }
            } else {
                std::string st_info = iss::net::get_status_info();
                if (!st_info.empty()) {
                    status = st_info;
                }
            }
        }
        else if (screen == SCREEN_JOIN && join_connecting) {
            iss::net::NetState st = iss::net::update_session();
            if (st == iss::net::STATE_CONNECTED) {
                status = "Connected to Host! Launching 1vs1 match...";
                status_error = false;
                if (!launch_pending) {
                    launch_pending = true;
                    launch_trigger_time = std::chrono::steady_clock::now();
                } else if (std::chrono::steady_clock::now() - launch_trigger_time > std::chrono::milliseconds(500)) {
                    play = true;
                    running = false;
                }
            } else if (st == iss::net::STATE_ERROR || std::chrono::steady_clock::now() - join_start_time > std::chrono::seconds(25)) {
                join_connecting = false;
                std::string err = iss::net::get_status_info();
                iss::net::shutdown();
                status = !err.empty() ? err : "Could not connect to room. Check soccer words and ensure Host is waiting.";
                status_error = true;
            }
        }

        int out_w = 0, out_h = 0, win_w = 0, win_h = 0;
        SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
        SDL_GetWindowSize(window, &win_w, &win_h);
        float dpi = win_w > 0 ? (float)out_w / win_w : 1.0f;
        float scale = std::min(out_w / 1280.0f, out_h / 720.0f);
        SDL_RenderSetScale(renderer, scale, scale);
        float vw = out_w / scale, vh = out_h / scale;

        // ---- Layout buttons per screen ----
        if (screen == SCREEN_MAIN) {
            buttons.resize(MAIN_BTN_COUNT);
            buttons[BTN_PLAY].label = "Play";
            buttons[BTN_PLAY].enabled = rom_ok;
            buttons[BTN_PLAY].custom_bg = SDL_Color{ 25, 65, 175, 240 }; // Bold Blue

            buttons[BTN_ONLINE].label = "Online";
            buttons[BTN_ONLINE].enabled = rom_ok;
            buttons[BTN_ONLINE].custom_bg = SDL_Color{ 22, 135, 65, 245 }; // Emerald Green accent

            buttons[BTN_ROM].label = rom_ok ? "Change ROM..." : "Select ROM...";
            buttons[BTN_ROM].enabled = true;
            buttons[BTN_VERSION].label = rom_ok ? version->short_name : "Version";
            buttons[BTN_VERSION].enabled = rom_ok && stored_count() > 1;
            buttons[BTN_FULLSCREEN].label = std::string("Fullscreen: ") + (settings.fullscreen ? "On" : "Off");
            buttons[BTN_WIDESCREEN].label = std::string("Widescreen: ") + (settings.widescreen ? "On" : "Off");
            buttons[BTN_DETAIL].label = std::string("Detail: ") + (settings.full_detail ? "Full" : "N64");
            buttons[BTN_QUIT].label = "Quit";

            const float bh = 52, gap = 11;
            std::array<float, MAIN_BTN_COUNT> widths;
            float total = (MAIN_BTN_COUNT - 1) * gap;
            for (int i = 0; i < MAIN_BTN_COUNT; i++) {
                widths[i] = std::max(115.0f, text_width(normal, buttons[i].label) + 28);
                total += widths[i];
            }
            float x = vw / 2 - total / 2;
            for (int i = 0; i < MAIN_BTN_COUNT; i++) {
                buttons[i].rect = { x, vh - 170, widths[i], bh };
                x += widths[i] + gap;
            }
        }
        else if (screen == SCREEN_ONLINE_MENU) {
            buttons.resize(ONL_MENU_COUNT);
            buttons[ONL_BTN_HOST].label = "Host 1vs1 Match (Get Soccer Words)";
            buttons[ONL_BTN_HOST].custom_bg = SDL_Color{ 22, 135, 65, 245 }; // Green

            buttons[ONL_BTN_JOIN].label = "Join 1vs1 Match (Enter Soccer Words)";
            buttons[ONL_BTN_JOIN].custom_bg = SDL_Color{ 25, 95, 195, 245 }; // Blue

            buttons[ONL_BTN_BACK].label = "Back to Main Menu";
            buttons[ONL_BTN_BACK].custom_bg = SDL_Color{ 75, 80, 95, 220 }; // Grey

            const float bh = 52, gap = 18;
            std::array<float, ONL_MENU_COUNT> widths;
            float total = (ONL_MENU_COUNT - 1) * gap;
            for (int i = 0; i < ONL_MENU_COUNT; i++) {
                widths[i] = std::max(220.0f, text_width(normal, buttons[i].label) + 40);
                total += widths[i];
            }
            float x = vw / 2 - total / 2;
            for (int i = 0; i < ONL_MENU_COUNT; i++) {
                buttons[i].rect = { x, vh - 165, widths[i], bh };
                x += widths[i] + gap;
            }
        }
        else if (screen == SCREEN_HOST) {
            buttons.resize(HOST_BTN_COUNT);
            buttons[HOST_BTN_REGION].label = "Region: " + settings.region + " (Change)";
            buttons[HOST_BTN_REGION].custom_bg = SDL_Color{ 65, 75, 95, 220 };
            buttons[HOST_BTN_COPY].label = "Copy Pass Phrase";
            buttons[HOST_BTN_COPY].enabled = iss::net::is_traversal_ready();
            buttons[HOST_BTN_COPY].custom_bg = SDL_Color{ 25, 95, 195, 240 };
            buttons[HOST_BTN_REROLL].label = "New Pass Phrase";
            buttons[HOST_BTN_REROLL].custom_bg = SDL_Color{ 65, 75, 95, 220 };
            buttons[HOST_BTN_BACK].label = "Back to Online Menu";
            buttons[HOST_BTN_BACK].custom_bg = SDL_Color{ 140, 45, 45, 220 };

            const float bh = 46, gap = 16;
            float total = (HOST_BTN_COUNT - 1) * gap;
            std::array<float, HOST_BTN_COUNT> widths;
            for (int i = 0; i < HOST_BTN_COUNT; i++) {
                widths[i] = std::max(160.0f, text_width(normal, buttons[i].label) + 36);
                total += widths[i];
            }
            float x = vw / 2 - total / 2;
            for (int i = 0; i < HOST_BTN_COUNT; i++) {
                buttons[i].rect = { x, vh - 85, widths[i], bh };
                x += widths[i] + gap;
            }
        }
        else if (screen == SCREEN_JOIN) {
            buttons.resize(JOIN_BTN_COUNT);
            buttons[JOIN_BTN_CONNECT].label = "Connect & Play";
            buttons[JOIN_BTN_CONNECT].custom_bg = SDL_Color{ 22, 135, 65, 245 };
            buttons[JOIN_BTN_PASTE].label = "Paste Clipboard";
            buttons[JOIN_BTN_PASTE].custom_bg = SDL_Color{ 25, 95, 195, 240 };
            buttons[JOIN_BTN_CLEAR].label = "Clear";
            buttons[JOIN_BTN_CLEAR].custom_bg = SDL_Color{ 65, 75, 95, 220 };
            buttons[JOIN_BTN_BACK].label = "Back to Online Menu";
            buttons[JOIN_BTN_BACK].custom_bg = SDL_Color{ 140, 45, 45, 220 };

            const float bh = 46, gap = 14;
            float total = (JOIN_BTN_COUNT - 1) * gap;
            std::array<float, JOIN_BTN_COUNT> widths;
            for (int i = 0; i < JOIN_BTN_COUNT; i++) {
                widths[i] = std::max(135.0f, text_width(normal, buttons[i].label) + 32);
                total += widths[i];
            }
            float x = vw / 2 - total / 2;
            for (int i = 0; i < JOIN_BTN_COUNT; i++) {
                buttons[i].rect = { x, vh - 85, widths[i], bh };
                x += widths[i] + gap;
            }
        }

        // ---- Events ----
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_DROPFILE:
                    use_rom(std::filesystem::path(reinterpret_cast<const char8_t*>(e.drop.file)));
                    SDL_free(e.drop.file);
                    break;
                case SDL_TEXTINPUT:
                    if (screen == SCREEN_JOIN && !join_connecting) {
                        for (char c : std::string(e.text.text)) {
                            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == ' ' || c == '_' || c == '.' || c == ':') {
                                join_input += (char)std::toupper((unsigned char)c);
                            }
                        }
                    }
                    break;
                case SDL_MOUSEMOTION:
                    for (size_t i = 0; i < buttons.size(); i++) {
                        if (buttons[i].enabled && inside(buttons[i].rect, e.motion.x * dpi / scale, e.motion.y * dpi / scale)) {
                            focus = (int)i;
                        }
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        for (size_t i = 0; i < buttons.size(); i++) {
                            if (buttons[i].enabled && inside(buttons[i].rect, e.button.x * dpi / scale, e.button.y * dpi / scale)) {
                                activate((int)i);
                            }
                        }
                    }
                    break;
                case SDL_KEYDOWN:
                    if (screen == SCREEN_JOIN) {
                        if (e.key.keysym.sym == SDLK_BACKSPACE && !join_input.empty()) {
                            join_input.pop_back();
                        }
                        else if (e.key.keysym.sym == SDLK_v && (SDL_GetModState() & KMOD_CTRL)) {
                            if (SDL_HasClipboardText()) {
                                char* clip = SDL_GetClipboardText();
                                if (clip) {
                                    for (char c : std::string(clip)) {
                                        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == ' ' || c == '_' || c == '.' || c == ':') {
                                            join_input += (char)std::toupper((unsigned char)c);
                                        }
                                    }
                                    SDL_free(clip);
                                }
                            }
                        }
                        else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                            do_join_connect();
                        }
                        else if (e.key.keysym.sym == SDLK_ESCAPE) {
                            activate((int)JOIN_BTN_BACK);
                        }
                    } else if (screen == SCREEN_HOST) {
                        if (e.key.keysym.sym == SDLK_ESCAPE) {
                            activate((int)HOST_BTN_BACK);
                        } else if (e.key.keysym.sym == SDLK_c && (SDL_GetModState() & KMOD_CTRL)) {
                            activate((int)HOST_BTN_COPY);
                        }
                    } else if (screen == SCREEN_ONLINE_MENU) {
                        if (e.key.keysym.sym == SDLK_ESCAPE) {
                            activate((int)ONL_BTN_BACK);
                        } else {
                            switch (e.key.keysym.sym) {
                                case SDLK_LEFT: case SDLK_UP: move_focus(-1); break;
                                case SDLK_RIGHT: case SDLK_DOWN: case SDLK_TAB: move_focus(1); break;
                                case SDLK_RETURN: case SDLK_SPACE: case SDLK_KP_ENTER: activate(focus); break;
                            }
                        }
                    } else {
                        switch (e.key.keysym.sym) {
                            case SDLK_LEFT: case SDLK_UP: move_focus(-1); break;
                            case SDLK_RIGHT: case SDLK_DOWN: case SDLK_TAB: move_focus(1); break;
                            case SDLK_RETURN: case SDLK_SPACE: case SDLK_KP_ENTER: activate(focus); break;
                            case SDLK_ESCAPE: running = false; break;
                        }
                    }
                    break;
                case SDL_CONTROLLERDEVICEADDED:
                    if (pad == nullptr) pad = SDL_GameControllerOpen(e.cdevice.which);
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    switch (e.cbutton.button) {
                        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: case SDL_CONTROLLER_BUTTON_DPAD_UP: move_focus(-1); break;
                        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: case SDL_CONTROLLER_BUTTON_DPAD_DOWN: move_focus(1); break;
                        case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: activate(focus); break;
                        case SDL_CONTROLLER_BUTTON_B:
                            if (screen == SCREEN_ONLINE_MENU) activate((int)ONL_BTN_BACK);
                            else if (screen == SCREEN_HOST) activate((int)HOST_BTN_BACK);
                            else if (screen == SCREEN_JOIN) activate((int)JOIN_BTN_BACK);
                            break;
                    }
                    break;
            }
        }
        if (!running) break;

        // ---- Render Background & Logo ----
        SDL_SetRenderDrawColor(renderer, 6, 8, 16, 255);
        SDL_RenderClear(renderer);
        if (logo != nullptr) {
            float lw = vw * 0.92f, lh = lw * logo_h / logo_w;
            if (lh > vh * 0.72f) { lh = vh * 0.72f; lw = lh * logo_w / logo_h; }
            SDL_FRect dst{ (vw - lw) / 2, vh * 0.35f - lh / 2, lw, lh };
            SDL_SetTextureColorMod(logo, 95, 95, 100);
            SDL_RenderCopyF(renderer, logo, nullptr, &dst);
        }

        // Bottom panel backdrop (spacious 330px height, comfortable room for all elements)
        fill_rect(renderer, { 0, vh - 330, vw, 330 }, { 0, 0, 0, 195 });

        // Header
        draw_text_centered(renderer, big, "Mabecs International Superstar Soccer 2000", vw / 2, vh - 315, { 255, 255, 255, 255 });

        if (screen == SCREEN_MAIN) {
            std::string subtitle = "Widescreen build  -  native PC recompilation of the N64 version";
            if (rom_ok) subtitle += std::string(": ") + version->name;
            draw_text_centered(renderer, small, subtitle, vw / 2, vh - 265, { 255, 205, 80, 255 });

            for (size_t i = 0; i < buttons.size(); i++) {
                const Button& b = buttons[i];
                bool focused = (int)i == focus && b.enabled;
                SDL_Color bg = !b.enabled ? SDL_Color{ 40, 40, 48, 200 }
                    : (b.custom_bg.a > 0 ? b.custom_bg : SDL_Color{ 25, 55, 140, 230 });
                if (focused) {
                    bg = SDL_Color{ 235, 130, 20, 245 }; // Highlighted orange
                }
                fill_rect(renderer, b.rect, bg);
                outline_rect(renderer, b.rect, focused ? SDL_Color{ 255, 230, 90, 255 } : SDL_Color{ 130, 160, 255, 140 }, focused ? 3 : 1.5f);
                SDL_Color fg = b.enabled ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 130, 130, 140, 255 };
                draw_text_centered(renderer, normal, b.label, b.rect.x + b.rect.w / 2, b.rect.y + (b.rect.h - normal.size) / 2 + 1, fg);
            }

            draw_text_centered(renderer, small, status, vw / 2, vh - 90, status_error ? SDL_Color{ 255, 110, 110, 255 } : SDL_Color{ 140, 235, 140, 255 });
            draw_text_centered(renderer, small, "Tip: Click 'Online' to play 1vs1 P2P with friends worldwide by sharing unique soccer pass phrases.", vw / 2, vh - 45, { 160, 170, 190, 255 });
        }
        else if (screen == SCREEN_ONLINE_MENU) {
            draw_text_centered(renderer, normal, "1vs1 ONLINE MULTIPLAYER (P2P)", vw / 2, vh - 270, { 50, 230, 120, 255 });
            draw_text_centered(renderer, small, "Play online with friends worldwide using unique soccer pass phrases (no port forwarding needed):", vw / 2, vh - 235, { 220, 220, 230, 255 });

            for (size_t i = 0; i < buttons.size(); i++) {
                const Button& b = buttons[i];
                bool focused = (int)i == focus && b.enabled;
                SDL_Color bg = focused ? SDL_Color{ 235, 130, 20, 245 } : b.custom_bg;
                fill_rect(renderer, b.rect, bg);
                outline_rect(renderer, b.rect, focused ? SDL_Color{ 255, 230, 90, 255 } : SDL_Color{ 140, 170, 255, 160 }, focused ? 3 : 1.5f);
                draw_text_centered(renderer, normal, b.label, b.rect.x + b.rect.w / 2, b.rect.y + (b.rect.h - normal.size) / 2 + 1, { 255, 255, 255, 255 });
            }

            draw_text_centered(renderer, small, status, vw / 2, vh - 80, status_error ? SDL_Color{ 255, 110, 110, 255 } : SDL_Color{ 140, 230, 160, 255 });
            draw_text_centered(renderer, small, "Online Traversal: stun.dolphin-emu.org  |  LAN Broadcast: Port 9099", vw / 2, vh - 28, { 130, 135, 150, 255 });
        }
        else if (screen == SCREEN_HOST) {
            draw_text_centered(renderer, normal, "HOST 1vs1 ONLINE MATCH", vw / 2, vh - 275, { 50, 230, 120, 255 });
            draw_text_centered(renderer, small, "Share this region-tagged pass phrase with your friend:", vw / 2, vh - 245, { 220, 220, 230, 255 });

            // 4-Word Soccer Phrase Display (no yellow frame, clean centered display)
            if (iss::net::is_traversal_ready() && !settings.room_words.empty()) {
                if (text_width(big, settings.room_words) < vw * 0.90f) {
                    draw_text_centered(renderer, big, settings.room_words, vw / 2, vh - 210, { 255, 235, 75, 255 });
                } else {
                    draw_text_centered(renderer, normal, settings.room_words, vw / 2, vh - 204, { 255, 235, 75, 255 });
                }
            } else {
                draw_text_centered(renderer, normal, "REQUESTING SOCCER WORDS FROM STUN...", vw / 2, vh - 204, { 180, 200, 220, 255 });
            }

            std::string anim_status = status;
            if (status.find("Waiting") != std::string::npos || status.find("Connecting") != std::string::npos) {
                int dots = (SDL_GetTicks() / 400) % 4;
                for (int d = 0; d < dots; d++) anim_status += ".";
            }
            draw_text_centered(renderer, normal, anim_status, vw / 2, vh - 138, { 130, 230, 150, 255 });

            for (size_t i = 0; i < buttons.size(); i++) {
                const Button& b = buttons[i];
                bool focused = (int)i == focus && b.enabled;
                SDL_Color bg = focused ? SDL_Color{ 235, 130, 20, 245 } : b.custom_bg;
                fill_rect(renderer, b.rect, bg);
                outline_rect(renderer, b.rect, focused ? SDL_Color{ 255, 230, 90, 255 } : SDL_Color{ 140, 170, 255, 160 }, focused ? 3 : 1.5f);
                draw_text_centered(renderer, normal, b.label, b.rect.x + b.rect.w / 2, b.rect.y + (b.rect.h - normal.size) / 2 + 1, { 255, 255, 255, 255 });
            }

            draw_text_centered(renderer, small, "STUN Traversal: stun.dolphin-emu.org  |  LAN Broadcast: Active", vw / 2, vh - 22, { 130, 135, 150, 255 });
        }
        else if (screen == SCREEN_JOIN) {
            draw_text_centered(renderer, normal, "JOIN 1vs1 ONLINE MATCH", vw / 2, vh - 275, { 70, 180, 255, 255 });
            draw_text_centered(renderer, small, "Enter the Host code (e.g. EU-STRIKER-VOLLEY-HEADER-GOAL) or direct IP/hostname:", vw / 2, vh - 245, { 220, 220, 230, 255 });

            // Text Input Box
            SDL_FRect input_box{ vw / 2 - 360, vh - 215, 720, 52 };
            fill_rect(renderer, input_box, { 15, 20, 32, 230 });
            outline_rect(renderer, input_box, join_connecting ? SDL_Color{ 255, 200, 50, 255 } : SDL_Color{ 80, 180, 255, 255 }, 2.5f);

            std::string disp_text = join_input;
            if (disp_text.empty()) {
                draw_text_centered(renderer, normal, "type or paste Host code, IP, or hostname...", vw / 2, input_box.y + 13, { 120, 130, 145, 255 });
            } else {
                if ((SDL_GetTicks() / 450) % 2 == 0 && !join_connecting) {
                    disp_text += "|";
                }
                if (text_width(big, disp_text) < input_box.w - 24) {
                    draw_text_centered(renderer, big, disp_text, vw / 2, input_box.y + 6, { 255, 255, 255, 255 });
                } else {
                    draw_text_centered(renderer, normal, disp_text, vw / 2, input_box.y + 13, { 255, 255, 255, 255 });
                }
            }

            draw_text_centered(renderer, normal, status, vw / 2, vh - 138, status_error ? SDL_Color{ 255, 110, 110, 255 } : SDL_Color{ 140, 230, 160, 255 });

            for (size_t i = 0; i < buttons.size(); i++) {
                const Button& b = buttons[i];
                bool focused = (int)i == focus && b.enabled;
                SDL_Color bg = focused ? SDL_Color{ 235, 130, 20, 245 } : b.custom_bg;
                fill_rect(renderer, b.rect, bg);
                outline_rect(renderer, b.rect, focused ? SDL_Color{ 255, 230, 90, 255 } : SDL_Color{ 140, 170, 255, 160 }, focused ? 3 : 1.5f);
                draw_text_centered(renderer, normal, b.label, b.rect.x + b.rect.w / 2, b.rect.y + (b.rect.h - normal.size) / 2 + 1, { 255, 255, 255, 255 });
            }

            draw_text_centered(renderer, small, "STUN Traversal: stun.dolphin-emu.org  |  Direct IP & LAN: Supported", vw / 2, vh - 22, { 130, 135, 150, 255 });
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(10);
    }

    if (pad != nullptr) SDL_GameControllerClose(pad);
    if (nfd_ok) NFD_Quit();
    SDL_DestroyTexture(big.texture);
    SDL_DestroyTexture(normal.texture);
    SDL_DestroyTexture(small.texture);
    if (logo != nullptr) SDL_DestroyTexture(logo);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (play) settings.version = version->id;
    save_launcher_settings(data_dir, settings);
    return play ? version : nullptr;
}
