#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Positive: output length; negative: fail closed, retain original MPV frame/EL.
// Output is de-escaped NAL62 (two-byte HEVC NAL header included).
// PQ BL + BL/EL PTS matching must be checked independently by the caller.
int wdv_convert_rpu_v1(const uint8_t *input, size_t length, int escaped,
                      uint8_t *output, size_t capacity, uint8_t *source_profile);
#ifdef __cplusplus
}
#endif
