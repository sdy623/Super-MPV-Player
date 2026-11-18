/* Bounded SDR appearance correction after the existing DV display map. */
#ifndef MPV_ADAPTIVE_DV_SDR_H
#define MPV_ADAPTIVE_DV_SDR_H
#include <math.h>

struct dvsdr_scene { float avg_y, shoulder, near_white; };
struct dvsdr_gain { float high, low; };

static inline float dvsdr_smooth(float lo, float hi, float x)
{
    float t = fminf(1, fmaxf(0, (x - lo) / (hi - lo)));
    return t*t*(3 - 2*t);
}

static inline struct dvsdr_gain dvsdr_policy(struct dvsdr_scene s, float peak,
                                              float anchor, float black)
{
    if (!isfinite(peak) || !isfinite(anchor) || !isfinite(black) ||
        !isfinite(s.avg_y) || !isfinite(s.shoulder) || !isfinite(s.near_white) ||
        !(peak > anchor && anchor > black && black > 0))
        return (struct dvsdr_gain){0};

    // Device budget comes from the same Advanced SDR target as the base map.
    const float headroom = dvsdr_smooth(.04f, .32f, (peak-anchor)/peak);
    const float contrast = 1-dvsdr_smooth(.002f, .020f, black/peak);
    const float lit = dvsdr_smooth(.08f, .18f, s.avg_y);
    const float broad_white = 1-dvsdr_smooth(.30f, .46f, s.avg_y);
    const float shoulder = dvsdr_smooth(.008f, .075f, s.shoulder);
    const float white_coverage = 1-dvsdr_smooth(.12f, .32f, s.near_white);
    return (struct dvsdr_gain){
        .high = .055f * headroom * contrast * lit * broad_white * shoulder * white_coverage,
        .low = .013f * headroom * contrast * (1-dvsdr_smooth(.05f,.12f,s.avg_y)),
    };
}
#endif
