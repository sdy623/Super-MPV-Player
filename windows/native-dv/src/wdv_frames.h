#pragma once
#include <stdint.h>
typedef struct wdv_frame_v2 {
    int64_t pts, duration;
    const uint8_t *y, *uv;
    int stride_y, stride_uv;
    const uint8_t *rpu;
    uint32_t rpu_len;
    int escaped;
    int paired_el;
} wdv_frame_v2;
typedef struct wdv_output_v2 {
    uint8_t *pixels;
    int stride;
    int64_t pts;
} wdv_output_v2;
typedef int (*wdv_render_fn)(const wdv_frame_v2 *,uint32_t,uint32_t,uint32_t,wdv_output_v2 *,char *,uint32_t);

// v5: textures belong to the caller and stay alive until render returns.
// Both textures must use the same ID3D11Device supplied to create_gpu_v5.
// No CPU pixel pointers are read or written on this path. RPU remains raw NAL.
typedef struct wdv_gpu_frame_v5 {
    void *input_texture;
    uint32_t input_subresource;
    void *output_texture;
} wdv_gpu_frame_v5;
