// Declarations for C hooks injected into recompiled code via [[patches.hook]] (recomp/iss2000.us.rev1.toml).
#ifndef ISS_HOOKS_H
#define ISS_HOOKS_H

#include "recomp.h"

#ifdef __cplusplus
extern "C" {
#endif

void iss_on_rom_read(uint8_t* rdram, recomp_context* ctx);
void iss_resolution_option(uint8_t* rdram, recomp_context* ctx);
void iss_player_lod(uint8_t* rdram, recomp_context* ctx);
// Launcher option: nonzero = players always use their best model, 0 = the game's distance-based LOD.
void iss_set_full_player_detail(int full);
void iss_debug_switch_res(uint8_t* rdram, recomp_context* ctx);
void iss_debug_sprite(uint8_t* rdram, recomp_context* ctx);
void iss_debug_force_res(uint8_t* rdram, recomp_context* ctx);
void iss_sky_stretch_begin(uint8_t* rdram, recomp_context* ctx);
void iss_sky_stretch_end(uint8_t* rdram, recomp_context* ctx);
void iss_widen_cull_frustum(uint8_t* rdram, recomp_context* ctx);
void iss_widen_cull_far_plane(uint8_t* rdram, recomp_context* ctx);
void iss_set_tv_type_pal(uint8_t* rdram, recomp_context* ctx);
void iss_pal_vi_mode(uint8_t* rdram, recomp_context* ctx);

// Horizontal widening factor for the game's view-frustum culling (window aspect / 4:3, >= 1).
float iss_cull_widen_factor(void);

#ifdef __cplusplus
}
#endif

#endif
