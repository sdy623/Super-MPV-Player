#pragma once
#include <stdint.h>
// Additive experiment ABI; never changes wdv_gpu_frame_v5 or legacy exports.
// repr points to PL_API_VER=387 structs kept alive with the original frames.
typedef struct wdv_fel_frame_v10 {
    void *el_texture;
    uint32_t el_subresource,el_w,el_h;
    const void *bl_repr,*el_repr;
    int bl_chroma,el_chroma;
    uint32_t pl_api_ver;
} wdv_fel_frame_v10;
