/* P8.4's preset analysis histogram is in nominal 12-bit PQ. */
#pragma once
static inline bool rt84_stats(const uint32_t hist[4096],uint32_t pixels,struct rtpq_frame *f)
{
    if(!pixels) return false;
    *f=(struct rtpq_frame){.pixels=pixels,.min=-1,.peak=-1};
    uint32_t n=0,min_rank=(pixels+999)/1000,peak_rank=((uint64_t)pixels*999+999)/1000;
    uint64_t total=0;
    for(unsigned q=0;q<4096;q++){
        if(hist[q] && (q<62 || q>3079)) return false;
        n+=hist[q];
        total+=(uint64_t)q*hist[q];
        f->bins[q>>7]+=hist[q];
        if(f->min<0 && n>=min_rank)f->min=q;
        if(f->peak<0 && n>=peak_rank)f->peak=q;
    }
    if(n!=pixels) return false;
    f->avg=(total+pixels/2)/pixels;
    return true;
}
