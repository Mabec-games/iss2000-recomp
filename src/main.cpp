// ISS 2000 Recompiled — host executable.
//
// Usage: iss2000recomp [path/to/rom.z64] [--no-launcher]
// Supports the USA (Rev 1) and Europe ROMs (src/iss_version.cpp). A ROM picked in the launcher or given on
// the command line is validated and stored by the runtime; the game then boots via its --game CLI path.
#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <atomic>

#include "SDL2/SDL.h"
#include "SDL2/SDL_syswm.h"
#ifdef _WIN32
#include <windows.h>
#endif
#undef None
#undef Status
#undef LockMask
#undef Always
#undef Success

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"
#include "librecomp/rsp.hpp"
#include "ultramodern/config.hpp"

#include "iss_render.h"
#include "launcher.h"
#include "iss_version.h"

extern RspUcodeFunc aspMain;
extern "C" void iss_controller_pak_set_data_dir(const char* dir);
extern "C" bool iss_controller_pak_inserted(int channel);

namespace {
    SDL_Window* window = nullptr;
    SDL_AudioDeviceID audio_device = 0;
    SDL_AudioStream* audio_stream = nullptr;
    uint32_t game_sample_rate = 32000;
    constexpr int output_sample_rate = 48000;
    SDL_GameController* controller = nullptr;

    void toggle_fullscreen();

    // Open the first connected game controller if none is open. Polled every frame because controllers that
    // were already connected are announced (SDL_CONTROLLERDEVICEADDED) only once, and the launcher consumes it.
    void ensure_controller() {
        if (controller != nullptr) {
            if (SDL_GameControllerGetAttached(controller)) return;
            SDL_GameControllerClose(controller);
            controller = nullptr;
        }
        static int last_count = -1;
        int count = SDL_NumJoysticks();
        for (int i = 0; i < count; i++) {
            if (SDL_IsGameController(i)) {
                controller = SDL_GameControllerOpen(i);
                if (controller != nullptr) {
                    fprintf(stderr, "Controller: %s\n", SDL_GameControllerName(controller));
                    return;
                }
            }
            else if (count != last_count) {
                fprintf(stderr, "Controller: joystick \"%s\" has no known button mapping\n", SDL_JoystickNameForIndex(i));
            }
        }
        last_count = count;
    }

    std::atomic<float> cull_widen_factor{ 1.0f };

    // Window aspect relative to 4:3 when widescreen expansion is on; read by the culling hooks.
    void update_cull_widen_factor() {
        if (window == nullptr || ultramodern::renderer::get_graphics_config().ar_option != ultramodern::renderer::AspectRatio::Expand) {
            cull_widen_factor = 1.0f;
            return;
        }
        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        float factor = (h > 0) ? ((float)w / (float)h) / (4.0f / 3.0f) : 1.0f;
        cull_widen_factor = factor > 1.0f ? factor : 1.0f;
    }

    // ---- Graphics ----

    ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
            fprintf(stderr, "Failed to initialize SDL2: %s\n", SDL_GetError());
            std::exit(EXIT_FAILURE);
        }
        return {};
    }

    ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t) {
        uint32_t flags = SDL_WINDOW_RESIZABLE;
#if defined(__linux__)
        flags |= SDL_WINDOW_VULKAN;
#endif
        if (ultramodern::renderer::get_graphics_config().wm_option == ultramodern::renderer::WindowMode::Fullscreen) {
            flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        }
        window = SDL_CreateWindow(ISS_APP_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 900, flags);
        if (window != nullptr) {
            iss::set_window_icon(window);
        }
        if (window == nullptr) {
            fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
            std::exit(EXIT_FAILURE);
        }
#if defined(_WIN32)
        SDL_SysWMinfo wm_info;
        SDL_VERSION(&wm_info.version);
        SDL_GetWindowWMInfo(window, &wm_info);
        return ultramodern::renderer::WindowHandle{ wm_info.info.win.window, GetCurrentThreadId() };
#else
        return ultramodern::renderer::WindowHandle{ window };
#endif
    }

    void update_gfx(void*) {
        update_cull_widen_factor();
        ensure_controller();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    ultramodern::quit();
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.scancode == SDL_SCANCODE_F11 && !event.key.repeat) {
                        toggle_fullscreen();
                    }
                    else if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE && !event.key.repeat) {
                        ultramodern::quit();
                    }
                    break;
                case SDL_CONTROLLERDEVICEREMOVED:
                    if (controller != nullptr && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == event.cdevice.which) {
                        SDL_GameControllerClose(controller);
                        controller = nullptr;
                    }
                    break;
            }
        }
    }

    // ---- Graphics settings ----
    //
    // The runtime zero-initializes its config, which means native 320x240, 4:3, no AA. Pick modern
    // defaults; each can be overridden with an environment variable:
    //   ISS_RES=original|2x|auto  ISS_ASPECT=original|wide  ISS_MSAA=0|2|4|8  ISS_FULLSCREEN=1
    std::string env_or(const char* name, const char* fallback) {
        const char* v = std::getenv(name);
        return v != nullptr ? std::string(v) : std::string(fallback);
    }

    void apply_graphics_config(const iss::LauncherSettings& settings) {
        using namespace ultramodern::renderer;
        GraphicsConfig cfg{};
        cfg.developer_mode = false;
        std::string res = env_or("ISS_RES", "auto");
        cfg.res_option = res == "original" ? Resolution::Original : res == "2x" ? Resolution::Original2x : Resolution::Auto;
        cfg.wm_option = env_or("ISS_FULLSCREEN", settings.fullscreen ? "1" : "0") == "1" ? WindowMode::Fullscreen : WindowMode::Windowed;
        bool wide = env_or("ISS_ASPECT", settings.widescreen ? "wide" : "original") != "original";
        cfg.ar_option = wide ? AspectRatio::Expand : AspectRatio::Original;
        cfg.hr_option = wide ? HUDRatioMode::Clamp16x9 : HUDRatioMode::Original;
        cfg.api_option = GraphicsApi::Auto;
        std::string msaa = env_or("ISS_MSAA", "4");
        cfg.msaa_option = msaa == "8" ? Antialiasing::MSAA8X : msaa == "2" ? Antialiasing::MSAA2X : msaa == "0" ? Antialiasing::None : Antialiasing::MSAA4X;
        cfg.rr_option = RefreshRate::Original;
        cfg.hpfb_option = HighPrecisionFramebuffer::Auto;
        cfg.rr_manual_value = 60;
        cfg.ds_option = 1;
        set_graphics_config(cfg);
    }

    void toggle_fullscreen() {
        using namespace ultramodern::renderer;
        GraphicsConfig cfg = get_graphics_config();
        cfg.wm_option = cfg.wm_option == WindowMode::Fullscreen ? WindowMode::Windowed : WindowMode::Fullscreen;
        SDL_SetWindowFullscreen(window, cfg.wm_option == WindowMode::Fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        set_graphics_config(cfg);
    }

    // ---- Audio ----

    void rebuild_audio_stream() {
        if (audio_stream != nullptr) {
            SDL_FreeAudioStream(audio_stream);
        }
        audio_stream = SDL_NewAudioStream(AUDIO_F32, 2, game_sample_rate, AUDIO_F32, 2, output_sample_rate);
    }

    void reset_audio() {
        SDL_InitSubSystem(SDL_INIT_AUDIO);
        SDL_AudioSpec desired{};
        desired.freq = output_sample_rate;
        desired.format = AUDIO_F32;
        desired.channels = 2;
        desired.samples = 0x100;
        audio_device = SDL_OpenAudioDevice(nullptr, false, &desired, nullptr, 0);
        if (audio_device == 0) {
            fprintf(stderr, "SDL error opening audio device: %s\n", SDL_GetError());
            return;
        }
        rebuild_audio_stream();
        SDL_PauseAudioDevice(audio_device, 0);
    }

    void queue_samples(int16_t* audio_data, size_t sample_count) {
        if (audio_device == 0 || audio_stream == nullptr) {
            return;
        }
        // A negative length from the game (its audio code underflows if the queue holds more than it expects,
        // e.g. a PAL version forced to 60 Hz) arrives as a huge count; drop it instead of reading past RDRAM.
        if (sample_count > game_sample_rate * 2) {
            fprintf(stderr, "[audio] ignored bogus buffer of %zu samples\n", sample_count);
            return;
        }
        static std::vector<float> converted;
        converted.resize(sample_count);
        // Swap channels to undo the 32-bit word swap from endianness handling.
        for (size_t i = 0; i + 1 < sample_count; i += 2) {
            converted[i + 0] = audio_data[i + 1] * (0.5f / 32768.0f);
            converted[i + 1] = audio_data[i + 0] * (0.5f / 32768.0f);
        }
        SDL_AudioStreamPut(audio_stream, converted.data(), (int)(sample_count * sizeof(float)));
        int available = SDL_AudioStreamAvailable(audio_stream);
        if (available > 0) {
            static std::vector<uint8_t> out;
            out.resize(available);
            int got = SDL_AudioStreamGet(audio_stream, out.data(), available);
            // Drop audio if more than 100ms is already queued so latency doesn't build up.
            static const bool audio_debug = std::getenv("ISS_AUDIO_DEBUG") != nullptr || std::getenv("ISS_FPS") != nullptr;
            uint32_t queued = SDL_GetQueuedAudioSize(audio_device);
            if (audio_debug) {
                static auto last = SDL_GetTicks();
                static uint32_t min_q = UINT32_MAX, max_q = 0, chunks = 0, empties = 0;
                min_q = std::min(min_q, queued); max_q = std::max(max_q, queued); chunks++;
                if (queued == 0) empties++;
                if (SDL_GetTicks() - last >= 2000) {
                    double ms = 1000.0 / (output_sample_rate * 2.0 * sizeof(float));
                    fprintf(stderr, "[audio] queued min %.1f ms max %.1f ms, %u chunks, %u ran dry, rate %u\n", min_q * ms, max_q * ms, chunks, empties, game_sample_rate);
                    last = SDL_GetTicks(); min_q = UINT32_MAX; max_q = 0; chunks = 0; empties = 0;
                }
            }
            if (got > 0 && queued < output_sample_rate * 2 * sizeof(float) / 10) {
                SDL_QueueAudio(audio_device, out.data(), got);
            }
            else if (audio_debug) {
                fprintf(stderr, "[audio] dropped %d bytes (queued %.1f ms)\n", got, queued / (output_sample_rate * 2.0 * sizeof(float)) * 1000.0);
            }
        }
    }

    size_t get_frames_remaining() {
        if (audio_device == 0) {
            return 0;
        }
        // The game tops its audio up to a small target based on this value. Report two VIs' worth less than is
        // really queued so it keeps a cushion; reporting the exact level left the output running dry several
        // times per second (audible crackles).
        static const uint64_t cushion_vis = std::getenv("ISS_AUDIO_CUSHION") ? std::strtoull(std::getenv("ISS_AUDIO_CUSHION"), nullptr, 10) : 2;
        uint64_t queued_bytes = SDL_GetQueuedAudioSize(audio_device);
        uint64_t queued_frames = queued_bytes / (2 * sizeof(float)) * game_sample_rate / output_sample_rate;
        uint64_t cushion = cushion_vis * game_sample_rate / ultramodern::get_vi_rate();
        return (size_t)(queued_frames > cushion ? queued_frames - cushion : 0);
    }

    void set_frequency(uint32_t freq) {
        if (std::getenv("ISS_AUDIO_DEBUG") != nullptr) {
            fprintf(stderr, "[audio] set_frequency %u (was %u)\n", freq, game_sample_rate);
        }
        game_sample_rate = freq;
        rebuild_audio_stream();
    }

    // ---- Input ----

    // N64 button bits
    constexpr uint16_t CONT_A = 0x8000, CONT_B = 0x4000, CONT_G = 0x2000, CONT_START = 0x1000,
        CONT_UP = 0x0800, CONT_DOWN = 0x0400, CONT_LEFT = 0x0200, CONT_RIGHT = 0x0100,
        CONT_L = 0x0020, CONT_R = 0x0010, CONT_E = 0x0008, CONT_D = 0x0004, CONT_C = 0x0002, CONT_F = 0x0001;

    void poll_input() {}

    // Debug input injection: ISS_INPUT_FILE=<path> makes controller 1 read held buttons from a text file,
    // e.g. "A START UP" or "STICK -1 0". Used to drive the game from scripts while debugging.
    uint16_t injected_buttons(float* x, float* y) {
        static const char* path = std::getenv("ISS_INPUT_FILE");
        if (path == nullptr) {
            return 0;
        }
        FILE* f = fopen(path, "r");
        if (f == nullptr) {
            return 0;
        }
        struct { const char* name; uint16_t bit; } names[] = {
            { "A", CONT_A }, { "B", CONT_B }, { "Z", CONT_G }, { "START", CONT_START }, { "L", CONT_L }, { "R", CONT_R },
            { "UP", CONT_UP }, { "DOWN", CONT_DOWN }, { "LEFT", CONT_LEFT }, { "RIGHT", CONT_RIGHT },
            { "CU", CONT_E }, { "CD", CONT_D }, { "CL", CONT_C }, { "CR", CONT_F },
        };
        uint16_t b = 0;
        char tok[32];
        while (fscanf(f, "%31s", tok) == 1) {
            if (strcmp(tok, "STICK") == 0) {
                if (fscanf(f, "%f %f", x, y) != 2) break;
                continue;
            }
            for (const auto& n : names) {
                if (strcmp(tok, n.name) == 0) b |= n.bit;
            }
        }
        fclose(f);
        return b;
    }

    bool get_input(int controller_num, uint16_t* buttons, float* x, float* y) {
        if (controller_num != 0) {
            return false;
        }
        uint16_t b = 0;
        float ax = 0.0f, ay = 0.0f;

        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        struct { SDL_Scancode key; uint16_t bit; } keymap[] = {
            { SDL_SCANCODE_X, CONT_A }, { SDL_SCANCODE_C, CONT_B }, { SDL_SCANCODE_Z, CONT_G },
            { SDL_SCANCODE_RETURN, CONT_START }, { SDL_SCANCODE_Q, CONT_L }, { SDL_SCANCODE_E, CONT_R },
            { SDL_SCANCODE_I, CONT_E }, { SDL_SCANCODE_K, CONT_D }, { SDL_SCANCODE_J, CONT_C }, { SDL_SCANCODE_L, CONT_F },
            { SDL_SCANCODE_T, CONT_UP }, { SDL_SCANCODE_G, CONT_DOWN }, { SDL_SCANCODE_F, CONT_LEFT }, { SDL_SCANCODE_H, CONT_RIGHT },
        };
        for (const auto& m : keymap) {
            if (keys[m.key]) b |= m.bit;
        }
        if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) ax -= 1.0f;
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) ax += 1.0f;
        if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) ay += 1.0f;
        if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) ay -= 1.0f;

        if (controller != nullptr) {
            auto btn = [](SDL_GameControllerButton sb) { return SDL_GameControllerGetButton(controller, sb) != 0; };
            auto axis = [](SDL_GameControllerAxis sa) { return SDL_GameControllerGetAxis(controller, sa) / 32767.0f; };
            if (btn(SDL_CONTROLLER_BUTTON_A)) b |= CONT_A;
            if (btn(SDL_CONTROLLER_BUTTON_X)) b |= CONT_B;
            if (btn(SDL_CONTROLLER_BUTTON_START)) b |= CONT_START;
            if (btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) b |= CONT_G;
            if (btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) b |= CONT_R;
            if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP)) b |= CONT_UP;
            if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) b |= CONT_DOWN;
            if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) b |= CONT_LEFT;
            if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= CONT_RIGHT;
            if (btn(SDL_CONTROLLER_BUTTON_BACK)) b |= CONT_L;
            if (axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 0.5f) b |= CONT_G;
            if (axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.5f) b |= CONT_R;
            float rx = axis(SDL_CONTROLLER_AXIS_RIGHTX), ry = axis(SDL_CONTROLLER_AXIS_RIGHTY);
            if (ry < -0.5f) b |= CONT_E;
            if (ry > 0.5f) b |= CONT_D;
            if (rx < -0.5f) b |= CONT_C;
            if (rx > 0.5f) b |= CONT_F;
            if (btn(SDL_CONTROLLER_BUTTON_Y)) b |= CONT_C;
            if (btn(SDL_CONTROLLER_BUTTON_B)) b |= CONT_D;
            float lx = axis(SDL_CONTROLLER_AXIS_LEFTX), ly = -axis(SDL_CONTROLLER_AXIS_LEFTY);
            if (lx * lx + ly * ly > 0.04f) { ax = lx; ay = ly; }
        }

        float ix = 0.0f, iy = 0.0f;
        b |= injected_buttons(&ix, &iy);
        if (ix != 0.0f || iy != 0.0f) { ax = ix; ay = iy; }

        *buttons = b;
        *x = ax;
        *y = ay;
        return true;
    }

    void set_rumble(int, bool) {}

    ultramodern::input::connected_device_info_t get_connected_device_info(int controller_num) {
        if (controller_num == 0) {
            // The runtime only knows "no pak" / "Rumble Pak"; any pak sets the controller's pak-inserted
            // status bit, which is all the game reads here. It identifies the pak itself with its own
            // osMotorInit (recompiled), which correctly reports a memory pak as "not a Rumble Pak".
            auto pak = iss_controller_pak_inserted(0) ? ultramodern::input::Pak::RumblePak : ultramodern::input::Pak::None;
            return { ultramodern::input::Device::Controller, pak };
        }
        return { ultramodern::input::Device::None, ultramodern::input::Pak::None };
    }

    // ---- RSP ----

    RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
        switch (task->t.type) {
            case M_AUDTASK:
                return aspMain;
            default:
                fprintf(stderr, "Unknown RSP task type: %" PRIu32 "\n", task->t.type);
                return nullptr;
        }
    }

    void message_box(const char* msg) {
        fprintf(stderr, "%s\n", msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, ISS_APP_NAME, msg, window);
    }
}


extern "C" float iss_cull_widen_factor(void) {
    return cull_widen_factor.load();
}

// Per-user data folder (stored ROM, Controller Pak, settings): $ISS_DATA_DIR, else
// $XDG_DATA_HOME/iss2000recomp, else ~/.local/share/iss2000recomp (Windows: %APPDATA%\iss2000recomp).
static std::filesystem::path get_data_dir() {
#ifdef _WIN32
    if (const wchar_t* d = _wgetenv(L"ISS_DATA_DIR")) return d;
    if (const wchar_t* a = _wgetenv(L"APPDATA"); a != nullptr && a[0] != L'\0') return std::filesystem::path(a) / "iss2000recomp";
#else
    if (const char* d = std::getenv("ISS_DATA_DIR")) return d;
#endif
    if (const char* x = std::getenv("XDG_DATA_HOME"); x != nullptr && x[0] != '\0') return std::filesystem::path(x) / "iss2000recomp";
    if (const char* h = std::getenv("HOME")) return std::filesystem::path(h) / ".local" / "share" / "iss2000recomp";
    return std::filesystem::current_path() / "iss2000recomp_data";
}

int main(int argc, char** argv) {
#ifdef _WIN32
    // GUI program: when started from a terminal (and the log isn't redirected), show the log there.
    if (GetStdHandle(STD_ERROR_HANDLE) == nullptr && AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
    // Sharp launcher and game windows on scaled displays (sizes are then in pixels).
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
    const std::filesystem::path data_dir = get_data_dir();
    std::filesystem::create_directories(data_dir);
    recomp::register_config_path(data_dir);
    iss_controller_pak_set_data_dir(reinterpret_cast<const char*>(data_dir.u8string().c_str()));

    for (size_t i = 0; i < iss_num_versions; i++) {
        const IssVersion& v = iss_versions[i];
        recomp::GameEntry game{};
        game.rom_hash = v.rom_hash;
        game.internal_name = "I.S.S.2000";
        game.display_name = "International Superstar Soccer 2000";
        game.game_id = v.game_id;
        game.mod_game_id = v.mod_game_id;
        game.save_type = recomp::SaveType::None;
        game.is_enabled = true;
        game.entrypoint_address = (gpr)(int32_t)0x80000400;
        game.entrypoint = v.entrypoint;
        recomp::register_game(game);
    }

    // Arguments: [rom path] [--no-launcher]. A ROM path is validated and stored like one picked in the launcher.
    std::vector<char*> args{ argv[0] };
    bool use_launcher = true;
    const IssVersion* cli_version = nullptr;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--no-launcher") == 0) {
            use_launcher = false;
        }
        else if (argv[i][0] != '-') {
            recomp::RomValidationError err;
            cli_version = iss::select_rom_any_version(std::filesystem::path(reinterpret_cast<const char8_t*>(argv[i])), err);
            if (cli_version == nullptr) {
                fprintf(stderr, "ROM rejected (error %d): a USA (Rev 1) or Europe ROM of International Superstar Soccer 2000 is required.\n", (int)err);
            }
        }
        else {
            args.push_back(argv[i]);
        }
    }
    recomp::check_all_stored_roms();

    iss::LauncherSettings settings = iss::load_launcher_settings(data_dir);
    if (cli_version != nullptr) {
        settings.version = cli_version->id;
    }
    const IssVersion* version = nullptr;
    if (use_launcher) {
        version = iss::run_launcher(data_dir, settings);
        if (version == nullptr) {
            return EXIT_SUCCESS;
        }
    }
    else {
        version = iss::stored_version(settings);
        if (version == nullptr) {
            fprintf(stderr, "No valid ROM stored; run without --no-launcher (or pass the ROM path) first.\n");
            return EXIT_FAILURE;
        }
    }
    fprintf(stderr, "Version: %s\n", version->name);
    iss_select_version(*version);
    // ISS_VI_RATE=<hz> overrides the version's VI rate (debugging, PAL speed comparisons).
    const char* vi_rate = std::getenv("ISS_VI_RATE");
    ultramodern::set_vi_rate(vi_rate != nullptr ? (uint32_t)std::atoi(vi_rate) : version->pal ? 50 : 60);
    version->register_overlays();

    static char game_flag[] = "--game";
    args.push_back(game_flag);
    args.push_back(const_cast<char*>(version->mod_game_id));

    reset_audio();
    apply_graphics_config(settings);

    recomp::Configuration cfg{};
    cfg.argc = (int)args.size();
    cfg.argv = args.data();
    cfg.rsp_callbacks = { .get_rsp_microcode = get_rsp_microcode };
    cfg.renderer_callbacks = { .create_render_context = iss::renderer::create_render_context };
    cfg.audio_callbacks = { .queue_samples = queue_samples, .get_frames_remaining = get_frames_remaining, .set_frequency = set_frequency };
    cfg.input_callbacks = { .poll_input = poll_input, .get_input = get_input, .set_rumble = set_rumble, .get_connected_device_info = get_connected_device_info };
    cfg.gfx_callbacks = { .create_gfx = create_gfx, .create_window = create_window, .update_gfx = update_gfx };
    cfg.error_handling_callbacks = { .message_box = message_box };

    recomp::start(cfg);
    return EXIT_SUCCESS;
}
