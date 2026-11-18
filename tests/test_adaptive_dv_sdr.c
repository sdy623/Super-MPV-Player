#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "adaptive_dv_sdr.h"

int main(void)
{
    // Linear, normalized probe data, not physical nits or screenshots.
    const struct dvsdr_scene ordinary = {.avg_y=.2365f,.shoulder=.24f,.near_white=.06f};
    const struct dvsdr_scene bright = {.avg_y=.4787f,.shoulder=.28f,.near_white=.30f};
    const struct dvsdr_scene dark = {.avg_y=.0335f,.shoulder=.025f,.near_white=.002f};
    const struct dvsdr_scene sparse = {.avg_y=.21f,.shoulder=.004f,.near_white=.004f};
    const struct dvsdr_scene nearwhite = {.avg_y=.21f,.shoulder=.20f,.near_white=.44f};
    const struct dvsdr_gain a = dvsdr_policy(ordinary,400,270,.203f);
    fprintf(stderr,"Eligible medium-brightness content needs a bounded shoulder (got %f)\n",a.high);
    assert(a.high > .035f && a.high <= .055f);
    assert(dvsdr_policy(bright,400,270,.203f).high == 0);
    assert(dvsdr_policy(dark,400,270,.203f).high == 0);
    assert(dvsdr_policy(dark,400,270,.203f).low > 0);
    assert(dvsdr_policy(sparse,400,270,.203f).high == 0);
    assert(dvsdr_policy(nearwhite,400,270,.203f).high == 0);
    assert(dvsdr_policy(ordinary,280,270,.203f).high == 0);
    assert(dvsdr_policy(ordinary,400,270,9).high == 0);
    assert(dvsdr_policy(ordinary,400,270,0).high == 0);
    assert(dvsdr_policy(ordinary,400,NAN,.203f).high == 0);
    assert(dvsdr_policy(ordinary,340,270,.203f).high < a.high);
    assert(dvsdr_policy(ordinary,400,270,2).high < a.high);
    puts("Adaptive SDR policy: display, broad white, dark/sparse and shoulder gates pass.");
}
