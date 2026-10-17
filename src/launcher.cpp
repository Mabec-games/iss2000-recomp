// Launcher window shown before the game starts: darkened logo background, ROM selection (file dialog via
// xdg-desktop-portal, or drag & drop onto the window), Fullscreen / Widescreen options, Play and Quit.
// Rendering uses SDL's 2D renderer in its own window, which is destroyed before RT64 creates the game window.
#include <array>
#include <cstdio>
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
            case recomp::RomValidationError::IncorrectVersion: return "Wrong version: the USA (Rev 1) ROM is required.";
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
    }
    return s;
}

void iss::save_launcher_settings(const std::filesystem::path& data_dir, const LauncherSettings& s) {
    std::ofstream out(data_dir / "launcher.ini");
    out << "fullscreen " << (s.fullscreen ? 1 : 0) << "\n";
    out << "widescreen " << (s.widescreen ? 1 : 0) << "\n";
}

bool iss::run_launcher(const std::u8string& game_id, const std::filesystem::path& data_dir, LauncherSettings& settings) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "Launcher: SDL init failed: %s\n", SDL_GetError());
        return recomp::is_rom_valid(game_id);
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
    Font big = bake_font(renderer, 40);
    Font normal = bake_font(renderer, 24);
    Font small = bake_font(renderer, 18);

    bool nfd_ok = NFD_Init() == NFD_OKAY;
    bool rom_ok = recomp::is_rom_valid(game_id);
    std::string status = rom_ok ? "ROM found. Press Play to start." : "Select your International Superstar Soccer 2000 (USA) ROM to begin.";
    bool status_error = false;

    enum { BTN_ROM, BTN_PLAY, BTN_FULLSCREEN, BTN_WIDESCREEN, BTN_QUIT, BTN_COUNT };
    std::array<Button, BTN_COUNT> buttons;
    int focus = rom_ok ? BTN_PLAY : BTN_ROM;
    SDL_GameController* pad = nullptr;

    auto use_rom = [&](const std::filesystem::path& path) {
        recomp::RomValidationError err = recomp::select_rom(path, game_id);
        if (err == recomp::RomValidationError::Good) {
            recomp::check_all_stored_roms();
            rom_ok = recomp::is_rom_valid(game_id);
        }
        status = rom_ok && err == recomp::RomValidationError::Good ? "ROM accepted. Press Play to start." : rom_error_text(err);
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
            status = std::string("File dialog failed (") + NFD_GetError() + "). Drag and drop the ROM onto this window instead.";
            status_error = true;
        }
    };

    bool play = false;
    bool running = true;
    auto activate = [&](int index) {
        switch (index) {
            case BTN_ROM: pick_rom(); break;
            case BTN_PLAY: if (rom_ok) { play = true; running = false; } break;
            case BTN_FULLSCREEN: settings.fullscreen = !settings.fullscreen; break;
            case BTN_WIDESCREEN: settings.widescreen = !settings.widescreen; break;
            case BTN_QUIT: running = false; break;
        }
    };
    auto move_focus = [&](int dir) {
        for (int i = 0; i < BTN_COUNT; i++) {
            focus = (focus + dir + BTN_COUNT) % BTN_COUNT;
            if (buttons[focus].enabled) break;
        }
    };

    while (running) {
        int out_w = 0, out_h = 0, win_w = 0, win_h = 0;
        SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
        SDL_GetWindowSize(window, &win_w, &win_h);
        float dpi = win_w > 0 ? (float)out_w / win_w : 1.0f;
        // Lay out in a 1280x720 virtual space, scaled to the window.
        float scale = std::min(out_w / 1280.0f, out_h / 720.0f);
        SDL_RenderSetScale(renderer, scale, scale);
        float vw = out_w / scale, vh = out_h / scale;

        buttons[BTN_ROM].label = rom_ok ? "Change ROM..." : "Select ROM...";
        buttons[BTN_PLAY].label = "Play";
        buttons[BTN_PLAY].enabled = rom_ok;
        buttons[BTN_FULLSCREEN].label = std::string("Fullscreen: ") + (settings.fullscreen ? "On" : "Off");
        buttons[BTN_WIDESCREEN].label = std::string("Widescreen: ") + (settings.widescreen ? "On" : "Off");
        buttons[BTN_QUIT].label = "Quit";
        const float bw = 210, bh = 56, gap = 18;
        float total = BTN_COUNT * bw + (BTN_COUNT - 1) * gap;
        for (int i = 0; i < BTN_COUNT; i++) {
            buttons[i].rect = { vw / 2 - total / 2 + i * (bw + gap), vh - 150, bw, bh };
        }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_DROPFILE:
                    use_rom(std::filesystem::path(reinterpret_cast<const char8_t*>(e.drop.file)));
                    SDL_free(e.drop.file);
                    break;
                case SDL_MOUSEMOTION:
                    for (int i = 0; i < BTN_COUNT; i++) {
                        if (buttons[i].enabled && inside(buttons[i].rect, e.motion.x * dpi / scale, e.motion.y * dpi / scale)) focus = i;
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        for (int i = 0; i < BTN_COUNT; i++) {
                            if (buttons[i].enabled && inside(buttons[i].rect, e.button.x * dpi / scale, e.button.y * dpi / scale)) activate(i);
                        }
                    }
                    break;
                case SDL_KEYDOWN:
                    switch (e.key.keysym.sym) {
                        case SDLK_LEFT: case SDLK_UP: move_focus(-1); break;
                        case SDLK_RIGHT: case SDLK_DOWN: case SDLK_TAB: move_focus(1); break;
                        case SDLK_RETURN: case SDLK_SPACE: case SDLK_KP_ENTER: activate(focus); break;
                        case SDLK_ESCAPE: running = false; break;
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
                    }
                    break;
            }
        }
        if (!running) break;

        // Background: black with the logo scaled to cover most of the window, darkened.
        SDL_SetRenderDrawColor(renderer, 6, 8, 16, 255);
        SDL_RenderClear(renderer);
        if (logo != nullptr) {
            float lw = vw * 0.92f, lh = lw * logo_h / logo_w;
            if (lh > vh * 0.78f) { lh = vh * 0.78f; lw = lh * logo_w / logo_h; }
            SDL_FRect dst{ (vw - lw) / 2, vh * 0.40f - lh / 2, lw, lh };
            SDL_SetTextureColorMod(logo, 105, 105, 105);
            SDL_RenderCopyF(renderer, logo, nullptr, &dst);
        }
        // Bottom panel for readability.
        fill_rect(renderer, { 0, vh - 230, vw, 230 }, { 0, 0, 0, 170 });

        draw_text_centered(renderer, big, "Mabecs International Superstar Soccer 2000", vw / 2, vh - 222, { 255, 255, 255, 255 });
        draw_text_centered(renderer, small, "Widescreen build  -  native PC recompilation of the N64 USA (Rev 1) version", vw / 2, vh - 172, { 255, 200, 90, 255 });

        for (int i = 0; i < BTN_COUNT; i++) {
            const Button& b = buttons[i];
            bool focused = i == focus && b.enabled;
            SDL_Color bg = !b.enabled ? SDL_Color{ 40, 40, 48, 220 } : focused ? SDL_Color{ 230, 120, 20, 240 } : SDL_Color{ 25, 55, 150, 230 };
            fill_rect(renderer, b.rect, bg);
            outline_rect(renderer, b.rect, focused ? SDL_Color{ 255, 220, 80, 255 } : SDL_Color{ 120, 150, 255, 160 }, 2);
            SDL_Color fg = b.enabled ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 130, 130, 140, 255 };
            draw_text_centered(renderer, normal, b.label, b.rect.x + b.rect.w / 2, b.rect.y + (b.rect.h - normal.size) / 2 + 2, fg);
        }

        draw_text_centered(renderer, small, status, vw / 2, vh - 72, status_error ? SDL_Color{ 255, 120, 110, 255 } : SDL_Color{ 150, 230, 150, 255 });
        draw_text_centered(renderer, small, "Tip: you can also drag and drop the ROM file onto this window.", vw / 2, vh - 42, { 140, 145, 160, 255 });

        SDL_RenderPresent(renderer);
    }

    if (pad != nullptr) SDL_GameControllerClose(pad);
    if (nfd_ok) NFD_Quit();
    SDL_DestroyTexture(big.texture);
    SDL_DestroyTexture(normal.texture);
    SDL_DestroyTexture(small.texture);
    if (logo != nullptr) SDL_DestroyTexture(logo);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    save_launcher_settings(data_dir, settings);
    return play;
}
