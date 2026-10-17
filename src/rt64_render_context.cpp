#include <memory>
#include <cstring>
#include <variant>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#define HLSL_CPU
#include "hle/rt64_application.h"

#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"

#include "iss_render.h"

// Adapted from Zelda64Recomp's src/main/rt64_render_context.cpp (GPL-3.0).

static uint8_t DMEM[0x1000];
static uint8_t IMEM[0x1000];

unsigned int MI_INTR_REG = 0;

unsigned int DPC_START_REG = 0;
unsigned int DPC_END_REG = 0;
unsigned int DPC_CURRENT_REG = 0;
unsigned int DPC_STATUS_REG = 0;
unsigned int DPC_CLOCK_REG = 0;
unsigned int DPC_BUFBUSY_REG = 0;
unsigned int DPC_PIPEBUSY_REG = 0;
unsigned int DPC_TMEM_REG = 0;

void dummy_check_interrupts() {}

RT64::UserConfiguration::AspectRatio to_rt64(ultramodern::renderer::AspectRatio option) {
    switch (option) {
        case ultramodern::renderer::AspectRatio::Original:
            return RT64::UserConfiguration::AspectRatio::Original;
        case ultramodern::renderer::AspectRatio::Expand:
            return RT64::UserConfiguration::AspectRatio::Expand;
        case ultramodern::renderer::AspectRatio::Manual:
            return RT64::UserConfiguration::AspectRatio::Manual;
        case ultramodern::renderer::AspectRatio::OptionCount:
            return RT64::UserConfiguration::AspectRatio::OptionCount;
    }
}

RT64::UserConfiguration::Antialiasing to_rt64(ultramodern::renderer::Antialiasing option) {
    switch (option) {
        case ultramodern::renderer::Antialiasing::None:
            return RT64::UserConfiguration::Antialiasing::None;
        case ultramodern::renderer::Antialiasing::MSAA2X:
            return RT64::UserConfiguration::Antialiasing::MSAA2X;
        case ultramodern::renderer::Antialiasing::MSAA4X:
            return RT64::UserConfiguration::Antialiasing::MSAA4X;
        case ultramodern::renderer::Antialiasing::MSAA8X:
            return RT64::UserConfiguration::Antialiasing::MSAA8X;
        case ultramodern::renderer::Antialiasing::OptionCount:
            return RT64::UserConfiguration::Antialiasing::OptionCount;
    }
}

RT64::UserConfiguration::RefreshRate to_rt64(ultramodern::renderer::RefreshRate option) {
    switch (option) {
        case ultramodern::renderer::RefreshRate::Original:
            return RT64::UserConfiguration::RefreshRate::Original;
        case ultramodern::renderer::RefreshRate::Display:
            return RT64::UserConfiguration::RefreshRate::Display;
        case ultramodern::renderer::RefreshRate::Manual:
            return RT64::UserConfiguration::RefreshRate::Manual;
        case ultramodern::renderer::RefreshRate::OptionCount:
            return RT64::UserConfiguration::RefreshRate::OptionCount;
    }
}

RT64::UserConfiguration::InternalColorFormat to_rt64(ultramodern::renderer::HighPrecisionFramebuffer option) {
    switch (option) {
        case ultramodern::renderer::HighPrecisionFramebuffer::Off:
            return RT64::UserConfiguration::InternalColorFormat::Standard;
        case ultramodern::renderer::HighPrecisionFramebuffer::On:
            return RT64::UserConfiguration::InternalColorFormat::High;
        case ultramodern::renderer::HighPrecisionFramebuffer::Auto:
            return RT64::UserConfiguration::InternalColorFormat::Automatic;
        case ultramodern::renderer::HighPrecisionFramebuffer::OptionCount:
            return RT64::UserConfiguration::InternalColorFormat::OptionCount;
    }
}

void set_application_user_config(RT64::Application* application, const ultramodern::renderer::GraphicsConfig& config) {
    switch (config.res_option) {
        default:
        case ultramodern::renderer::Resolution::Auto:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::WindowIntegerScale;
            application->userConfig.downsampleMultiplier = 1;
            break;
        case ultramodern::renderer::Resolution::Original:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
            application->userConfig.resolutionMultiplier = std::max(config.ds_option, 1);
            application->userConfig.downsampleMultiplier = std::max(config.ds_option, 1);
            break;
        case ultramodern::renderer::Resolution::Original2x:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
            application->userConfig.resolutionMultiplier = 2.0 * std::max(config.ds_option, 1);
            application->userConfig.downsampleMultiplier = std::max(config.ds_option, 1);
            break;
    }

    switch (config.hr_option) {
        default:
        case ultramodern::renderer::HUDRatioMode::Original:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Original;
            break;
        case ultramodern::renderer::HUDRatioMode::Clamp16x9:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Manual;
            application->userConfig.extAspectTarget = 16.0/9.0;
            break;
        case ultramodern::renderer::HUDRatioMode::Full:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Expand;
            break;
    }

    application->userConfig.aspectRatio = to_rt64(config.ar_option);
    application->userConfig.antialiasing = to_rt64(config.msaa_option);
    application->userConfig.refreshRate = to_rt64(config.rr_option);
    application->userConfig.refreshRateTarget = config.rr_manual_value;
    application->userConfig.internalColorFormat = to_rt64(config.hpfb_option);
    application->userConfig.displayBuffering = RT64::UserConfiguration::DisplayBuffering::Triple;
}

ultramodern::renderer::SetupResult map_setup_result(RT64::Application::SetupResult rt64_result) {
    switch (rt64_result) {
        case RT64::Application::SetupResult::Success:
            return ultramodern::renderer::SetupResult::Success;
        case RT64::Application::SetupResult::DynamicLibrariesNotFound:
            return ultramodern::renderer::SetupResult::DynamicLibrariesNotFound;
        case RT64::Application::SetupResult::InvalidGraphicsAPI:
            return ultramodern::renderer::SetupResult::InvalidGraphicsAPI;
        case RT64::Application::SetupResult::GraphicsAPINotFound:
            return ultramodern::renderer::SetupResult::GraphicsAPINotFound;
        case RT64::Application::SetupResult::GraphicsDeviceNotFound:
            return ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
    }

    fprintf(stderr, "Unhandled `RT64::Application::SetupResult` ?\n");
    assert(false);
    std::exit(EXIT_FAILURE);
}

ultramodern::renderer::GraphicsApi map_graphics_api(RT64::UserConfiguration::GraphicsAPI api) {
    switch (api) {
        case RT64::UserConfiguration::GraphicsAPI::D3D12:
            return ultramodern::renderer::GraphicsApi::D3D12;
        case RT64::UserConfiguration::GraphicsAPI::Vulkan:
            return ultramodern::renderer::GraphicsApi::Vulkan;
        case RT64::UserConfiguration::GraphicsAPI::Metal:
            return ultramodern::renderer::GraphicsApi::Metal;
        case RT64::UserConfiguration::GraphicsAPI::Automatic:
            return ultramodern::renderer::GraphicsApi::Auto;
    }

    fprintf(stderr, "Unhandled `RT64::UserConfiguration::GraphicsAPI` ?\n");
    assert(false);
    std::exit(EXIT_FAILURE);
}
iss::renderer::RT64Context::RT64Context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool debug) {
    static unsigned char dummy_rom_header[0x40];

    // Set up the RT64 application core fields.
    RT64::Application::Core appCore{};
#if defined(_WIN32)
    appCore.window = window_handle.window;
#elif defined(__linux__) || defined(__ANDROID__)
    appCore.window = window_handle;
#elif defined(__APPLE__)
    appCore.window.window = window_handle.window;
    appCore.window.view = window_handle.view;
#endif

    appCore.checkInterrupts = dummy_check_interrupts;

    appCore.HEADER = dummy_rom_header;
    appCore.RDRAM = rdram;
    appCore.DMEM = DMEM;
    appCore.IMEM = IMEM;

    appCore.MI_INTR_REG = &MI_INTR_REG;

    appCore.DPC_START_REG = &DPC_START_REG;
    appCore.DPC_END_REG = &DPC_END_REG;
    appCore.DPC_CURRENT_REG = &DPC_CURRENT_REG;
    appCore.DPC_STATUS_REG = &DPC_STATUS_REG;
    appCore.DPC_CLOCK_REG = &DPC_CLOCK_REG;
    appCore.DPC_BUFBUSY_REG = &DPC_BUFBUSY_REG;
    appCore.DPC_PIPEBUSY_REG = &DPC_PIPEBUSY_REG;
    appCore.DPC_TMEM_REG = &DPC_TMEM_REG;

    ultramodern::renderer::ViRegs* vi_regs = ultramodern::renderer::get_vi_regs();

    appCore.VI_STATUS_REG = &vi_regs->VI_STATUS_REG;
    appCore.VI_ORIGIN_REG = &vi_regs->VI_ORIGIN_REG;
    appCore.VI_WIDTH_REG = &vi_regs->VI_WIDTH_REG;
    appCore.VI_INTR_REG = &vi_regs->VI_INTR_REG;
    appCore.VI_V_CURRENT_LINE_REG = &vi_regs->VI_V_CURRENT_LINE_REG;
    appCore.VI_TIMING_REG = &vi_regs->VI_TIMING_REG;
    appCore.VI_V_SYNC_REG = &vi_regs->VI_V_SYNC_REG;
    appCore.VI_H_SYNC_REG = &vi_regs->VI_H_SYNC_REG;
    appCore.VI_LEAP_REG = &vi_regs->VI_LEAP_REG;
    appCore.VI_H_START_REG = &vi_regs->VI_H_START_REG;
    appCore.VI_V_START_REG = &vi_regs->VI_V_START_REG;
    appCore.VI_V_BURST_REG = &vi_regs->VI_V_BURST_REG;
    appCore.VI_X_SCALE_REG = &vi_regs->VI_X_SCALE_REG;
    appCore.VI_Y_SCALE_REG = &vi_regs->VI_Y_SCALE_REG;

    // Set up the RT64 application configuration fields.
    RT64::ApplicationConfiguration appConfig;
    appConfig.useConfigurationFile = false;

    // Create the RT64 application.
    app = std::make_unique<RT64::Application>(appCore, appConfig);

    // Set initial user config settings based on the current settings.
    auto& cur_config = ultramodern::renderer::get_graphics_config();
    set_application_user_config(app.get(), cur_config);
    app->userConfig.developerMode = debug;
    // Force gbi depth branches to prevent LODs from kicking in.
    app->enhancementConfig.f3dex.forceBranch = true;
    // Scale LODs based on the output resolution.
    app->enhancementConfig.textureLOD.scale = true;
    // Pick an API if the user has set an override.
    switch (cur_config.api_option) {
        case ultramodern::renderer::GraphicsApi::D3D12:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::D3D12;
            break;
        case ultramodern::renderer::GraphicsApi::Vulkan:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Vulkan;
            break;
        case ultramodern::renderer::GraphicsApi::Metal:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Metal;
            break;
        case ultramodern::renderer::GraphicsApi::Auto:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Automatic;
            break;
    }

    // Set up the RT64 application.
    uint32_t thread_id = 0;
#ifdef _WIN32
    thread_id = window_handle.thread_id;
#endif
    setup_result = map_setup_result(app->setup(thread_id));
    // Get the API that RT64 chose.
    chosen_api = map_graphics_api(app->chosenGraphicsAPI);
    if (setup_result != ultramodern::renderer::SetupResult::Success) {
        app = nullptr;
        return;
    }

    // Set the application's fullscreen state.
    app->setFullScreen(cur_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);
}

iss::renderer::RT64Context::~RT64Context() = default;

// Debug: when the file named by ISS_DL_DUMP exists, write the next frame's 2D-relevant F3DEX2 commands
// (fills, texrects, scissor, color image, display list calls) next to it and delete the trigger.
static void debug_dump_display_list(const uint8_t* rdram, uint32_t dl_start) {
    static const char* trigger = std::getenv("ISS_DL_DUMP");
    if (trigger == nullptr || !std::filesystem::exists(trigger)) {
        return;
    }
    std::filesystem::remove(trigger);
    FILE* out = fopen((std::string(trigger) + ".txt").c_str(), "w");
    if (out == nullptr) return;
    auto rd = [rdram](uint32_t addr) {
        addr &= 0x7FFFFF;
        return (uint32_t)rdram[addr ^ 3] << 24 | (uint32_t)rdram[(addr + 1) ^ 3] << 16 | (uint32_t)rdram[(addr + 2) ^ 3] << 8 | rdram[(addr + 3) ^ 3];
    };
    uint32_t segments[16]{};
    auto resolve = [&](uint32_t a) { return (segments[(a >> 24) & 0xF] + (a & 0xFFFFFF)) & 0x7FFFFF; };
    uint32_t stack[32]; int sp = 0;
    uint32_t pc = dl_start & 0x7FFFFF;
    for (int n = 0; n < 200000; n++) {
        uint32_t w0 = rd(pc), w1 = rd(pc + 4);
        uint8_t op = w0 >> 24;
        pc += 8;
        switch (op) {
            case 0xDB: // G_MOVEWORD
                if (((w0 >> 16) & 0xFF) == 6) segments[((w0 & 0xFFFF) / 4) & 0xF] = w1 & 0xFFFFFF;
                break;
            case 0xDE: // G_DL
                fprintf(out, "DL   %08X%s\n", w1, (w0 >> 16) & 1 ? " (branch)" : "");
                if (!((w0 >> 16) & 1) && sp < 32) stack[sp++] = pc;
                pc = resolve(w1);
                break;
            case 0xDF: // G_ENDDL
                if (sp == 0) { fclose(out); return; }
                pc = stack[--sp];
                break;
            case 0xFF: fprintf(out, "CIMG fmt %X width %u addr %08X\n", (w0 >> 19) & 0x1F, (w0 & 0xFFF) + 1, w1); break;
            case 0xED: fprintf(out, "SCIS (%u,%u)-(%u,%u)\n", (w0 >> 14) & 0x3FF, (w0 >> 2) & 0x3FF, (w1 >> 14) & 0x3FF, (w1 >> 2) & 0x3FF); break;
            case 0xF7: fprintf(out, "FCOL %08X\n", w1); break;
            case 0xF6: fprintf(out, "FILL (%u,%u)-(%u,%u)\n", (w1 >> 14) & 0x3FF, (w1 >> 2) & 0x3FF, (w0 >> 14) & 0x3FF, (w0 >> 2) & 0x3FF); break;
            case 0xFD: fprintf(out, "TIMG %08X @%08X\n", w1, pc - 8); break;
            case 0xE4: case 0xE5: fprintf(out, "TRCT (%.1f,%.1f)-(%.1f,%.1f)\n", ((w1 >> 12) & 0xFFF) / 4.0, (w1 & 0xFFF) / 4.0, ((w0 >> 12) & 0xFFF) / 4.0, (w0 & 0xFFF) / 4.0); pc += 16; break;
            case 0x01: { static uint32_t last = 0; (void)last; break; }
            case 0x05: case 0x06: case 0x07: break;
            default: break;
        }
        if (op == 0x01 || op == 0x05 || op == 0x06) {
            static thread_local int tri_run = 0; (void)tri_run;
        }
    }
    fclose(out);
}

// Debug: ISS_TIMING=1 logs, for each frame, when the display list arrives and how long RT64 takes, in ms since
// the last screen update (VI), for 3 seconds starting 25 seconds in.
static std::chrono::steady_clock::time_point last_vi = std::chrono::steady_clock::now();
static bool timing_enabled() {
    static const bool on = std::getenv("ISS_TIMING") != nullptr;
    return on;
}
static bool timing_window() {
    static const auto start = std::chrono::steady_clock::now();
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return t > 25.0 && t < 28.0;
}

void iss::renderer::RT64Context::send_dl(const OSTask* task) {
    debug_dump_display_list(app->core.RDRAM, task->t.data_ptr);
    auto dl_start = std::chrono::steady_clock::now();
    struct TimingLog {
        std::chrono::steady_clock::time_point start;
        ~TimingLog() {
            if (timing_enabled() && timing_window()) {
                auto end = std::chrono::steady_clock::now();
                fprintf(stderr, "[timing] DL arrives +%.2f ms after VI, RT64 took %.2f ms\n",
                    std::chrono::duration<double, std::milli>(start - last_vi).count(),
                    std::chrono::duration<double, std::milli>(end - start).count());
            }
        }
    } timing_log{ dl_start };
    static const bool debug = std::getenv("ISS_DEBUG") != nullptr;
    static uint32_t dl_count = 0;
    if (debug && (++dl_count % 60) == 1) {
        fprintf(stderr, "[gfx] display list #%u (ucode %08X)\n", dl_count, task->t.ucode);
    }
    app->state->rsp->reset();
    app->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFF, task->t.ucode_data & 0x3FFFFFF, true);
    app->processDisplayLists(app->core.RDRAM, task->t.data_ptr & 0x3FFFFFF, 0, true);
}

void iss::renderer::RT64Context::send_dummy_workload(uint32_t fb_address) {
    // Only used before the game starts (launcher UI); this host boots straight into the game.
    (void)fb_address;
}

void iss::renderer::RT64Context::update_screen() {
    auto vi_start = std::chrono::steady_clock::now();
    app->updateScreen();
    if (timing_enabled() && timing_window()) {
        fprintf(stderr, "[timing] VI: screen update took %.2f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - vi_start).count());
    }
    last_vi = vi_start;
}

void iss::renderer::RT64Context::shutdown() {
    if (app != nullptr) {
        app->end();
    }
}

bool iss::renderer::RT64Context::update_config(const ultramodern::renderer::GraphicsConfig& old_config, const ultramodern::renderer::GraphicsConfig& new_config) {
    if (old_config == new_config) {
        return false;
    }

    if (new_config.wm_option != old_config.wm_option) {
        app->setFullScreen(new_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);
    }

    set_application_user_config(app.get(), new_config);

    app->updateUserConfig(true);

    if (new_config.msaa_option != old_config.msaa_option) {
        app->updateMultisampling();
    }
    return true;
}

void iss::renderer::RT64Context::enable_instant_present() {
    // Enable the present early presentation mode for minimal latency.
    app->enhancementConfig.presentation.mode = RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly;

    app->updateEnhancementConfig();
}

uint32_t iss::renderer::RT64Context::get_display_framerate() const {
    return app->presentQueue->ext.sharedResources->swapChainRate;
}

float iss::renderer::RT64Context::get_resolution_scale() const {
    constexpr int ReferenceHeight = 240;
    switch (app->userConfig.resolution) {
        case RT64::UserConfiguration::Resolution::WindowIntegerScale:
            if (app->sharedQueueResources->swapChainHeight > 0) {
                return std::max(float((app->sharedQueueResources->swapChainHeight + ReferenceHeight - 1) / ReferenceHeight), 1.0f);
            }
            else {
                return 1.0f;
            }
        case RT64::UserConfiguration::Resolution::Manual:
            return float(app->userConfig.resolutionMultiplier);
        case RT64::UserConfiguration::Resolution::Original:
        default:
            return 1.0f;
    }
}

std::unique_ptr<ultramodern::renderer::RendererContext> iss::renderer::create_render_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
    return std::make_unique<iss::renderer::RT64Context>(rdram, window_handle, developer_mode);
}
