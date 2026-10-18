// Hands N64Recomp's generated section tables to the runtime. Compiled into each version's recompiled library
// (with that version's output directory on the include path), so the function below ends up prefixed as
// us_iss_register_overlays, ende_iss_register_overlays, ... (see CMakeLists.txt and src/iss_version.cpp).
#include "recomp_overlays.inl"

#include "librecomp/overlays.hpp"

extern "C" void iss_register_overlays() {
    recomp::overlays::overlay_section_table_data_t sections {
        .code_sections = section_table,
        .num_code_sections = ARRLEN(section_table),
        .total_num_sections = num_sections,
    };

    recomp::overlays::overlays_by_index_t overlays {
        .table = overlay_sections_by_index,
        .len = ARRLEN(overlay_sections_by_index),
    };

    recomp::overlays::register_overlays(sections, overlays);
}
