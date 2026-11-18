/* Isolated P7 FEL reconstruction experiment. LGPL-2.1-or-later.
 * This is NOT a lossless P7 conversion: the complete reconstructed picture
 * is quantized to P010 for the installed Windows component's input contract.
 * Originals stay untouched. No ICC, SDR mapping, or active-area crop here.
 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/colorspace.h>
#include <libdovi/rpu_parser.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "fel_abi.h"

struct fel_ctx { pl_log log; pl_d3d11 d3d; pl_renderer rr; int metadata_saved; };
__declspec(dllexport) void fel_destroy_v1(void *opaque)
{
    struct fel_ctx *c=opaque;if(!c)return;
    pl_renderer_destroy(&c->rr);pl_d3d11_destroy(&c->d3d);
    pl_log_destroy(&c->log);free(c);
}
__declspec(dllexport) void *fel_create_v1(void *device, uint32_t pl_version)
{
    if(!device || pl_version!=PL_API_VER)return NULL;
    struct fel_ctx *c=calloc(1,sizeof(*c));if(!c)return NULL;
    c->log=pl_log_create(PL_API_VER,pl_log_params(.log_cb=pl_log_simple,.log_priv=stderr,.log_level=PL_LOG_WARN));
    c->d3d=pl_d3d11_create(c->log,pl_d3d11_params(.device=device));
    if(c->d3d)c->rr=pl_renderer_create(c->log,c->d3d->gpu);
    if(!c->rr){fel_destroy_v1(c);return NULL;}return c;
}
static void unwrap(struct fel_ctx*c,struct pl_frame*f)
{
    for(int i=0;i<f->num_planes;i++)pl_tex_destroy(c->d3d->gpu,&f->planes[i].texture);
}
static int wrap(struct fel_ctx*c,struct pl_frame*f,void*tex,int w,int h,
                const struct pl_color_repr*repr,int chroma)
{
    f->num_planes=2;f->repr=*repr;
    f->repr.bits=(struct pl_bit_encoding){16,10,6};
    f->color=(struct pl_color_space){.primaries=PL_COLOR_PRIM_BT_2020,
        .transfer=PL_COLOR_TRC_PQ,.hdr={.min_luma=1e-6f,.max_luma=10000}};
    f->crop=(pl_rect2df){0,0,w,h};
    for(int i=0;i<2;i++){
        f->planes[i].texture=pl_d3d11_wrap(c->d3d->gpu,pl_d3d11_wrap_params(
            .tex=tex,.fmt=i?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R16_UNORM,
            .w=i?w/2:w,.h=i?h/2:h));
        f->planes[i].components=i?2:1;
        f->planes[i].component_mapping[0]=i?1:0;
        f->planes[i].component_mapping[1]=i?2:-1;
        if(!f->planes[i].texture)return 0;
    }
    pl_frame_set_chroma_location(f,chroma);return 1;
}

/* Combines reconstruction and metadata handoff: no normalized RPU can be
 * returned without both input textures and a successful reconstruction pass.
 * All textures belong to the bridge's private D3D11 device and are queue-ready.
 */
static int render_controlled(void*opaque,void*bl,void*el,void*out,
    uint32_t w,uint32_t h,const wdv_fel_frame_v10*extra,
    const uint8_t*rpu,size_t rpu_len,int escaped,uint8_t*nal,size_t capacity,int el_enabled)
{
    struct fel_ctx*c=opaque;
    if(!c||!bl||!el||!out||!extra||!extra->bl_repr||!extra->el_repr||
       !rpu||!rpu_len||!nal||capacity<2||w<2||h<2||w%2||h%2)return -1;
    const struct pl_color_repr *br=extra->bl_repr,*er=extra->el_repr;
    if(br->sys!=PL_COLOR_SYSTEM_DOLBYVISION || !br->dovi || !br->dovi->nlq_active)return -2;
    DoviRpuOpaque*rp=escaped?dovi_parse_unspec62_nalu(rpu,rpu_len):dovi_parse_rpu(rpu,rpu_len);
    if(!rp)return -3;
    const DoviRpuDataHeader*head=dovi_rpu_get_header(rp);
    int valid=head&&head->guessed_profile==7&&head->el_type&&!strcmp(head->el_type,"FEL");
    if(head)dovi_rpu_free_header(head);
    if(!valid){dovi_rpu_free(rp);return -4;}
    struct pl_frame src={0},enh={0},dst={0};int result=-5;
    struct pl_color_repr target={.sys=PL_COLOR_SYSTEM_BT_2020_NC,
        .levels=PL_COLOR_LEVELS_LIMITED,.alpha=PL_ALPHA_NONE};
    if(!wrap(c,&src,bl,w,h,br,extra->bl_chroma)||
       !wrap(c,&enh,el,extra->el_w,extra->el_h,er,extra->el_chroma)||
       !wrap(c,&dst,out,w,h,&target,extra->bl_chroma))goto done;
    src.enhancement_layer=el_enabled?&enh:NULL;
    struct pl_render_params params=pl_render_default_params;
    struct pl_color_map_params cm=pl_color_map_default_params;
    cm.tone_mapping_function=&pl_tone_map_clip;cm.gamut_mapping=&pl_gamut_map_clip;
    cm.metadata=PL_HDR_METADATA_NONE;cm.mastering_clip=0;cm.film_strength=0;
    params.color_map_params=&cm;params.peak_detect_params=NULL;
    params.dither_params=NULL;params.deband_params=NULL;
    // Identical 10000-nit PQ/BT.2020 endpoints: no display/tone mapping here.
    if(!pl_render_image(c->rr,&src,&dst,&params))goto done;
    pl_gpu_flush(c->d3d->gpu);
    // The full FEL image is now baked into PQ/BT.2020. Remove ONLY the already
    // applied reshape/NLQ, using libdovi's P8.1 coefficients and preserving DM.
    if(!c->metadata_saved){
        char path[32768];DWORD n=GetEnvironmentVariableA("WDV_FEL_RPU_DUMP",path,sizeof(path));
        if(n&&n<sizeof(path)-20){strcat(path,".original.rpu");FILE*f=fopen(path,"wb");if(f){fwrite(rpu,1,rpu_len,f);fclose(f);}}
    }
    if(dovi_convert_rpu_with_mode(rp,2)!=0){result=-6;goto done;}
    const DoviData*data=dovi_write_rpu(rp);
    if(!data||data->len+2>capacity){if(data)dovi_data_free(data);result=-7;goto done;}
    nal[0]=0x7c;nal[1]=0x01;memcpy(nal+2,data->data,data->len);
    result=(int)data->len+2;dovi_data_free(data);
    if(!c->metadata_saved){
        char path[32768];DWORD n=GetEnvironmentVariableA("WDV_FEL_RPU_DUMP",path,sizeof(path));
        if(n&&n<sizeof(path)-20){strcat(path,".normalized.rpu");FILE*f=fopen(path,"wb");if(f){fwrite(nal,1,result,f);fclose(f);}}
        c->metadata_saved=1;
    }
done:
    unwrap(c,&src);unwrap(c,&enh);unwrap(c,&dst);dovi_rpu_free(rp);return result;
}

__declspec(dllexport) int fel_render_v1(void*c,void*b,void*e,void*o,uint32_t w,uint32_t h,
 const wdv_fel_frame_v10*x,const uint8_t*r,size_t n,int esc,uint8_t*dst,size_t cap){
 return render_controlled(c,b,e,o,w,h,x,r,n,esc,dst,cap,1);
}
__declspec(dllexport) int fel_render_v2(void*c,void*b,void*e,void*o,uint32_t w,uint32_t h,
 const wdv_fel_frame_v10*x,const uint8_t*r,size_t n,int esc,uint8_t*dst,size_t cap,int el){
 if(el!=0 && el!=1)return -8;
 return render_controlled(c,b,e,o,w,h,x,r,n,esc,dst,cap,el);
}
