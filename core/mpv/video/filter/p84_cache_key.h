/* Fixed composer; only these serialized RPU fields vary. PTS is not in the RPU. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define P84_CACHE_CAP 64
static inline uint32_t p84_key(unsigned min,unsigned avg,unsigned max,bool cut)
{
    return 1u+((min<<25)|(avg<<13)|(max<<1)|cut);
}
static inline unsigned p84_slot(uint32_t key)
{
    return ((key^(key>>12)^(key>>25))*UINT32_C(2654435761)) >> 26;
}
