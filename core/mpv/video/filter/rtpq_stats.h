/* Isolated prototype, LGPL-2.1-or-later. Limited BT.2020 NCL P010 analysis. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define RTPQ_WIN 24
#define RTPQ_BINS 32

struct rtpq_lut { int y[1024], r[1024], gu[1024], gv[1024], b[1024]; };
struct rtpq_frame { int min, avg, peak; uint32_t bins[RTPQ_BINS], pixels; };
struct rtpq_state {
    struct rtpq_frame previous;
    int peaks[RTPQ_WIN], averages[RTPQ_WIN], count, next;
    double last_pts;
};
struct rtpq_meta { int min, avg, peak; bool cut; };

static inline void rtpq_lut_init(struct rtpq_lut *l)
{
    const double q=4095.*1024.;
    for(int i=0;i<1024;i++){
        double c=(i-512)/896.;
        l->y[i]=(int)lround(q*(i-64)/876.);
        l->r[i]=(int)lround(q*1.4746*c); l->b[i]=(int)lround(q*1.8814*c);
        l->gu[i]=(int)lround(-q*.16455312684366*c); l->gv[i]=(int)lround(-q*.57135312684366*c);
    }
}

static inline bool rtpq_analyze(const struct rtpq_lut *l,const uint16_t *ys,ptrdiff_t sy,
                                const uint16_t *uv,ptrdiff_t sc,int w,int h,struct rtpq_frame *f)
{
    if(!ys || !uv || w<2 || h<2 || w>3840 || h>2160 || (w|h)&1 || sy<2*w || sc<2*w) return false;
    uint32_t hist[4096]={0};
    *f=(struct rtpq_frame){.pixels=w*h,.min=-1,.peak=-1};
    uint64_t sum=0;
    for(int y=0;y<h;y+=2) {
        const uint16_t *a=(const void*)((const char*)ys+y*sy), *b=(const void*)((const char*)a+sy);
        const uint16_t *c=(const void*)((const char*)uv+(y/2)*sc);
        for(int x=0;x<w;x+=2){
            unsigned u=c[x]>>6,v=c[x+1]>>6;
            int k=l->r[v],g=l->gu[u]+l->gv[v],bb=l->b[u];
            if(g>k) k=g;
            if(bb>k) k=bb;
            int z[4]={a[x]>>6,a[x+1]>>6,b[x]>>6,b[x+1]>>6};
            for(int i=0;i<4;i++){
                int q=(l->y[z[i]]+k+512)/1024;
                q=q<0?0:q>4095?4095:q;
                hist[q]++;
            }
        }
    }
    const uint32_t min_rank=(f->pixels+999)/1000;
    const uint32_t peak_rank=((uint64_t)f->pixels*999+999)/1000;
    uint32_t enclosed=0;
    for(int q=0;q<4096;q++){
        uint32_t n=hist[q];
        enclosed+=n;sum+=(uint64_t)n*q;
        f->bins[q>>(12-5)]+=n;
        if(f->min<0 && enclosed>=min_rank) f->min=q;
        if(f->peak<0 && enclosed>=peak_rank) f->peak=q;
    }
    f->avg=(sum+f->pixels/2)/f->pixels;
    return true;
}

static inline struct rtpq_meta rtpq_push(struct rtpq_state *s,const struct rtpq_frame *f,double pts,double fps)
{
    uint32_t distance=0;
    for(int i=0;i<RTPQ_BINS;i++) distance+=abs((int)f->bins[i]-(int)s->previous.bins[i]);
    bool cut=!s->count || pts<=s->last_pts || pts-s->last_pts>3/fps ||
             f->pixels!=s->previous.pixels || distance>f->pixels*.76;
    if(cut) s->count=s->next=0;
    s->peaks[s->next]=f->peak;s->averages[s->next]=f->avg;
    s->next=(s->next+1)%RTPQ_WIN;
    if(s->count<RTPQ_WIN)s->count++;
    s->last_pts=pts;s->previous=*f;
    int a[RTPQ_WIN],avg=0;
    for(int i=0;i<s->count;i++){
        int j=i;
        while(j>0 && a[j-1]>s->peaks[i]){a[j]=a[j-1];j--;}
        a[j]=s->peaks[i]; avg+=s->averages[i];
    }
    return (struct rtpq_meta){f->min,(avg+s->count/2)/s->count,a[(95*s->count+99)/100-1],cut};
}
