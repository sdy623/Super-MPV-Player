/* Standalone D3D11 analysis. Only the histogram leaves the GPU, never a video frame. */
#pragma once
#include <d3d10.h>
#include "rt84_stats.h"
#include "rt84_program.inc"

struct rt84_gpu {
    ID3D11Device *dev;
    ID3D11DeviceContext *ctx;
    ID3D10Multithread *mt;
    ID3D11Texture2D *copy;
    ID3D11ShaderResourceView *views[2];
    ID3D11ComputeShader *shader;
    ID3D11UnorderedAccessView *view_hist;
    ID3D11Buffer *hist,*stage,*params,*lut;
    int w,h;
};

#define RELEASE84(t,p) do { if(p) t##_Release(p); } while(0)
static void rt84_destroy(struct rt84_gpu *d)
{
    RELEASE84(ID3D11ShaderResourceView,d->views[0]);
    RELEASE84(ID3D11ShaderResourceView,d->views[1]);
    RELEASE84(ID3D11UnorderedAccessView,d->view_hist);
    RELEASE84(ID3D11Texture2D,d->copy);
    RELEASE84(ID3D11ComputeShader,d->shader);
    RELEASE84(ID3D11Buffer,d->hist);
    RELEASE84(ID3D11Buffer,d->stage);
    RELEASE84(ID3D11Buffer,d->params);
    RELEASE84(ID3D11Buffer,d->lut);
    RELEASE84(ID3D11DeviceContext,d->ctx);
    RELEASE84(ID3D10Multithread,d->mt);
    RELEASE84(ID3D11Device,d->dev);
    *d=(struct rt84_gpu){0};
}

static bool rt84_prepare(struct mp_filter *f,struct rt84_gpu *d,ID3D11Device *dev,int w,int h)
{
    if(d->shader && d->dev==dev && d->w==w && d->h==h) return true;
    rt84_destroy(d);
    if(w<2 || h<2 || (w|h)&1 || w>3840 || h>2160)return false;
    d->dev=dev; ID3D11Device_AddRef(dev);
    HRESULT hr;
    if(FAILED(hr=ID3D11Device_QueryInterface(dev,&IID_ID3D10Multithread,(void**)&d->mt)))goto failed;
    if(!ID3D10Multithread_GetMultithreadProtected(d->mt)){hr=E_FAIL;goto failed;}
    ID3D11Device_GetImmediateContext(dev,&d->ctx);
    D3D11_TEXTURE2D_DESC tex={.Width=w,.Height=h,.MipLevels=1,.ArraySize=1,.Format=DXGI_FORMAT_P010,
                           .SampleDesc={.Count=1},.Usage=D3D11_USAGE_DEFAULT,.BindFlags=D3D11_BIND_SHADER_RESOURCE};
    if(FAILED(hr=ID3D11Device_CreateTexture2D(dev,&tex,NULL,&d->copy)))goto failed;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv={.Format=DXGI_FORMAT_R16_UNORM,.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D,
                                        .Texture2D={.MipLevels=1}};
    if(FAILED(hr=ID3D11Device_CreateShaderResourceView(dev,(ID3D11Resource*)d->copy,&srv,&d->views[0])))goto failed;
    srv.Format=DXGI_FORMAT_R16G16_UNORM;
    if(FAILED(hr=ID3D11Device_CreateShaderResourceView(dev,(ID3D11Resource*)d->copy,&srv,&d->views[1])))goto failed;
    D3D11_BUFFER_DESC hist={.ByteWidth=4096*sizeof(uint32_t),.Usage=D3D11_USAGE_DEFAULT,.BindFlags=D3D11_BIND_UNORDERED_ACCESS};
    if(FAILED(hr=ID3D11Device_CreateBuffer(dev,&hist,NULL,&d->hist)))goto failed;
    D3D11_UNORDERED_ACCESS_VIEW_DESC uh={.Format=DXGI_FORMAT_R32_UINT,.ViewDimension=D3D11_UAV_DIMENSION_BUFFER,
                                         .Buffer={.NumElements=4096}};
    if(FAILED(hr=ID3D11Device_CreateUnorderedAccessView(dev,(ID3D11Resource*)d->hist,&uh,&d->view_hist)))goto failed;
    hist.Usage=D3D11_USAGE_STAGING; hist.BindFlags=0;hist.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    if(FAILED(hr=ID3D11Device_CreateBuffer(dev,&hist,NULL,&d->stage)))goto failed;
    D3D11_BUFFER_DESC cb={.ByteWidth=16,.Usage=D3D11_USAGE_DEFAULT,.BindFlags=D3D11_BIND_CONSTANT_BUFFER};
    if(FAILED(hr=ID3D11Device_CreateBuffer(dev,&cb,NULL,&d->params)))goto failed;
    cb.ByteWidth=sizeof(rt84_luma);cb.Usage=D3D11_USAGE_IMMUTABLE;
    D3D11_SUBRESOURCE_DATA lut={.pSysMem=rt84_luma};
    if(FAILED(hr=ID3D11Device_CreateBuffer(dev,&cb,&lut,&d->lut)))goto failed;
    if(FAILED(hr=ID3D11Device_CreateComputeShader(dev,rt84_bytecode,sizeof(rt84_bytecode),NULL,&d->shader)))goto failed;
    d->w=w;d->h=h;
    return true;
failed:
    MP_WARN(f,"P8.4 GPU analysis setup failed (0x%08lx)\n",(unsigned long)hr);
    rt84_destroy(d);
    return false;
}

static bool rt84_analyze(struct mp_filter *f,struct rt84_gpu *d,struct mp_image *img,struct rtpq_frame *stats)
{
    AVHWFramesContext *fc=(void*)img->hwctx->data;
    AVD3D11VADeviceContext *dd=((AVHWDeviceContext*)fc->device_ref->data)->hwctx;
    if(!rt84_prepare(f,d,dd->device,img->w,img->h))return false;
    ID3D11Texture2D *input=(void*)img->planes[0];
    UINT slice=(UINT)(uintptr_t)img->planes[1];
    D3D11_TEXTURE2D_DESC src={0};
    ID3D11Texture2D_GetDesc(input,&src);
    if(src.Format!=DXGI_FORMAT_P010 || slice>=src.ArraySize || src.MipLevels!=1 ||
       src.Width<(UINT)d->w || src.Height<(UINT)d->h)return false;
    ID3D11DeviceContext *c=d->ctx;
    uint32_t dims[4]={d->w,d->h,img->params.chroma_location==PL_CHROMA_TOP_LEFT,0};
    ID3D11ShaderResourceView *old_views[2]={0};
    ID3D11UnorderedAccessView *old_uav=NULL,*empty_uav=NULL;
    ID3D11Buffer *old_cb[2]={0},*bind_cb[2]={d->params,d->lut};
    ID3D11ComputeShader *old_shader=NULL;
    ID3D11ClassInstance *classes[256]={0};
    UINT nclasses=MP_ARRAY_SIZE(classes);
    D3D11_BOX box={0,0,0,d->w,d->h,1};
    UINT clear[4]={0};
    D3D11_MAPPED_SUBRESOURCE map={0};
    uint32_t data[4096];
    ID3D10Multithread_Enter(d->mt);
    ID3D11DeviceContext_CSGetShader(c,&old_shader,classes,&nclasses);
    ID3D11DeviceContext_CSGetShaderResources(c,0,2,old_views);
    ID3D11DeviceContext_CSGetConstantBuffers(c,0,2,old_cb);
    ID3D11DeviceContext_CSGetUnorderedAccessViews(c,0,1,&old_uav);
    ID3D11DeviceContext_CopySubresourceRegion(c,(ID3D11Resource*)d->copy,0,0,0,0,(ID3D11Resource*)input,slice,&box);
    ID3D11DeviceContext_UpdateSubresource(c,(ID3D11Resource*)d->params,0,NULL,dims,0,0);
    ID3D11DeviceContext_ClearUnorderedAccessViewUint(c,d->view_hist,clear);
    ID3D11DeviceContext_CSSetShader(c,d->shader,NULL,0);
    ID3D11DeviceContext_CSSetShaderResources(c,0,2,d->views);
    ID3D11DeviceContext_CSSetConstantBuffers(c,0,2,bind_cb);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(c,0,1,&d->view_hist,NULL);
    ID3D11DeviceContext_Dispatch(c,256,1,1);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(c,0,1,&empty_uav,NULL);
    ID3D11DeviceContext_CopyResource(c,(ID3D11Resource*)d->stage,(ID3D11Resource*)d->hist);
    ID3D11DeviceContext_CSSetShader(c,old_shader,classes,nclasses);
    ID3D11DeviceContext_CSSetShaderResources(c,0,2,old_views);
    ID3D11DeviceContext_CSSetConstantBuffers(c,0,2,old_cb);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(c,0,1,&old_uav,NULL);
    HRESULT hr=ID3D11DeviceContext_Map(c,(ID3D11Resource*)d->stage,0,D3D11_MAP_READ,0,&map);
    if(SUCCEEDED(hr)){
        memcpy(data,map.pData,sizeof(data));
        ID3D11DeviceContext_Unmap(c,(ID3D11Resource*)d->stage,0);
    }
    ID3D10Multithread_Leave(d->mt);
    RELEASE84(ID3D11ComputeShader,old_shader); RELEASE84(ID3D11UnorderedAccessView,old_uav);
    for(int i=0;i<2;i++){RELEASE84(ID3D11ShaderResourceView,old_views[i]);RELEASE84(ID3D11Buffer,old_cb[i]);}
    for(UINT i=0;i<nclasses;i++)RELEASE84(ID3D11ClassInstance,classes[i]);
    return SUCCEEDED(hr) && rt84_stats(data,((d->w+3)/4)*((d->h+3)/4),stats);
}
#undef RELEASE84
