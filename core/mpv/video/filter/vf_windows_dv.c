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

#define BATCH 6
#define READY_CAP (BATCH * 2)
struct opts { char *dll; char *output_icc; bool gpu_float; };
typedef int (__cdecl *render_fn)(const wdv_frame_v2 *, uint32_t, uint32_t,
                               uint32_t, wdv_output_v2 *, char *, uint32_t);
typedef void *(__cdecl *create_fn)(void);
typedef void *(__cdecl *create_hlg_fn)(uint32_t,uint32_t,uint32_t);
typedef void (__cdecl *destroy_fn)(void*);
typedef int (__cdecl *render3_fn)(void*,const wdv_frame_v2*,uint32_t,uint32_t,uint32_t,wdv_output_v2*,char*,uint32_t);
typedef void* (__cdecl *create_gpu6_fn)(void*,uint32_t,uint32_t,uint32_t,uint32_t);
typedef void* (__cdecl *create_gpu_fn)(void*,uint32_t,uint32_t,uint32_t);
typedef int (__cdecl *render_gpu_fn)(void*,const wdv_frame_v2*,const wdv_gpu_frame_v5*,uint32_t,uint32_t,uint32_t,wdv_output_v2*,char*,uint32_t);
struct priv {
    create_gpu6_fn create_gpu7, create_gpu6; bool job_float; unsigned gpu_transfer;
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

// Only the external renderer runs on this worker. MPV image download,
// ownership, pins and metadata stay on the core thread. Reset discards the
// outstanding generation without blocking menu/pause/seek commands.
static DWORD WINAPI render_worker(LPVOID context)
{
    struct mp_filter *f=context;struct priv *p=f->priv;
    LARGE_INTEGER a,b,hz;QueryPerformanceFrequency(&hz);QueryPerformanceCounter(&a);
    if(p->job_gpu)p->rc=p->render_gpu(p->gpu_session,p->input,p->gpu,p->job_count,p->job_w,p->job_h,p->output,p->error,sizeof(p->error));
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
            if(p->job_float)dst->params.color=(struct pl_color_space){
                .primaries=PL_COLOR_PRIM_BT_709,.transfer=PL_COLOR_TRC_SCRGB};
            av_buffer_unref(&dst->icc_profile);
            if(p->output_icc && !p->job_float)dst->icc_profile=av_buffer_ref(p->output_icc);
            // MediaEngine returns a complete opaque video image, not a compositing layer.
            // Its incidental alpha (e.g. the Dolby notification rectangle) is not coverage.
            // Keep RGB and the empty-surface guard unchanged; do not affect MPV OSD alpha.
            dst->params.repr=(struct pl_color_repr){.sys=PL_COLOR_SYSTEM_RGB,
                .levels=PL_COLOR_LEVELS_FULL,.alpha=PL_ALPHA_NONE};
            dst->params.primaries_orig=PL_COLOR_PRIM_BT_709;
            dst->params.transfer_orig=p->job_float?PL_COLOR_TRC_SCRGB:PL_COLOR_TRC_SRGB; dst->params.sys_orig=PL_COLOR_SYSTEM_RGB;
            dst->params.no_dovi=true; dst->params.no_enhancement_layer=true;
            p->ready[kept+i]=dst; rgb[i]=NULL; talloc_free(orig);
        } else p->ready[kept+i]=orig;
        mp_image_unrefp(&cpu[i]); mp_image_unrefp(&rgb[i]); p->pending[i]=NULL;
    }
    p->state=p->job_fel?"fel-gpu-next":ok?"processed":(has_rpu||p->failed?"fallback":"passthrough");
    if(p->job_fel){p->failed=false;p->last_error[0]=0;}
    if(ok){if(p->job_gpu)p->gpu_frames+=p->count;p->processed_frames+=p->count;p->failed=false;p->last_error[0]=0;}
    if (ok) MP_DBG(f,"Windows DV processed %d paired frames %.6f..%.6f in MPV; output %s.\n",
                   p->count,p->ready[kept]->pts,p->ready[kept+p->count-1]->pts,p->job_float?"FP16 scRGB":"SDR BGRA");
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
    bool has_rpu = false; p->job_fel=false;
    struct mp_image *first=p->pending[0];
    // v6 selects the actual BL transfer; v5 is HLG-only and must never receive PQ.
    unsigned transfer=first->params.transfer_orig==PL_COLOR_TRC_HLG ? 2 :
                      first->params.transfer_orig==PL_COLOR_TRC_PQ ? 1 : 0;
    p->job_gpu=p->render_gpu && (p->create_gpu6 || (p->create_gpu && transfer==2)) &&
        first->imgfmt==IMGFMT_D3D11 && first->params.hw_subfmt==IMGFMT_P010 &&
        first->hwctx && transfer && first->nominal_fps>0;
    // Float output is verified for HLG. Keep established PQ/P7 output unchanged.
    p->job_float=p->job_gpu && transfer==2 && p->opts->gpu_float && p->create_gpu7;
    if(p->job_gpu){
        AVHWFramesContext *fc=(void*)first->hwctx->data;
        AVHWDeviceContext *dc=(void*)fc->device_ref->data;
        AVD3D11VADeviceContext *dd=dc->hwctx;
        AVRational fps=av_d2q(first->nominal_fps,100000);unsigned chroma=first->params.chroma_location==PL_CHROMA_TOP_LEFT;
        if(p->gpu_session && (p->gpu_device!=dd->device || av_cmp_q(fps,p->gpu_fps) || chroma!=p->gpu_chroma || transfer!=p->gpu_transfer)){p->destroy3(p->gpu_session);p->gpu_session=NULL;}
        if(!p->gpu_session){p->gpu_device=dd->device;p->gpu_fps=fps;p->gpu_chroma=chroma;p->gpu_transfer=transfer;p->gpu_session=p->job_float ? p->create_gpu7(dd->device,fps.num,fps.den,chroma,transfer) : p->create_gpu6 ? p->create_gpu6(dd->device,fps.num,fps.den,chroma,transfer) : p->create_gpu(dd->device,fps.num,fps.den,chroma);}
        ok=p->gpu_session && mp_update_av_hw_frames_pool(&p->gpu_pool,fc->device_ref,IMGFMT_D3D11,p->job_float?IMGFMT_RGBAF16:IMGFMT_BGRA,w,h,false);
        if(!ok)snprintf(p->error,sizeof(p->error),"GPU Windows DV session/pool unavailable");
    }

    for (int i = 0; ok && i < p->count; i++) {
        struct mp_image *img = p->pending[i], *el = img->enhancement_layer;
        AVBufferRef *rpu = el ? raw_rpu(el) : NULL;
        if(!rpu)rpu=raw_rpu(img); // Interleaved streams can retain raw RPU on BL.
        p->paired_el|=!!el;
        // Original BL transfer describes raw P010, unlike effective DV/PQ tags.
        p->source_transfer = img->params.transfer_orig;
        has_rpu |= !!rpu; if(rpu){p->rpu_seen++;p->paired_el|=!!el;}
        bool hlg=img->params.transfer_orig==PL_COLOR_TRC_HLG;
        if (i==0) p->job_hlg=hlg;
#if PL_API_VER >= 370
        // Windows accepts BL + RPU, not FEL residual textures. Preserve both
        // layers for gpu-next's NLQ reconstruction instead of stripping FEL.
        if (img->params.repr.dovi && img->params.repr.dovi->nlq_active) {
            p->job_fel=!!el;has_rpu=true;
            snprintf(p->error,sizeof(p->error),"%s",el?"FEL reconstruction uses gpu-next; Windows BL-only effect cannot consume FEL residual":"FEL residual not paired; original frame retained");
            ok=false;break;
        }
#endif
        if (rpu && p->job_gpu &&
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
        } else if (rpu && img->params.transfer_orig!=PL_COLOR_TRC_PQ) {
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
    p->rpu_seen=p->processed_frames=p->gpu_frames=0;p->paired_el=false;p->state="waiting";p->last_error[0]=0;
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
    p->job_pending=false;reset(f);if(p->gpu_session)p->destroy3(p->gpu_session);av_buffer_unref(&p->gpu_pool);av_buffer_unref(&p->output_icc);if(p->hlg_session)p->destroy3(p->hlg_session);if(p->session)p->destroy3(p->session);if(p->dll)FreeLibrary(p->dll);
}
static bool command(struct mp_filter *f,struct mp_filter_command *cmd)
{
    struct priv *p=f->priv;
    if(cmd->type==MP_FILTER_COMMAND_SET_SEEK_TARGET){p->seek_pts=cmd->seek_pts;return true;}
    if(cmd->type!=MP_FILTER_COMMAND_GET_META)return false;
    struct mp_tags *t=talloc_zero(NULL,struct mp_tags);
    mp_tags_set_str(t,"seek-preroll-skipped",mp_tprintf(64,"%llu",p->seek_skipped));
    mp_tags_set_str(t,"output-icc",p->job_float?"none (scRGB)":p->profile_path?p->profile_path:"none");
    mp_tags_set_str(t,"state",p->state?p->state:"waiting");
    mp_tags_set_str(t,"rpu-seen",mp_tprintf(64,"%llu",p->rpu_seen));
    mp_tags_set_str(t,"processed-frames",mp_tprintf(64,"%llu",p->processed_frames));
    mp_tags_set_str(t,"paired-el",p->paired_el?"yes":"no");
    mp_tags_set_str(t,"enhancement-path",p->job_fel?"gpu-next-fel":"unchanged");
    mp_tags_set_str(t,"bridge-available",p->render?"yes":"no");
    mp_tags_set_str(t,"fallback-reason",p->last_error);
    mp_tags_set_str(t,"source-base-transfer",p->source_transfer == PL_COLOR_TRC_HLG ? "hlg" :
        p->source_transfer == PL_COLOR_TRC_PQ ? "pq" : "other-or-unknown");
    mp_tags_set_str(t,"windows-input-transfer",p->job_hlg?"HLG":"PQ");
    mp_tags_set_str(t,"output",p->job_float?"scRGB FP16 (display mapping follows)":"SDR");
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
    p->state=p->render?"waiting":"unavailable";
    if(!p->render)MP_WARN(f,"Windows DV bridge unavailable; original MPV path retained.\n");
    return f;
}
#define OPT_BASE_STRUCT struct opts
const struct mp_user_filter_entry vf_windows_dv={
    .desc={.name="windows-dv",.description="Experimental Windows Dolby processing in MPV window",
        .priv_size=sizeof(struct opts),.options=(const m_option_t[]){{"dll",OPT_STRING(dll)},{"output-icc",OPT_STRING(output_icc)},{"gpu-float",OPT_BOOL(gpu_float)},{0}}},
    .create=create,
};
