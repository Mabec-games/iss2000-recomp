// Debug-only (trace build): count calls per recompiled function. See src/iss_trace.cpp.
#ifndef ISS_TRACE_H
#define ISS_TRACE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t* iss_trace_register(const char* name);
void iss_trace_gfx(const char* name, const uint8_t* rdram);
#ifdef __cplusplus
}
#endif
#define TRACE_RETURN()
#define TRACE_ENTRY() { static uint32_t* iss_trace_counter = 0; if (!iss_trace_counter) iss_trace_counter = iss_trace_register(__func__); (*iss_trace_counter)++; iss_trace_gfx(__func__, rdram); }
#endif
