/* Experimental in-process Windows Dolby renderer bridge, LGPL-2.1-or-later.
 * MPV still owns the demuxer, disc navigation, audio clock, OSD and window.
 * Failure preserves the original BL/EL frame. No file conversion is performed.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <math.h>
#include <d3d11.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
#include "common/msg.h"
#include "common/common.h"
#include "common/tags.h"
#include "filters/filter_internal.h"
#include "filters/user_filters.h"
#include "options/m_option.h"
#include "osdep/io.h"
#include "video/mp_image.h"
#include "video/mp_image_pool.h"
#include "video/fmt-conversion.h"
#include "wdv_frames.h"
#include "fel_abi.h"
#include <wchar.h>
#include "rtpq_stats.h"
#include "p84_cache_key.h"
#include "rt84_gpu.h"

/* Embedded into the standalone filter TU only. LGPL-2.1-or-later. */


struct rt_dovi_data { const uint8_t *data; size_t len; };
struct rt_dovi_list { void *const *list; size_t len; const char *error; };
struct rt_dovi {
    HMODULE mod;
    const struct rt_dovi_list *(__cdecl *generate)(const char *);
    const struct rt_dovi_data *(__cdecl *write)(void *);
    void (__cdecl *free_list)(const struct rt_dovi_list *);
    void (__cdecl *free_data)(const struct rt_dovi_data *);
};



#define BATCH 6
#define READY_CAP (BATCH * 2)
struct opts { char *dll; char *output_icc; bool gpu_float; bool fel_experiment; bool realtime_p81; bool realtime_p84; };
typedef int (__cdecl *render_fn)(const wdv_frame_v2 *, uint32_t, uint32_t,
                               uint32_t, wdv_output_v2 *, char *, uint32_t);
typedef void *(__cdecl *create_fn)(void);
typedef void *(__cdecl *create_hlg_fn)(uint32_t,uint32_t,uint32_t);
typedef void (__cdecl *destroy_fn)(void*);
typedef int (__cdecl *render3_fn)(void*,const wdv_frame_v2*,uint32_t,uint32_t,uint32_t,wdv_output_v2*,char*,uint32_t);
typedef void* (__cdecl *create_gpu6_fn)(void*,uint32_t,uint32_t,uint32_t,uint32_t);
typedef void* (__cdecl *create_gpu_fn)(void*,uint32_t,uint32_t,uint32_t);
typedef int (__cdecl *render_gpu_fn)(void*,const wdv_frame_v2*,const wdv_gpu_frame_v5*,uint32_t,uint32_t,uint32_t,wdv_output_v2*,char*,uint32_t);
typedef int (__cdecl *render_fel_fn)(void*,const wdv_frame_v2*,const wdv_gpu_frame_v5*,const wdv_fel_frame_v10*,uint32_t,uint32_t,uint32_t,wdv_output_v2*,char*,uint32_t);
typedef int (__cdecl *profile_fn)(const uint8_t*,size_t,int);
typedef int (__cdecl *controls_fn)(void*,int,int);
struct priv {
    controls_fn controls;
    bool el_off,rpu_off,job_el_off,job_rpu_off,applied_el_off,applied_rpu_off;
    unsigned long long rpu_forwarded;
    profile_fn classify;create_gpu6_fn create_profile,create_sdr12;unsigned source_profile;
    bool job_native_sdr,gpu_native_sdr;

    render_fel_fn render_fel;wdv_fel_frame_v10 fel[BATCH];bool job_reconstructed;
    create_gpu6_fn create_gpu9, create_gpu7, create_gpu6; bool job_float, job_relative; unsigned gpu_transfer;
    create_gpu_fn create_gpu;render_gpu_fn render_gpu;void*gpu_session;
    AVBufferRef *gpu_pool;ID3D11Device *gpu_device;AVRational gpu_fps;unsigned gpu_chroma;
    wdv_gpu_frame_v5 gpu[BATCH];bool job_gpu;unsigned long long gpu_frames;
    struct opts *opts;
    HMODULE dll;
    AVBufferRef *output_icc;
    char *profile_path;
    render_fn render;
    render3_fn render3; destroy_fn destroy3; void *session;
    create_hlg_fn create_hlg; void *hlg_session; bool job_hlg; AVRational hlg_fps; unsigned hlg_chroma;
    struct mp_image *pending[BATCH], *ready[READY_CAP];
    int count, ready_count, ready_pos;
    bool eof, failed, discard, job_pending;
    HANDLE worker;
    LONG done;
    int rc, job_count, job_w, job_h;
    double render_ms;
    double seek_pts;
    struct mp_image *seek_tail;
    unsigned long long seek_skipped;
    bool job_ok, job_rpu, job_fel;
    wdv_frame_v2 input[BATCH];
    wdv_output_v2 output[BATCH];
    struct mp_image *cpu[BATCH], *rgb[BATCH];
    char error[1024], last_error[1024];
    const char *state;
    unsigned long long rpu_seen, processed_frames;
    struct rt_dovi rt_api;
    struct rtpq_lut rt_lut;
    struct rtpq_state rt_state,rt84_state;
    struct rt84_gpu rt84_gpu;
    struct { uint32_t key; AVBufferRef *nal; } rt84_cache[P84_CACHE_CAP];
    unsigned long long rt84_hits, rt84_serialized;
    unsigned long long rt84_frames,rt84_cuts;
    double rt84_ms;
    bool rt84_failed;
    AVBufferRef *rt_rpu[BATCH];
    unsigned long long rt_epoch, rt_frames, rt_cuts;
    double rt_last_ms;
    bool paired_el;
    enum pl_color_transfer source_transfer;
};

// Optional verified renderer-output profile, NOT a second output transform.
// Some Windows Dolby/OEM chains already return display-referred RGB. Describing
// those pixels as sRGB and converting to that display again desaturates them.
// The sidecar is deliberately local to the renderer DLL; never infer a profile
// from the film, and never attach it to fallback HDR/BL/EL frames.
static bool load_output_profile(struct mp_filter *f)
{
    struct priv *p=f->priv;
    const char *name=p->opts->output_icc;
    bool automatic=!name || !name[0] || !strcmp(name,"auto");
    if(name && !strcmp(name,"none"))return true;
    if(automatic){
        const char *dll=p->opts->dll;
        if(!dll)return true;
        const char *slash=strrchr(dll,'/'),*back=strrchr(dll,'\\');
        if(back && (!slash || back>slash))slash=back;
        if(!slash)return true;
        name=talloc_asprintf(p,"%.*s/windows-dv-output.icc",(int)(slash-dll),dll);
    }
    if(strlen(name)<3 || name[1]!=':' || (name[2]!='/' && name[2]!='\\')){
        MP_ERR(f,"output ICC must be an absolute local path\n");return false;
    }
    FILE *fp=fopen(name,"rb");
    if(!fp){if(automatic)return true;MP_ERR(f,"cannot open explicit output ICC\n");return false;}
    bool ok=false;long size=0;AVBufferRef *buf=NULL;
    if(fseek(fp,0,SEEK_END)==0 && (size=ftell(fp))>=128 && size<=1024*1024 && fseek(fp,0,SEEK_SET)==0){
        buf=av_buffer_alloc(size);
        if(buf && fread(buf->data,1,size,fp)==(size_t)size &&
           !memcmp(buf->data+36,"acsp",4) && !memcmp(buf->data+16,"RGB ",4) &&
           !memcmp(buf->data+12,"mntr",4)){
            uint32_t declared=((uint32_t)buf->data[0]<<24)|((uint32_t)buf->data[1]<<16)|((uint32_t)buf->data[2]<<8)|buf->data[3];
            ok=declared==(uint32_t)size;
        }
    }
    fclose(fp);
    if(!ok){av_buffer_unref(&buf);MP_ERR(f,"invalid RGB display ICC; refusing mislabeled Windows output\n");return false;}
    p->output_icc=buf;p->profile_path=talloc_strdup(p,name);
    MP_INFO(f,"Windows output carries its verified device ICC: %s\n",name);
    return true;
}

static AVBufferRef *raw_rpu(struct mp_image *img)
{
    for (int i = 0; i < img->num_ff_side_data; i++)
        if (img->ff_side_data[i].type == AV_FRAME_DATA_DOVI_RPU_BUFFER)
            return img->ff_side_data[i].buf;
    return NULL;
}

static struct mp_image *p010(struct mp_image *img)
{
    struct mp_image *sw = img->hwctx ? mp_image_hw_download(img, NULL)
                                    : mp_image_new_ref(img);
    if (!sw) return NULL;
    if (sw->imgfmt == IMGFMT_P010) return sw;
    struct mp_image *out = mp_image_alloc(IMGFMT_P010, img->w, img->h);
    struct SwsContext *s = sws_getContext(img->w, img->h, imgfmt2pixfmt(sw->imgfmt),
        img->w, img->h, AV_PIX_FMT_P010LE, SWS_POINT, NULL, NULL, NULL);
    if (!out || !s) { talloc_free(out); out = NULL; }
    else if (sws_scale(s, (const uint8_t *const *)sw->planes, sw->stride, 0,
                      img->h, out->planes, out->stride) != img->h) {
        talloc_free(out); out = NULL;
    }
    sws_freeContext(s);
    talloc_free(sw);
    return out;
}

static bool rtpq_load(struct rt_dovi *d, HMODULE bridge)
{
    if(!bridge) return false;
    wchar_t path[32768];
    DWORD n=GetModuleFileNameW(bridge,path,MP_ARRAY_SIZE(path));
    if(!n || n>=MP_ARRAY_SIZE(path)) return false;
    wchar_t *end=wcsrchr(path,L'\\');
    if(!end || end+MP_ARRAY_SIZE(L"\\libdovi.dll")>=path+MP_ARRAY_SIZE(path)) return false;
    wcscpy(end,L"\\libdovi.dll");
    d->mod=LoadLibraryExW(path,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!d->mod) return false;
    d->generate=(void*)GetProcAddress(d->mod,"dovi_generate_from_json");
    d->write=(void*)GetProcAddress(d->mod,"dovi_write_unspec62_nalu");
    d->free_list=(void*)GetProcAddress(d->mod,"dovi_rpu_list_free");
    d->free_data=(void*)GetProcAddress(d->mod,"dovi_data_free");
    if(d->generate && d->write && d->free_list && d->free_data) return true;
    FreeLibrary(d->mod);*d=(struct rt_dovi){0};
    return false;
}

static bool rtpq_eligible(const struct mp_image *img)
{
    return img->imgfmt==IMGFMT_D3D11 && img->params.hw_subfmt==IMGFMT_P010 && img->hwctx &&
        !img->enhancement_layer && !img->dovi && !img->params.repr.dovi && !raw_rpu((struct mp_image *)img) &&
        img->params.transfer_orig==PL_COLOR_TRC_PQ && img->params.primaries_orig==PL_COLOR_PRIM_BT_2020 &&
        img->params.sys_orig==PL_COLOR_SYSTEM_BT_2020_NC && img->params.repr.levels==PL_COLOR_LEVELS_LIMITED &&
        (img->params.chroma_location==PL_CHROMA_LEFT || img->params.chroma_location==PL_CHROMA_TOP_LEFT) &&
        img->params.color.hdr.max_luma>=100 && img->params.color.hdr.max_luma<=10000 &&
        img->pts!=MP_NOPTS_VALUE && isfinite(img->pts) && img->nominal_fps>=12 && img->nominal_fps<=120;
}

static AVBufferRef *rtpq_make(struct mp_filter *f,struct mp_image *img)
{
    struct priv *p=f->priv;
    LARGE_INTEGER started, done, freq;
    QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&started);
    struct mp_image *sw=p010(img);
    struct rtpq_frame stats;
    bool good=sw && rtpq_analyze(&p->rt_lut,(const void*)sw->planes[0],sw->stride[0],
                              (const void*)sw->planes[1],sw->stride[1],img->w,img->h,&stats);
    talloc_free(sw);
    if(!good) return NULL;
    struct rtpq_meta fields=rtpq_push(&p->rt_state,&stats,img->pts,img->nominal_fps);
    // The libdovi V40 generator uses these bounds; keep input and observed RPU consistent.
    int min=MPCLAMP(fields.min,0,12), peak=MPCLAMP(fields.peak,2081,4095);
    int avg=MPCLAMP(fields.avg,1229,peak-1);
    const struct pl_hdr_metadata *hdr=&img->params.color.hdr;
    int prim=8; // known BT.2020 default; Level9 indices are verified below.
    const struct pl_raw_primaries *pr=&hdr->prim;
    if(fabs(pr->red.x-.680)<.005 && fabs(pr->green.y-.690)<.005) prim=0; // P3-D65, as in the source SEI
    else if(fabs(pr->red.x-.708)<.005 && fabs(pr->green.y-.797)<.005) prim=2;
    char l9[128]="";
    if(prim==0 || prim==2)
        snprintf(l9,sizeof(l9),"\"default_metadata_blocks\":[{\"Level9\":{\"length\":1,\"source_primary_index\":%d}}],",prim);
    char json[1024];
    snprintf(json,sizeof(json),
        "{\"cm_version\":\"V40\",\"profile\":\"8.1\",\"length\":2,%s"
        "\"level6\":{\"max_display_mastering_luminance\":%d,\"min_display_mastering_luminance\":%d,"
        "\"max_content_light_level\":%d,\"max_frame_average_light_level\":%d},"
        "\"shots\":[{\"start\":0,\"duration\":2,\"metadata_blocks\":[{\"Level1\":"
        "{\"min_pq\":%d,\"avg_pq\":%d,\"max_pq\":%d}}]}]}",
        l9,(int)lround(hdr->max_luma),(int)lround(fmax(0,hdr->min_luma)*10000.),
        (int)lround(fmax(0,hdr->max_cll)),(int)lround(fmax(0,hdr->max_fall)),min,avg,peak);
    const struct rt_dovi_list *list=p->rt_api.generate(json);
    const struct rt_dovi_data *nal=list && list->len==2 ? p->rt_api.write(list->list[fields.cut?0:1]) : NULL;
    AVBufferRef *buf=nal && nal->len>2 ? av_buffer_alloc(nal->len) : NULL;
    if(buf) memcpy(buf->data,nal->data,nal->len);
    if(nal) p->rt_api.free_data(nal);
    if(list) p->rt_api.free_list(list);
    if(!buf) return NULL;
    QueryPerformanceCounter(&done);
    p->rt_last_ms=1000.0*(done.QuadPart-started.QuadPart)/freq.QuadPart;
    p->rt_frames++;p->rt_cuts+=fields.cut;
    MP_DBG(f,"rt-p81 gen=%llu pts=%.6f cut=%d window=%d framepeak=%d min=%d avg=%d max=%d analyze_ms=%.3f\n",
           p->rt_epoch,img->pts,fields.cut,p->rt_state.count,stats.peak,min,avg,peak,p->rt_last_ms);
    return buf;
}

/* Optional HLG branch only. The original HLG surface is handed to Windows Dolby. */
static bool rt84_eligible(struct mp_image *img)
{
    return img->imgfmt==IMGFMT_D3D11 && img->params.hw_subfmt==IMGFMT_P010 && img->hwctx &&
        !img->enhancement_layer && !img->dovi && !img->params.repr.dovi && !raw_rpu(img) &&
        img->params.transfer_orig==PL_COLOR_TRC_HLG && img->params.primaries_orig==PL_COLOR_PRIM_BT_2020 &&
        img->params.sys_orig==PL_COLOR_SYSTEM_BT_2020_NC && img->params.repr.levels==PL_COLOR_LEVELS_LIMITED &&
        (img->params.chroma_location==PL_CHROMA_LEFT || img->params.chroma_location==PL_CHROMA_TOP_LEFT) &&
        img->pts!=MP_NOPTS_VALUE && isfinite(img->pts) && img->nominal_fps>=12 && img->nominal_fps<=120;
}

static AVBufferRef *rt84_make(struct mp_filter *f,struct mp_image *img)
{
    struct priv *p=f->priv;
    LARGE_INTEGER started,done,freq;
    QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&started);
    struct rtpq_frame stats;
    if(!rt84_analyze(f,&p->rt84_gpu,img,&stats))return NULL;
    struct rtpq_meta fields=rtpq_push(&p->rt84_state,&stats,img->pts,img->nominal_fps);
    int min=MPCLAMP(fields.min,0,12),peak=MPCLAMP(fields.peak,2081,3079);
    int avg=MPCLAMP(fields.avg,1229,peak-1);
    uint32_t key=p84_key(min,avg,peak,fields.cut);
    unsigned index=p84_slot(key);
    AVBufferRef *buf=NULL;
    if(p->rt84_cache[index].key==key)
        buf=av_buffer_ref(p->rt84_cache[index].nal);
    if(buf){
        p->rt84_hits++;
    }else{
        char json[512];
        snprintf(json,sizeof(json),
            "{\"cm_version\":\"V40\",\"profile\":\"8.4\",\"length\":2,"
            "\"shots\":[{\"start\":0,\"duration\":2,\"metadata_blocks\":[{\"Level1\":"
            "{\"min_pq\":%d,\"avg_pq\":%d,\"max_pq\":%d}}]}]}",min,avg,peak);
        const struct rt_dovi_list *list=p->rt_api.generate(json);
        const struct rt_dovi_data *nal=list && list->len==2 ? p->rt_api.write(list->list[fields.cut?0:1]) : NULL;
        buf=nal && nal->len>2 ? av_buffer_alloc(nal->len) : NULL;
        if(buf)memcpy(buf->data,nal->data,nal->len);
        if(nal)p->rt_api.free_data(nal);
        if(list)p->rt_api.free_list(list);
        if(!buf)return NULL;
        av_buffer_unref(&p->rt84_cache[index].nal);
        AVBufferRef *keep=av_buffer_ref(buf);
        p->rt84_cache[index].nal=keep;
        p->rt84_cache[index].key=keep?key:0;
        p->rt84_serialized++;
    }
    QueryPerformanceCounter(&done);
    p->rt84_ms=1000.0*(done.QuadPart-started.QuadPart)/freq.QuadPart;
    p->rt84_frames++; p->rt84_cuts+=fields.cut;
    MP_DBG(f,"rt-p84 gen=%llu pts=%.6f cut=%d window=%d samples=%u framepeak=%d min=%d avg=%d max=%d analyze_ms=%.3f\n",
           p->rt_epoch,img->pts,fields.cut,p->rt84_state.count,stats.pixels,stats.peak,min,avg,peak,p->rt84_ms);
    return buf;
}

// Only the external renderer runs on this worker. MPV image download,
// ownership, pins and metadata stay on the core thread. Reset discards the
// outstanding generation without blocking menu/pause/seek commands.
static DWORD WINAPI render_worker(LPVOID context)
{
    struct mp_filter *f=context;struct priv *p=f->priv;
    LARGE_INTEGER a,b,hz;QueryPerformanceFrequency(&hz);QueryPerformanceCounter(&a);
    if(p->controls && p->job_gpu && p->controls(p->gpu_session,!p->job_el_off,!p->job_rpu_off)!=0){
        p->rc=-1;snprintf(p->error,sizeof(p->error),"Windows comparison controls rejected");
    }
    else if(p->job_reconstructed)p->rc=p->render_fel(p->gpu_session,p->input,p->gpu,p->fel,p->job_count,p->job_w,p->job_h,p->output,p->error,sizeof(p->error));
    else if(p->job_gpu)p->rc=p->render_gpu(p->gpu_session,p->input,p->gpu,p->job_count,p->job_w,p->job_h,p->output,p->error,sizeof(p->error));
    else p->rc=(p->job_hlg?p->hlg_session:p->session)?p->render3(p->job_hlg?p->hlg_session:p->session,p->input,p->job_count,p->job_w,p->job_h,p->output,p->error,sizeof(p->error)):p->render(p->input,p->job_count,p->job_w,p->job_h,p->output,p->error,sizeof(p->error));
    QueryPerformanceCounter(&b);p->render_ms=1000.0*(b.QuadPart-a.QuadPart)/hz.QuadPart;
    InterlockedExchange(&p->done,1);mp_filter_wakeup(f);return 0;
}

static void finish_batch(struct mp_filter *f)
{
    struct priv *p=f->priv;
    int w=p->job_w,h=p->job_h;
    wdv_frame_v2 *input=p->input;wdv_output_v2 *output=p->output;
    struct mp_image **cpu=p->cpu,**rgb=p->rgb;
    bool ok=p->job_ok && p->rc==p->job_count;
    bool has_rpu=p->job_rpu;
    char *error=p->error;
    for(int i=0;i<BATCH;i++)av_buffer_unref(&p->rt_rpu[i]);
    if(p->discard){
        for(int i=0;i<BATCH;i++){mp_image_unrefp(&p->pending[i]);mp_image_unrefp(&cpu[i]);mp_image_unrefp(&rgb[i]);}
        p->count=p->ready_count=p->ready_pos=0;p->eof=p->failed=p->discard=false;
        return;
    }
    // Keep ordered GPU frames ahead of presentation. A completed single-frame
    // job must not wait for the ready queue to empty before it can be published.
    int kept=p->ready_count-p->ready_pos;
    memmove(p->ready,p->ready+p->ready_pos,kept*sizeof(p->ready[0]));
    memset(p->ready+kept,0,(READY_CAP-kept)*sizeof(p->ready[0]));
    for (int i = 0; ok && i < p->count; i++) {
        if (output[i].pts != input[i].pts) {ok=false;snprintf(error,sizeof(p->error),"renderer PTS mismatch");break;}
        // A successful renderer must supply opaque pixels, including black video.
        if(p->job_gpu)continue; // GPU path validates exact PTS and successful TransferVideoFrame; no CPU pixel readback.
        size_t opaque=0;
        for(int y=0;y<h;y++)for(int x=0;x<w;x++)opaque+=rgb[i]->planes[0][y*rgb[i]->stride[0]+x*4+3]!=0;
        if(!opaque){ok=false;snprintf(error,sizeof(p->error),"renderer returned an empty/transparent surface");break;}
    }
    if (!ok && has_rpu && !p->job_fel && !p->failed) {
        MP_WARN(f, "Windows DV fallback: %s; original BL/EL and PTS retained.\n", error);
        p->failed = true;
        snprintf(p->last_error,sizeof(p->last_error),"%s",error);
    }
    for (int i=0;i<p->count;i++) {
        struct mp_image *orig=p->pending[i];
        if (ok) {
            struct mp_image *dst=rgb[i];
            mp_image_copy_attributes(dst, orig);
            av_buffer_unref(&dst->dovi); mp_image_unrefp(&dst->enhancement_layer);
            for (int k=0;k<dst->num_ff_side_data;k++) av_buffer_unref(&dst->ff_side_data[k].buf);
            dst->num_ff_side_data=0;
            dst->params.color=pl_color_space_srgb;
            // FP16 carries extended linear RGB (1.0 = 80 nit), not panel RGB.
            // Keep negative/over-one values until gpu-next's gamut/tone mapping.
            if(p->job_float && !p->job_relative)dst->params.color=(struct pl_color_space){
                .primaries=PL_COLOR_PRIM_BT_709,.transfer=PL_COLOR_TRC_SCRGB};
            av_buffer_unref(&dst->icc_profile);
            if(p->output_icc && !p->job_float)dst->icc_profile=av_buffer_ref(p->output_icc);
            // MediaEngine returns a complete opaque video image, not a compositing layer.
            // Its incidental alpha (e.g. the Dolby notification rectangle) is not coverage.
            // Keep RGB and the empty-surface guard unchanged; do not affect MPV OSD alpha.
            dst->params.repr=(struct pl_color_repr){.sys=PL_COLOR_SYSTEM_RGB,
                .levels=PL_COLOR_LEVELS_FULL,.alpha=PL_ALPHA_NONE};
            dst->params.primaries_orig=PL_COLOR_PRIM_BT_709;
            dst->params.transfer_orig=p->job_float && !p->job_relative?PL_COLOR_TRC_SCRGB:PL_COLOR_TRC_SRGB; dst->params.sys_orig=PL_COLOR_SYSTEM_RGB;
            dst->params.no_dovi=true; dst->params.no_enhancement_layer=true;
            p->ready[kept+i]=dst; rgb[i]=NULL; talloc_free(orig);
        } else p->ready[kept+i]=orig;
        mp_image_unrefp(&cpu[i]); mp_image_unrefp(&rgb[i]); p->pending[i]=NULL;
    }
    p->state=p->job_fel?"fel-gpu-next":ok?"processed":(has_rpu||p->failed?"fallback":"passthrough");
    if(p->job_fel){p->failed=false;p->last_error[0]=0;}
    if(ok){p->applied_el_off=p->job_el_off;p->applied_rpu_off=p->job_rpu_off;if(!p->job_rpu_off)p->rpu_forwarded+=p->count;if(p->job_gpu)p->gpu_frames+=p->count;p->processed_frames+=p->count;p->failed=false;p->last_error[0]=0;}
    if (ok) MP_DBG(f,"Windows DV processed %d paired frames %.6f..%.6f in MPV; output %s.\n",
                   p->count,p->ready[kept]->pts,p->ready[kept+p->count-1]->pts,
                   p->job_native_sdr?"native SDR RGB10":p->job_relative?"relative sRGB FP16":p->job_float?"FP16 scRGB":"SDR BGRA");
    p->ready_count=kept+p->count; p->ready_pos=0; p->count=0;
}

static void begin_batch(struct mp_filter *f)
{
    struct priv *p = f->priv;
    wdv_frame_v2 *input=p->input; wdv_output_v2 *output=p->output;
    struct mp_image **cpu=p->cpu, **rgb=p->rgb;
    memset(input,0,sizeof(p->input));memset(output,0,sizeof(p->output));
    snprintf(p->error,sizeof(p->error),"unsupported frame; original BL/EL retained");
    bool ok = p->render || p->session;
    int w = p->pending[0]->w, h = p->pending[0]->h;
    p->job_el_off=p->el_off;p->job_rpu_off=p->rpu_off;
    bool has_rpu = false; p->job_fel=false;p->job_reconstructed=false;memset(p->fel,0,sizeof(p->fel));
    struct mp_image *first=p->pending[0];
    if(p->rt_api.mod && p->opts->realtime_p81){
        bool eligible=true;
        for(int i=0;i<p->count;i++) eligible &= rtpq_eligible(p->pending[i]);
        if(eligible){
            for(int i=0;i<p->count;i++){
                p->rt_rpu[i]=rtpq_make(f,p->pending[i]);
                if(!p->rt_rpu[i]){
                    eligible=false;
                    MP_WARN(f,"Realtime P8.1 metadata unavailable; keeping original HDR10.\n");
                    break;
                }
            }
        }
        if(!eligible){
            p->rt_state=(struct rtpq_state){0};
            for(int i=0;i<p->count;i++)av_buffer_unref(&p->rt_rpu[i]);
        }
    }
    if(p->rt_api.mod && p->opts->realtime_p84 && !p->rt84_failed &&
       first->params.transfer_orig==PL_COLOR_TRC_HLG){
        bool eligible=true;
        for(int i=0;i<p->count;i++)eligible &= rt84_eligible(p->pending[i]);
        if(eligible){
            for(int i=0;i<p->count;i++){
                p->rt_rpu[i]=rt84_make(f,p->pending[i]);
                if(!p->rt_rpu[i]){
                    eligible=false;p->rt84_failed=true;
                    MP_WARN(f,"Realtime P8.4 analysis/generation unavailable; keeping original HLG.\n");
                    break;
                }
            }
        }
        if(!eligible){
            p->rt84_state=(struct rtpq_state){0};
            for(int i=0;i<p->count;i++)av_buffer_unref(&p->rt_rpu[i]);
        }
    }
    AVBufferRef *first_rpu=raw_rpu(first);
    if(!first_rpu) first_rpu=p->rt_rpu[0];
    p->source_profile=p->classify&&first_rpu?p->classify(first_rpu->data,first_rpu->size,1):0;
    bool p5=p->source_profile==5;
    bool p8=p->source_profile==8 && !first->enhancement_layer;
    // v6 selects the actual BL transfer; v5 is HLG-only and must never receive PQ.
    unsigned transfer=p5 ? 3 : first->params.transfer_orig==PL_COLOR_TRC_HLG ? 2 :
                      first->params.transfer_orig==PL_COLOR_TRC_PQ ? 1 : 0;
    p->job_gpu=p->render_gpu && (p->create_gpu6 || (p->create_gpu && transfer==2)) &&
        first->imgfmt==IMGFMT_D3D11 && first->params.hw_subfmt==IMGFMT_P010 &&
        first->hwctx && transfer && first->nominal_fps>0;
    // Native SDR10 uses FP16 transport without re-encoding. P7 remains legacy.
    p->job_native_sdr=p->job_gpu && ((p5 && p->create_profile) || (p8 && p->create_sdr12));
    p->job_float=p->job_gpu && ((p8 && p->create_sdr12) || (p5 && p->create_profile) || (transfer==2 && p->opts->gpu_float && (p->create_gpu9 || p->create_gpu7)));
    p->job_relative=p->job_native_sdr || (p->job_float && (p5?p->create_profile!=NULL:p->create_gpu9!=NULL));
    if(p->job_gpu){
        AVHWFramesContext *fc=(void*)first->hwctx->data;
        AVHWDeviceContext *dc=(void*)fc->device_ref->data;
        AVD3D11VADeviceContext *dd=dc->hwctx;
        AVRational fps=av_d2q(first->nominal_fps,100000);unsigned chroma=first->params.chroma_location==PL_CHROMA_TOP_LEFT;
        if(p->gpu_session && (p->gpu_device!=dd->device || av_cmp_q(fps,p->gpu_fps) || chroma!=p->gpu_chroma || transfer!=p->gpu_transfer || p->job_native_sdr!=p->gpu_native_sdr)){p->destroy3(p->gpu_session);p->gpu_session=NULL;}
        if(!p->gpu_session){p->gpu_device=dd->device;p->gpu_fps=fps;p->gpu_chroma=chroma;p->gpu_transfer=transfer;p->gpu_native_sdr=p->job_native_sdr;p->gpu_session=p8 && p->create_sdr12 ? p->create_sdr12(dd->device,fps.num,fps.den,chroma,transfer) : p5 ? (p->create_profile?p->create_profile(dd->device,fps.num,fps.den,chroma,5):NULL) : p->job_relative ? p->create_gpu9(dd->device,fps.num,fps.den,chroma,transfer) : p->job_float ? p->create_gpu7(dd->device,fps.num,fps.den,chroma,transfer) : p->create_gpu6 ? p->create_gpu6(dd->device,fps.num,fps.den,chroma,transfer) : p->create_gpu(dd->device,fps.num,fps.den,chroma);}
        ok=p->gpu_session && mp_update_av_hw_frames_pool(&p->gpu_pool,fc->device_ref,
            IMGFMT_D3D11,p->job_float?IMGFMT_RGBAF16:IMGFMT_BGRA,w,h,false);
        if(!ok)snprintf(p->error,sizeof(p->error),"GPU Windows DV session/pool unavailable");
    }

    for (int i = 0; ok && i < p->count; i++) {
        struct mp_image *img = p->pending[i], *el = img->enhancement_layer;
        AVBufferRef *rpu = el ? raw_rpu(el) : NULL;
        if(!rpu)rpu=raw_rpu(img); // Interleaved streams can retain raw RPU on BL.
        if(!rpu)rpu=p->rt_rpu[i];
        p->paired_el|=!!el;
        // Original BL transfer describes raw P010, unlike effective DV/PQ tags.
        p->source_transfer = img->params.transfer_orig;
        has_rpu |= !!rpu; if(rpu){p->rpu_seen++;p->paired_el|=!!el;}
        bool hlg=img->params.transfer_orig==PL_COLOR_TRC_HLG;
        if (i==0) p->job_hlg=hlg;
#if PL_API_VER >= 370
        if (img->params.repr.dovi && img->params.repr.dovi->nlq_active) {
            if(!p->opts->fel_experiment || !p->render_fel || !p->job_gpu || !el ||
               el->imgfmt!=IMGFMT_D3D11 || el->params.hw_subfmt!=IMGFMT_P010 || !el->hwctx) {
                p->job_fel=!!el;has_rpu=true;
                snprintf(p->error,sizeof(p->error),"FEL experiment unavailable: EL fmt=%d subfmt=%d; original BL/EL retained",el?el->imgfmt:0,el?el->params.hw_subfmt:0);
                MP_WARN(f,"%s\n",p->error);ok=false;break;
            }
            p->job_reconstructed=true;
            p->fel[i]=(wdv_fel_frame_v10){el->planes[0],(uintptr_t)el->planes[1],el->w,el->h,
                &img->params.repr,&el->params.repr,img->params.chroma_location,el->params.chroma_location,PL_API_VER};
        } else if(p->job_reconstructed) {ok=false;break;}
#endif
        if(p5 && (!p->job_gpu || el || img->params.repr.levels!=PL_COLOR_LEVELS_FULL ||
            img->params.chroma_location!=PL_CHROMA_LEFT || !p->classify || p->classify(rpu?rpu->data:NULL,rpu?rpu->size:0,1)!=5)){
            snprintf(p->error,sizeof(p->error),"P5 requires full-range raw P010, center-left chroma, original P5 RPU and no EL");ok=false;break;
        }
        if (rpu && !p5 && p->job_gpu &&
            (img->params.primaries_orig!=PL_COLOR_PRIM_BT_2020 ||
             img->params.sys_orig!=PL_COLOR_SYSTEM_BT_2020_NC ||
             img->params.repr.levels!=PL_COLOR_LEVELS_LIMITED ||
             (img->params.chroma_location!=PL_CHROMA_LEFT && img->params.chroma_location!=PL_CHROMA_TOP_LEFT))) {
            snprintf(p->error,sizeof(p->error),"GPU Windows requires BT2020 NCL limited PQ/HLG input with known chroma siting");ok=false;break;
        }
        if (rpu && hlg) {
            if (!p->create_hlg || !p->render3 || !p->destroy3 || el ||
                img->params.primaries_orig!=PL_COLOR_PRIM_BT_2020 ||
                img->params.sys_orig!=PL_COLOR_SYSTEM_BT_2020_NC ||
                img->params.repr.levels!=PL_COLOR_LEVELS_LIMITED ||
                (img->params.chroma_location!=PL_CHROMA_LEFT && img->params.chroma_location!=PL_CHROMA_TOP_LEFT) || !(img->nominal_fps>0)) {
                snprintf(p->error,sizeof(p->error),"HLG Windows v4 requires P8 BT2020 NCL limited BL and valid frame rate");ok=false;break;
            }
            unsigned chroma=img->params.chroma_location==PL_CHROMA_TOP_LEFT;
            AVRational fps=av_d2q(img->nominal_fps,100000);
            if(p->hlg_session && (av_cmp_q(fps,p->hlg_fps) || chroma!=p->hlg_chroma)) {p->destroy3(p->hlg_session);p->hlg_session=NULL;}
            if(!p->job_gpu && !p->hlg_session){p->hlg_fps=fps;p->hlg_chroma=chroma;p->hlg_session=p->create_hlg(fps.num,fps.den,chroma);}
            if(!p->job_gpu && !p->hlg_session){snprintf(p->error,sizeof(p->error),"HLG session creation failed");ok=false;break;}
        } else if (rpu && !p5 && img->params.transfer_orig!=PL_COLOR_TRC_PQ) {
            snprintf(p->error,sizeof(p->error),"unsupported original BL transfer");ok=false;break;
        }
        if(hlg!=p->job_hlg){snprintf(p->error,sizeof(p->error),"mixed BL transfer batch");ok=false;break;}
        if (!rpu || img->pts == MP_NOPTS_VALUE || !isfinite(img->pts) ||
            img->w != w || img->h != h || w > 3840 || h > 2160 ||
            (el && (el->pts == MP_NOPTS_VALUE || fabs(el->pts-img->pts)>1e-6))) { ok = false; break; }
        if(p->job_gpu){
            if(img->imgfmt!=IMGFMT_D3D11 || !img->hwctx){ok=false;break;}
            AVFrame *af=av_frame_alloc();
            if(af && av_hwframe_get_buffer(p->gpu_pool,af,0)>=0)rgb[i]=mp_image_from_av_frame(af);
            av_frame_free(&af);
            if(rgb[i])p->gpu[i]=(wdv_gpu_frame_v5){img->planes[0],(uintptr_t)img->planes[1],rgb[i]->planes[0]};
        }else{cpu[i] = p010(img); rgb[i] = mp_image_alloc(IMGFMT_BGRA, w, h);}
        if ((!p->job_gpu && !cpu[i]) || !rgb[i]) { MP_WARN(f,"download/allocation failed cpu=%d rgb=%d\n",!!cpu[i],!!rgb[i]); ok = false; break; }
        double duration = img->pkt_duration;
        if (!(duration > 0)) duration = img->nominal_fps > 0 ? 1/img->nominal_fps : 1001.0/24000;
        input[i] = (wdv_frame_v2){
            .pts = llround(img->pts*10000000), .duration = llround(duration*10000000),
            .y = p->job_gpu?NULL:cpu[i]->planes[0], .uv = p->job_gpu?NULL:cpu[i]->planes[1],
            .stride_y = p->job_gpu?0:cpu[i]->stride[0], .stride_uv = p->job_gpu?0:cpu[i]->stride[1],
            .rpu = rpu->data, .rpu_len = rpu->size, .escaped = 1, .paired_el = !!el,
        };
        output[i] = (wdv_output_v2){.pixels=p->job_gpu?NULL:rgb[i]->planes[0], .stride=p->job_gpu?0:rgb[i]->stride[0]};
    }
    p->job_count=p->count;p->job_w=w;p->job_h=h;p->job_rpu=has_rpu;p->job_ok=ok;
    p->rc=-1;p->job_pending=true;InterlockedExchange(&p->done,0);
    if(ok){
        p->state="processing";InterlockedExchange(&p->done,0);
        p->worker=CreateThread(NULL,0,render_worker,f,0,NULL);
        if(p->worker)return;
        snprintf(p->error,sizeof(p->error),"Windows worker creation failed");
    }
    InterlockedExchange(&p->done,1);mp_filter_internal_mark_progress(f);
}

// Decoding pre-roll is still necessary for HEVC references and RPU/EL pairing.
// Rendering those invisible frames through MediaEngine is not. Keep only the
// latest one so seeking beyond EOF can still display a correctly rendered tail.
static bool hold_seek_preroll(struct priv *p, struct mp_image *img)
{
    if (p->seek_pts == MP_NOPTS_VALUE) return false;
    if (img->pts != MP_NOPTS_VALUE && img->pts < p->seek_pts) {
        mp_image_unrefp(&p->seek_tail);
        p->seek_tail=img;
        p->seek_skipped++;
        return true;
    }
    p->seek_pts=MP_NOPTS_VALUE;
    mp_image_unrefp(&p->seek_tail);
    return false;
}

static void process(struct mp_filter *f)
{
    struct priv *p=f->priv;
    // GPU work is single-frame after priming, with up to READY_CAP ordered output
    // prefetch. This absorbs bounded MediaEngine transfer stalls on resume.
    // CPU fallback keeps its original batch/drain ordering. No timestamps are changed.
    int buffered=p->ready_count-p->ready_pos;
    bool room=p->job_gpu ? buffered+p->job_count<=READY_CAP : buffered==0;
    if(p->job_pending&&room&&InterlockedCompareExchange(&p->done,0,0)){
        if(p->worker){WaitForSingleObject(p->worker,INFINITE);CloseHandle(p->worker);p->worker=NULL;}
        p->job_pending=false;finish_batch(f);
    }
    if(mp_pin_in_needs_data(f->ppins[1])&&p->ready_pos<p->ready_count){
        struct mp_image *img=p->ready[p->ready_pos];p->ready[p->ready_pos++]=NULL;
        mp_pin_in_write(f->ppins[1],MAKE_FRAME(MP_FRAME_VIDEO,img));
    }
    // Bound the next batch by actual free slots, not by a worst-case batch
    // reservation. Steady GPU work submits one frame; reserving six wasted five
    // ready slots and exposed short MediaEngine stalls after resume.
    int capacity=MPMIN(BATCH,READY_CAP-(p->ready_count-p->ready_pos));
    if(!p->job_pending && capacity>0){
        while(!p->eof&&p->count<capacity){
            if(!mp_pin_out_request_data(f->ppins[0]))return;
            struct mp_frame fr=mp_pin_out_read(f->ppins[0]);
            if(fr.type==MP_FRAME_EOF){
                p->eof=true;
                if(p->seek_tail){p->pending[p->count++]=p->seek_tail;p->seek_tail=NULL;}
                p->seek_pts=MP_NOPTS_VALUE;
                break;
            }
            if(fr.type!=MP_FRAME_VIDEO){mp_frame_unref(&fr);continue;}
            if(hold_seek_preroll(p,fr.data))continue;
            p->pending[p->count++]=fr.data;
            // Prime MediaEngine with the original batch, then bound HLG main-thread
            // download work to one frame so pause/menu commands are not delayed.
            if(p->processed_frames && (p->job_gpu || (p->hlg_session &&
               p->pending[p->count-1]->params.transfer_orig==PL_COLOR_TRC_HLG)))break;
        }
        if(p->count)begin_batch(f);
    }
    if(!p->job_pending&&!p->count&&p->ready_pos>=p->ready_count&&p->eof&&mp_pin_in_needs_data(f->ppins[1]))mp_pin_in_write(f->ppins[1],MP_EOF_FRAME);
}
static void reset(struct mp_filter *f)
{
    struct priv *p=f->priv;
    p->seek_pts=MP_NOPTS_VALUE;p->seek_skipped=0;mp_image_unrefp(&p->seek_tail);
    p->rpu_seen=p->rpu_forwarded=p->processed_frames=p->gpu_frames=0;p->paired_el=false;p->state="waiting";p->last_error[0]=0;
    p->rt_state=(struct rtpq_state){0};
    p->rt_epoch++;p->rt_frames=p->rt_cuts=0;p->rt_last_ms=0;
    p->rt84_state=(struct rtpq_state){0};
    p->rt84_frames=p->rt84_cuts=0;p->rt84_ms=0;
    p->rt84_hits=p->rt84_serialized=0;
    for(int i=0;i<P84_CACHE_CAP;i++){
        av_buffer_unref(&p->rt84_cache[i].nal);
        p->rt84_cache[i].key=0;
    }
    p->source_transfer=PL_COLOR_TRC_UNKNOWN;
    for(int i=0;i<READY_CAP;i++)mp_image_unrefp(&p->ready[i]);
    p->ready_count=p->ready_pos=0;
    if(p->job_pending){p->discard=true;return;}
    for(int i=0;i<BATCH;i++){mp_image_unrefp(&p->pending[i]);mp_image_unrefp(&p->ready[i]);mp_image_unrefp(&p->cpu[i]);mp_image_unrefp(&p->rgb[i]);}
    p->count=p->ready_count=p->ready_pos=0;p->eof=p->failed=p->discard=false;
}
static void destroy(struct mp_filter *f)
{
    struct priv *p=f->priv;
    if(p->worker){WaitForSingleObject(p->worker,INFINITE);CloseHandle(p->worker);p->worker=NULL;}
    rt84_destroy(&p->rt84_gpu);
    if(p->rt_api.mod)FreeLibrary(p->rt_api.mod);
    for(int i=0;i<BATCH;i++)av_buffer_unref(&p->rt_rpu[i]);
    p->job_pending=false;reset(f);if(p->gpu_session)p->destroy3(p->gpu_session);av_buffer_unref(&p->gpu_pool);av_buffer_unref(&p->output_icc);if(p->hlg_session)p->destroy3(p->hlg_session);if(p->session)p->destroy3(p->session);if(p->dll)FreeLibrary(p->dll);
}
static bool command(struct mp_filter *f,struct mp_filter_command *cmd)
{
    struct priv *p=f->priv;
    if(cmd->type==MP_FILTER_COMMAND_SET_SEEK_TARGET){p->seek_pts=cmd->seek_pts;return true;}
    if(cmd->type==MP_FILTER_COMMAND_TEXT){
        if(!p->controls || !p->job_gpu || !cmd->arg)return false;
        bool off=!strcmp(cmd->arg,"no");
        if(!off && strcmp(cmd->arg,"yes"))return false;
        if(!strcmp(cmd->cmd,"el")){p->el_off=off;return true;}
        if(!strcmp(cmd->cmd,"rpu")){
            // P5 has no independently displayable HDR base layer. Never feed its
            // IPT-PQ-C2 codes to a metadata-free BT.2020/YUV interpretation.
            if(off && p->source_profile==5)return false;
            p->rpu_off=off;return true;
        }
        return false;
    }
    if(cmd->type!=MP_FILTER_COMMAND_GET_META)return false;
    struct mp_tags *t=talloc_zero(NULL,struct mp_tags);
    mp_tags_set_str(t,"comparison-controls",p->controls && p->job_gpu?"yes":"no");
    mp_tags_set_str(t,"el-requested",p->el_off?"off":"on");
    mp_tags_set_str(t,"rpu-requested",p->rpu_off?"off":"on");
    mp_tags_set_str(t,"el-contribution",p->applied_el_off?"off":"on");
    mp_tags_set_str(t,"rpu-display-contribution",p->applied_rpu_off?"off":"on");
    mp_tags_set_str(t,"rpu-forwarded",mp_tprintf(64,"%llu",p->rpu_forwarded));
    mp_tags_set_str(t,"rpu-disable-supported",p->source_profile!=5 && p->controls && p->job_gpu?"yes":"no");
    mp_tags_set_str(t,"seek-preroll-skipped",mp_tprintf(64,"%llu",p->seek_skipped));
    mp_tags_set_str(t,"output-icc",p->job_relative?"none (Windows display-relative)":p->job_float?"none (scRGB)":p->profile_path?p->profile_path:"none");
    mp_tags_set_str(t,"state",p->state?p->state:"waiting");
    mp_tags_set_str(t,"realtime-p81",p->rt_api.mod && p->opts->realtime_p81?"yes":"no");
    mp_tags_set_str(t,"realtime-p84",p->rt_api.mod && p->opts->realtime_p84 && !p->rt84_failed?"yes":"no");
    mp_tags_set_str(t,"rt84-generated",mp_tprintf(64,"%llu",p->rt84_frames));
    mp_tags_set_str(t,"rt84-cache-hits",mp_tprintf(64,"%llu",p->rt84_hits));
    mp_tags_set_str(t,"rt84-serializations",mp_tprintf(64,"%llu",p->rt84_serialized));
    mp_tags_set_str(t,"rt84-scenes",mp_tprintf(64,"%llu",p->rt84_cuts));
    mp_tags_set_str(t,"rt84-analysis-ms",mp_tprintf(64,"%.3f",p->rt84_ms));
    mp_tags_set_str(t,"rtpq-epoch",mp_tprintf(64,"%llu",p->rt_epoch));
    mp_tags_set_str(t,"rtpq-generated",mp_tprintf(64,"%llu",p->rt_frames));
    mp_tags_set_str(t,"rtpq-scenes",mp_tprintf(64,"%llu",p->rt_cuts));
    mp_tags_set_str(t,"rtpq-analysis-ms",mp_tprintf(64,"%.3f",p->rt_last_ms));
    mp_tags_set_str(t,"rpu-seen",mp_tprintf(64,"%llu",p->rpu_seen));
    mp_tags_set_str(t,"processed-frames",mp_tprintf(64,"%llu",p->processed_frames));
    mp_tags_set_str(t,"paired-el",p->paired_el?"yes":"no");
    mp_tags_set_str(t,"enhancement-path",p->job_reconstructed && !strcmp(p->state,"processed")?"gpu-reconstructed-fel-10bit-windows":p->job_fel?"gpu-next-fel":"unchanged");
    mp_tags_set_str(t,"bridge-available",p->render?"yes":"no");
    mp_tags_set_str(t,"fallback-reason",p->last_error);
    mp_tags_set_str(t,"source-profile",mp_tprintf(32,"%u",p->source_profile));
    mp_tags_set_str(t,"source-base-transfer",p->source_profile==5 ? "ipt-pq-c2" :p->source_transfer == PL_COLOR_TRC_HLG ? "hlg" :
        p->source_transfer == PL_COLOR_TRC_PQ ? "pq" : "other-or-unknown");
    mp_tags_set_str(t,"windows-input-transfer",p->source_profile==5?"P5 VUI unspecified":p->job_hlg?"HLG":"PQ");
    mp_tags_set_str(t,"output",p->job_native_sdr?"Windows SDR10 -> FP16 transport":p->job_relative?"Windows relative-linear -> sRGB FP16":p->job_float?"scRGB FP16 (display mapping follows)":"SDR");
    mp_tags_set_str(t,"output-contract",p->job_native_sdr?"windows-sdr10-fp16-transport":p->job_relative?"display-relative-srgb-fp16":p->job_float?"scrgb-fp16":"legacy-sdr");
    mp_tags_set_str(t,"frame-transport",p->job_gpu?"d3d11-gpu":"cpu-p010-bgra");
    mp_tags_set_str(t,"gpu-frames",mp_tprintf(64,"%llu",p->gpu_frames));
    mp_tags_set_str(t,"last-render-ms",mp_tprintf(64,"%.3f",p->render_ms));
    mp_tags_set_str(t,"session-mode",p->session?"persistent":"legacy-batch");
    *(struct mp_tags **)cmd->res=t;return true;
}
static const struct mp_filter_info info={.name="windows-dv",.priv_size=sizeof(struct priv),
    .process=process,.reset=reset,.destroy=destroy,.command=command};
static struct mp_filter *create(struct mp_filter *parent,void *options)
{
    struct mp_filter *f=mp_filter_create(parent,&info);if(!f){talloc_free(options);return NULL;}
    struct priv *p=f->priv;p->opts=talloc_steal(p,options);p->seek_pts=MP_NOPTS_VALUE;
    if(!load_output_profile(f)){talloc_free(f);return NULL;}
    mp_filter_add_pin(f,MP_PIN_IN,"in");mp_filter_add_pin(f,MP_PIN_OUT,"out");
    const char *path=p->opts->dll;
    if(path&&strlen(path)>3&&path[1]==':'&&(path[2]=='/'||path[2]=='\\')){
        wchar_t *wide=mp_from_utf8(NULL,path);
        p->dll=LoadLibraryExW(wide,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        talloc_free(wide);
        if(p->dll)p->render=(render_fn)GetProcAddress(p->dll,"wdv_render_batch_v2");
    }
    if(p->dll){
        p->controls=(controls_fn)GetProcAddress(p->dll,"wdv_set_comparison_v14");
        p->render_fel=(render_fel_fn)GetProcAddress(p->dll,"wdv_render_gpu_fel_v10");
        p->classify=(profile_fn)GetProcAddress(p->dll,"wdv_rpu_profile_v11");
        p->create_sdr12=(create_gpu6_fn)GetProcAddress(p->dll,"wdv_create_gpu_sdr_v12");
        p->create_profile=(create_gpu6_fn)GetProcAddress(p->dll,"wdv_create_gpu_profile_v11");
        p->create_gpu9=(create_gpu6_fn)GetProcAddress(p->dll,"wdv_create_gpu_v9");
        p->create_gpu7=(create_gpu6_fn)GetProcAddress(p->dll,"wdv_create_gpu_v7");
        p->create_gpu6=(create_gpu6_fn)GetProcAddress(p->dll,"wdv_create_gpu_v6");
        p->create_gpu=(create_gpu_fn)GetProcAddress(p->dll,"wdv_create_gpu_v5");
        p->render_gpu=(render_gpu_fn)GetProcAddress(p->dll,"wdv_render_gpu_v5");
        p->create_hlg=(create_hlg_fn)GetProcAddress(p->dll,"wdv_create_hlg_v4");
        create_fn create3=(create_fn)GetProcAddress(p->dll,"wdv_create_v3");
        p->render3=(render3_fn)GetProcAddress(p->dll,"wdv_render_v3");
        p->destroy3=(destroy_fn)GetProcAddress(p->dll,"wdv_destroy_v3");
        if(create3&&p->render3&&p->destroy3)p->session=create3();
    }
    if(p->opts->realtime_p81 || p->opts->realtime_p84){
        if(rtpq_load(&p->rt_api,p->dll)){
            if(p->opts->realtime_p81)rtpq_lut_init(&p->rt_lut);
        }else MP_WARN(f,"Realtime DV generator unavailable; original video path retained.\n");
    }
    p->state=p->render?"waiting":"unavailable";
    if(!p->render)MP_WARN(f,"Windows DV bridge unavailable; original MPV path retained.\n");
    return f;
}
#define OPT_BASE_STRUCT struct opts
const struct mp_user_filter_entry vf_windows_dv={
    .desc={.name="windows-dv",.description="Experimental Windows Dolby processing in MPV window",
        .priv_size=sizeof(struct opts),.options=(const m_option_t[]){{"dll",OPT_STRING(dll)},{"output-icc",OPT_STRING(output_icc)},{"gpu-float",OPT_BOOL(gpu_float)},{"fel-experiment",OPT_BOOL(fel_experiment)},{"realtime-p81",OPT_BOOL(realtime_p81)},{"realtime-p84",OPT_BOOL(realtime_p84)},{0}}},
    .create=create,
};
