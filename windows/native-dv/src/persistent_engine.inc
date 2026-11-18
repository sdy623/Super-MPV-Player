#include "fel_abi.h"
#include <d3dcompiler.h>
#include <d3d11_4.h>
#ifndef WDV_TEST_OMIT_RPU
#define WDV_TEST_OMIT_RPU 0
#endif
// Separate MediaEngine device: the MPV decoder/VO context is never given to MF.
// NT shared textures move pixels only on GPU. Queue fences or legacy keys establish producer
// completion before the consumer copies; caller textures remain alive for the job.
struct SharedGpuSurface {
    ComPtr<ID3D11Texture2D> caller, renderer;
    ComPtr<IDXGIKeyedMutex> caller_key, renderer_key;
    ComPtr<ID3D11Fence> caller_fence, renderer_fence;
    ComPtr<ID3D11DeviceContext4> caller_queue, renderer_queue;
    UINT64 sequence=0;
    bool fenced=false;
    void init(ID3D11Device* a, ID3D11Device* b, UINT w, UINT h, DXGI_FORMAT format) {
        ComPtr<ID3D11Device5> a5,b5;ComPtr<ID3D11DeviceContext> ac,bc;
        a->GetImmediateContext(&ac);b->GetImmediateContext(&bc);
        if(SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&a5))) &&
           SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&b5))) &&
           SUCCEEDED(ac.As(&caller_queue)) && SUCCEEDED(bc.As(&renderer_queue)) &&
           SUCCEEDED(a5->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&caller_fence)))){
            HANDLE shared=nullptr;HRESULT hr=caller_fence->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&shared);
            if(SUCCEEDED(hr)){hr=b5->OpenSharedFence(shared,IID_PPV_ARGS(&renderer_fence));CloseHandle(shared);}
            fenced=SUCCEEDED(hr);
        }
        if(!fenced){caller_fence.Reset();renderer_fence.Reset();caller_queue.Reset();renderer_queue.Reset();}

        D3D11_TEXTURE2D_DESC d{}; d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;
        d.Format=format;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        d.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE | (fenced ? D3D11_RESOURCE_MISC_SHARED : D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX);
        CK(a->CreateTexture2D(&d,nullptr,&caller));
        ComPtr<IDXGIResource1> resource;CK(caller.As(&resource));HANDLE handle=nullptr;
        CK(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handle));
        ComPtr<ID3D11Device1> device;HRESULT hr=b->QueryInterface(IID_PPV_ARGS(&device));
        if(SUCCEEDED(hr))hr=device->OpenSharedResource1(handle,IID_PPV_ARGS(&renderer));
        CloseHandle(handle);CK(hr);if(!fenced){CK(caller.As(&caller_key));CK(renderer.As(&renderer_key));}
    }
};
struct KeyLease {
    IDXGIKeyedMutex* key; UINT64 release_key; bool held=false;
    KeyLease(IDXGIKeyedMutex*k,UINT64 acquire,UINT64 release):key(k),release_key(release){
        HRESULT hr=key->AcquireSync(acquire,2000);
        // WAIT_TIMEOUT and WAIT_ABANDONED are nonnegative: FAILED() is insufficient.
        if(hr!=S_OK)throw Failure{hr==WAIT_TIMEOUT?HRESULT_FROM_WIN32(WAIT_TIMEOUT):E_FAIL,"shared GPU mutex acquire"};
        held=true;
    }
    ~KeyLease(){if(held)key->ReleaseSync(release_key);}
    void release(){HRESULT hr=key->ReleaseSync(release_key);held=false;CK(hr);}
};
// Queue waits preserve producer/consumer/reuse ordering without stalling the CPU.
// Older D3D11 implementations retain the existing checked keyed-mutex path.
struct SurfaceLease {
    SharedGpuSurface& surface;bool caller,held=false;
    std::unique_ptr<KeyLease> legacy;
    SurfaceLease(SharedGpuSurface& s,bool c,UINT64 acquire,UINT64 release):surface(s),caller(c){
        if(s.fenced)CK(queue()->Wait(fence(),s.sequence));
        else legacy=std::make_unique<KeyLease>(c?s.caller_key.Get():s.renderer_key.Get(),acquire,release);
        held=true;
    }
    ID3D11DeviceContext4* queue(){return caller?surface.caller_queue.Get():surface.renderer_queue.Get();}
    ID3D11Fence* fence(){return caller?surface.caller_fence.Get():surface.renderer_fence.Get();}
    void release(){
        if(surface.fenced){
            CK(queue()->Signal(fence(),surface.sequence+1));
            ++surface.sequence;queue()->Flush();
        }else legacy->release();
        held=false;
    }
    ~SurfaceLease(){
        if(held&&surface.fenced){
            if(SUCCEEDED(queue()->Signal(fence(),surface.sequence+1)))++surface.sequence;
            queue()->Flush();
        }
    }
};
struct ContextLease {
    ID3D10Multithread* mt;
    ContextLease(ID3D10Multithread*m):mt(m){mt->Enter();}
    ~ContextLease(){mt->Leave();}
};
static double gpu_clock_ms(){LARGE_INTEGER c,f;QueryPerformanceCounter(&c);QueryPerformanceFrequency(&f);return 1000.0*double(c.QuadPart)/f.QuadPart;}

// Separate optional ABI, never relax the existing P7/P8 RPU converter.
struct ProfileTransport {
 HMODULE module=nullptr;
 using Classify=int(__cdecl*)(const uint8_t*,size_t,int);
 using Convert=int(__cdecl*)(const uint8_t*,size_t,int,uint8_t*,size_t,uint8_t*);
 Classify classify=nullptr;Convert forward=nullptr;
 ProfileTransport(){
  HMODULE self=nullptr;wchar_t file[32768];
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&gpu_clock_ms),&self))return;
  DWORD n=GetModuleFileNameW(self,file,32768);if(!n||n>=32768)return;
  auto path=fs::path(file).parent_path()/L"dv-profile.dll";
  module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(module){classify=reinterpret_cast<Classify>(GetProcAddress(module,"wdv_profile_v11"));forward=reinterpret_cast<Convert>(GetProcAddress(module,"wdv_forward_p5_v11"));}
 }
 ~ProfileTransport(){if(module)FreeLibrary(module);}
};
static ProfileTransport& profile_transport(){static ProfileTransport p;return p;}
extern "C" __declspec(dllexport) int wdv_rpu_profile_v11(const uint8_t*p,size_t n,int escaped){
 auto&helper=profile_transport();return helper.classify?helper.classify(p,n,escaped):0;
}

struct FelGpuHelper {
    HMODULE module=nullptr;void*ctx=nullptr;
    using Create=void*(__cdecl*)(void*,uint32_t);
    using Destroy=void(__cdecl*)(void*);
    using Render=int(__cdecl*)(void*,void*,void*,void*,uint32_t,uint32_t,const wdv_fel_frame_v10*,const uint8_t*,size_t,int,uint8_t*,size_t);
    using RenderControlled=int(__cdecl*)(void*,void*,void*,void*,uint32_t,uint32_t,const wdv_fel_frame_v10*,const uint8_t*,size_t,int,uint8_t*,size_t,int);
    Destroy destroy=nullptr;Render render=nullptr;RenderControlled controlled=nullptr;
    ~FelGpuHelper(){if(ctx&&destroy)destroy(ctx);if(module)FreeLibrary(module);}
    void init(ID3D11Device*device,uint32_t version){
      if(ctx)return;
      HMODULE self=nullptr;CK(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&gpu_clock_ms),&self)?S_OK:E_FAIL);
      wchar_t file[32768];DWORD n=GetModuleFileNameW(self,file,32768);if(!n||n>=32768)throw Failure{E_FAIL,"FEL module path"};
      auto path=fs::path(file).parent_path()/L"fel-gpu.dll";
      module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
      if(!module)throw Failure{HRESULT_FROM_WIN32(GetLastError()),"FEL helper load"};
      auto create=reinterpret_cast<Create>(GetProcAddress(module,"fel_create_v1"));
      destroy=reinterpret_cast<Destroy>(GetProcAddress(module,"fel_destroy_v1"));
      render=reinterpret_cast<Render>(GetProcAddress(module,"fel_render_v1"));
      controlled=reinterpret_cast<RenderControlled>(GetProcAddress(module,"fel_render_v2"));
      if(!create||!destroy||!render)throw Failure{E_NOINTERFACE,"FEL helper ABI"};
      ctx=create(device,version);if(!ctx)throw Failure{E_FAIL,"FEL private GPU create"};
    }
};
struct PersistentEngine {
    unsigned source_profile=8; bool native_sdr10=false;
    bool el_enabled=true,rpu_enabled=true;
    const wchar_t* renderer_profile()const{return source_profile==5?L"dvhe.05":L"dvhe.08";}

    const wdv_fel_frame_v10*fel=nullptr;
    FelGpuHelper fel_helper;
    SharedGpuSurface el_shared[8];
    uint32_t el_width=0,el_height=0;
    uint64_t fel_completed=0;

    uint64_t prep_ms=0,copy_ms=0; bool fp16=false; bool relative_srgb=false; bool hlg=false; uint32_t fps_n=24000,fps_d=1001,chroma=0;
    const wdv_gpu_frame_v5*gpu=nullptr; ID3D11Device*shared_device=nullptr;
    ComPtr<ID3D11DeviceContext> caller_context;
    ComPtr<ID3D10Multithread> caller_mt;
    // Rotate across jobs: single-frame jobs must not keep reusing slot zero.
    // Eight bounded slots also cover the largest supported batch.
    SharedGpuSurface input_shared[8], output_shared[8];
    uint64_t input_slot=0;
    HWND window=nullptr;
    ComPtr<ID3D11Device> d3d;ComPtr<ID3D11DeviceContext> context;
    ComPtr<IMFDXGIDeviceManager> manager;
    ComPtr<IMFMediaEngine> engine;ComPtr<IMFMediaEngineEx> ex;
    ComPtr<SourceExtension> extension;ComPtr<ID3D11Texture2D> texture,staging;
    bool metadata=false,ready=false,stepReady=false;HRESULT error=S_OK;
    ComPtr<RawSource> queue;
    LONGLONG origin=0,previous=-1,last_end=0;bool need_step=false;uint64_t rendered=0;
    uint32_t w,h,count;const wdv_frame_v2*input;wdv_output_v2*output;
    PersistentEngine(const wdv_frame_v2*i,uint32_t n,uint32_t width,uint32_t height,wdv_output_v2*o,bool is_hlg=false,uint32_t fn=24000,uint32_t fd=1001,uint32_t cs=0,const wdv_gpu_frame_v5*g=nullptr,ID3D11Device*dev=nullptr,bool use_float=false,bool use_relative=false):fp16(use_float),relative_srgb(use_relative),hlg(is_hlg),fps_n(fn),fps_d(fd),chroma(cs),gpu(g),shared_device(dev),w(width),h(height),count(n),input(i),output(o){origin=i[0].pts-10000000;}
    ~PersistentEngine(){close_direct_host();if(engine)engine->Shutdown();if(extension&&extension->source)extension->source->Shutdown();if(window)DestroyWindow(window);}
    static LRESULT CALLBACK wnd(HWND hw,UINT m,WPARAM wp,LPARAM lp){
        auto*p=reinterpret_cast<PersistentEngine*>(GetWindowLongPtrW(hw,GWLP_USERDATA));
        if(m==WM_NCCREATE){p=static_cast<PersistentEngine*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hw,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}
        if(p&&m==EV){if(wp==MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA){p->metadata=true;p->error=p->engine->SetCurrentTime(1);}if(wp==MF_MEDIA_ENGINE_EVENT_FIRSTFRAMEREADY)p->ready=true;if(wp==MF_MEDIA_ENGINE_EVENT_FRAMESTEPCOMPLETED)p->stepReady=true;if(wp==MF_MEDIA_ENGINE_EVENT_ERROR)p->error=FAILED(HRESULT(lp))?HRESULT(lp):E_FAIL;return 0;}
        return DefWindowProcW(hw,m,wp,lp);
    }
    DXGI_FORMAT output_format() const {return fp16?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_B8G8R8A8_UNORM;}
    // v9: vendor display-relative linear FP16, encoded once to sRGB FP16.
    // Storage format is not transfer semantics: do NOT label these codes scRGB.
    ComPtr<ID3D11Texture2D> sdr_transport;
    ComPtr<ID3D11ShaderResourceView> sdr_view;
    ComPtr<ID3D11RenderTargetView> sdr_target;
    ComPtr<ID3D11VertexShader> sdr_vs;
    ComPtr<ID3D11PixelShader> sdr_ps;
    bool uses_sdr10()const{return source_profile==5 || native_sdr10;}
    DXGI_FORMAT render_format()const{return uses_sdr10()?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_R16G16B16A16_FLOAT;}
    void init_sdr_transport(){
        D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        CK(d3d->CreateTexture2D(&d,nullptr,&sdr_transport));
        CK(d3d->CreateShaderResourceView(texture.Get(),nullptr,&sdr_view));
        CK(d3d->CreateRenderTargetView(sdr_transport.Get(),nullptr,&sdr_target));
        const char* vs="float4 main(uint i:SV_VertexID):SV_Position {float2 p=float2((i<<1)&2,i&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}";
        const char* ps="Texture2D<float4> src:register(t0);float enc(float x){return x<=0.0031308?12.92*x:1.055*pow(max(x,0.0),1.0/2.4)-0.055;}float4 main(float4 p:SV_Position):SV_Target {float3 c=src.Load(int3(p.xy,0)).rgb;return float4(enc(c.r),enc(c.g),enc(c.b),1);}";
        if(uses_sdr10())ps="Texture2D<float4> src:register(t0);float4 main(float4 p:SV_Position):SV_Target {return float4(src.Load(int3(p.xy,0)).rgb,1);}";
        ComPtr<ID3DBlob> bv,bp,err;
        CK(D3DCompile(vs,strlen(vs),nullptr,nullptr,nullptr,"main","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bv,&err));
        CK(D3DCompile(ps,strlen(ps),nullptr,nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bp,&err));
        CK(d3d->CreateVertexShader(bv->GetBufferPointer(),bv->GetBufferSize(),nullptr,&sdr_vs));
        CK(d3d->CreatePixelShader(bp->GetBufferPointer(),bp->GetBufferSize(),nullptr,&sdr_ps));
    }
    ID3D11Texture2D* transport_texture(){
        if(!relative_srgb)return texture.Get();
        D3D11_VIEWPORT vp{0,0,float(w),float(h),0,1};context->RSSetViewports(1,&vp);
        auto* rt=sdr_target.Get();context->OMSetRenderTargets(1,&rt,nullptr);
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(sdr_vs.Get(),nullptr,0);context->PSSetShader(sdr_ps.Get(),nullptr,0);
        auto* srv=sdr_view.Get();context->PSSetShaderResources(0,1,&srv);context->Draw(3,0);
        srv=nullptr;context->PSSetShaderResources(0,1,&srv);context->OMSetRenderTargets(0,nullptr,nullptr);
        return sdr_transport.Get();
    }
    struct RoScope {HRESULT hr=E_UNEXPECTED;void init(){hr=RoInitialize(RO_INIT_SINGLETHREADED);CK(hr);}~RoScope(){if(SUCCEEDED(hr))RoUninitialize();}} ro;
    winrt::Windows::ApplicationModel::AppService::AppServiceConnection service{nullptr};
    winrt::Windows::System::DispatcherQueueController dispatcher{nullptr};
    winrt::Windows::Graphics::Display::DisplayInformation display{nullptr};
    ComPtr<IMFTransform> xvp;
    void close_direct_host() noexcept {
      try{
       if(service){service.Close();service=nullptr;}
       xvp.Reset();display=nullptr;
       if(dispatcher){auto closing=dispatcher.ShutdownQueueAsync();while(closing.Status()==winrt::Windows::Foundation::AsyncStatus::Started){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}closing.GetResults();dispatcher=nullptr;}
      }catch(...){OutputDebugStringW(L"MPV: Dolby host cleanup failed\n");}
    }
    void init(){
        HMODULE self=nullptr;CK(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&wnd),&self)?S_OK:HRESULT_FROM_WIN32(GetLastError()));
        wchar_t filename[32768];DWORD n=GetModuleFileNameW(self,filename,32768);if(!n||n>=32768)throw Failure{E_FAIL,"module path"};
        auto rpuPath=fs::path(filename).parent_path()/L"mpv_windows_dv_rpu.dll";
        HMODULE rpu=LoadLibraryExW(rpuPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!rpu)throw Failure{HRESULT_FROM_WIN32(GetLastError()),"RPU adapter load"};
        struct Guard{HMODULE h;~Guard(){FreeLibrary(h);}} guard{rpu};
        using Convert=int(__cdecl*)(const uint8_t*,size_t,int,uint8_t*,size_t,uint8_t*);
        auto convert=reinterpret_cast<Convert>(GetProcAddress(rpu,"wdv_convert_rpu_v1"));if(!convert)throw Failure{E_NOINTERFACE,"RPU ABI"};
        WNDCLASSW cls{};cls.lpfnWndProc=wnd;cls.hInstance=self;cls.lpszClassName=L"MpvWindowsDvFrameServerV1";RegisterClassW(&cls);
        window=CreateWindowExW(0,cls.lpszClassName,L"",WS_POPUP,0,0,1,1,nullptr,nullptr,self,this);if(!window)throw Failure{HRESULT_FROM_WIN32(GetLastError()),"hidden callback window"};
        if(shared_device){
            ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;
            CK(shared_device->QueryInterface(IID_PPV_ARGS(&dxgi)));CK(dxgi->GetAdapter(&adapter));
            CK(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_VIDEO_SUPPORT|D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&d3d,nullptr,&context));
            shared_device->GetImmediateContext(&caller_context);
            CK(shared_device->QueryInterface(IID_PPV_ARGS(&caller_mt)));
            if(!caller_mt->GetMultithreadProtected())throw Failure{E_FAIL,"MPV device lacks multithread protection"};
            for(auto& surface:input_shared)surface.init(shared_device,d3d.Get(),w,h,DXGI_FORMAT_P010);
            for(auto& surface:output_shared)surface.init(shared_device,d3d.Get(),w,h,output_format());
        }else CK(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_VIDEO_SUPPORT|D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&d3d,nullptr,&context));
        ComPtr<ID3D10Multithread> mt;CK(d3d.As(&mt));mt->SetMultithreadProtected(TRUE);
        UINT token=0;CK(MFCreateDXGIDeviceManager(&token,&manager));CK(manager->ResetDevice(d3d.Get(),token));
        ComPtr<IMFMediaType> type;CK(MFCreateMediaType(&type));CK(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));CK(type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_P010));
        CK(MFSetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,w,h));CK(MFSetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,fps_n,fps_d));CK(MFSetAttributeRatio(type.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1));
        CK(type->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));CK(type->SetUINT32(MF_MT_VIDEO_PRIMARIES,MFVideoPrimaries_BT2020));CK(type->SetUINT32(MF_MT_TRANSFER_FUNCTION,hlg?MFVideoTransFunc_HLG:MFVideoTransFunc_2084));CK(type->SetUINT32(MF_MT_YUV_MATRIX,MFVideoTransferMatrix_BT2020_10));CK(type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,MFNominalRange_16_235));CK(type->SetString(MF_MT_VIDEO_RENDERER_EXTENSION_PROFILE,renderer_profile()));
        if(!rpu_enabled)CK(type->DeleteItem(MF_MT_VIDEO_RENDERER_EXTENSION_PROFILE));
        if(source_profile==5){
          // P5 code values are IPT-PQ-C2, not BT.2020 NCL. Keep the raw full-range
          // surface; the selected P5 effect interprets it with its unchanged RPU.
          CK(type->SetUINT32(MF_MT_VIDEO_PRIMARIES,MFVideoPrimaries_Unknown));
          // Verified against Windows HEVC output after its initial type-change event.
          CK(type->SetUINT32(MF_MT_TRANSFER_FUNCTION,MFVideoTransFunc_Unknown));
          CK(type->DeleteItem(MF_MT_YUV_MATRIX));
          CK(type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,MFNominalRange_0_255));
        }
        if(source_profile!=5 && (hlg || gpu))CK(type->SetUINT32(MF_MT_VIDEO_CHROMA_SITING,MFVideoChromaSubsampling_ProgressiveChroma | (chroma?MFVideoChromaSubsampling_Cosited:MFVideoChromaSubsampling_MPEG2)));
        if(relative_srgb){
        if(GetSystemMetrics(SM_CMONITORS)!=1)throw Failure{E_NOTIMPL,"relative Dolby host requires one active display"};
        if(!rpu_enabled){
          CK(CoCreateInstance(CLSID_VideoProcessorMFT,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&xvp)));
        }else{
        IMFActivate** effects=nullptr;UINT32 effect_count=0;
        CK(MFTEnumEx(MFT_CATEGORY_VIDEO_RENDERER_EFFECT,MFT_ENUM_FLAG_ALL|MFT_ENUM_FLAG_UNTRUSTED_STOREMFT,nullptr,nullptr,&effects,&effect_count));
        HRESULT activated=E_NOINTERFACE;
        for(UINT32 i=0;i<effect_count;i++){
          PROPVARIANT profiles;PropVariantInit(&profiles);bool p8=false;
          if(SUCCEEDED(effects[i]->GetItem(MFT_ENUM_VIDEO_RENDERER_EXTENSION_PROFILE,&profiles))){
           if(profiles.vt==(VT_VECTOR|VT_LPWSTR))for(ULONG j=0;j<profiles.calpwstr.cElems;j++)p8|=!wcscmp(profiles.calpwstr.pElems[j],renderer_profile());
           else if(profiles.vt==VT_LPWSTR)p8=!wcscmp(profiles.pwszVal,renderer_profile());}
          PropVariantClear(&profiles);
          if(p8&&!xvp)activated=effects[i]->ActivateObject(IID_PPV_ARGS(&xvp));
          effects[i]->Release();
        }CoTaskMemFree(effects);CK(activated);
        }
        ComPtr<IMFAttributes> xa;CK(xvp->GetAttributes(&xa));CK(xa->SetUINT32(MF_XVP_DISABLE_FRC,TRUE));CK(xa->SetUINT32(MF_XVP_CALLER_ALLOCATES_OUTPUT,TRUE));
        CK(xvp->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,reinterpret_cast<ULONG_PTR>(manager.Get())));
        ComPtr<IMFVideoProcessorControl3> vc;CK(xvp.As(&vc));
        ro.init();
        if(rpu_enabled){
        wchar_t* service_name=nullptr;UINT32 name_length=0;CK(xa->GetAllocatedString(MF_VIDEO_RENDERER_EFFECT_APP_SERVICE_NAME,&service_name,&name_length));
        service=winrt::Windows::ApplicationModel::AppService::AppServiceConnection();
        service.PackageFamilyName(L"DolbyLaboratories.DolbyVisionAccess_rz1tebttyb220");service.AppServiceName(service_name);CoTaskMemFree(service_name);
        auto opening=service.OpenAsync();auto deadline=GetTickCount64()+5000;
        while(opening.Status()==winrt::Windows::Foundation::AsyncStatus::Started){if(GetTickCount64()>deadline)throw Failure{HRESULT_FROM_WIN32(WAIT_TIMEOUT),"Dolby app service open timeout"};MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
        if(opening.GetResults()!=winrt::Windows::ApplicationModel::AppService::AppServiceConnectionStatus::Success)throw Failure{E_FAIL,"Dolby app service open failed"};
        ComPtr<IMFVideoRendererEffectControl> effect_control;CK(xvp.As(&effect_control));CK(effect_control->OnAppServiceConnectionEstablished(reinterpret_cast<IUnknown*>(winrt::get_abi(service))));
        }
        DispatcherQueueOptions dq{sizeof(DispatcherQueueOptions),DQTYPE_THREAD_CURRENT,DQTAT_COM_NONE};CK(CreateDispatcherQueueController(dq,reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(dispatcher))));
        auto df=winrt::get_activation_factory<winrt::Windows::Graphics::Display::DisplayInformation,IDisplayInformationStaticsInterop>();
        CK(df->GetForMonitor(MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY),winrt::guid_of<winrt::Windows::Graphics::Display::DisplayInformation>(),winrt::put_abi(display)));
        CK(vc->SetOutputDevice(reinterpret_cast<IUnknown*>(winrt::get_abi(display))));
        CK(xvp->SetInputType(0,type.Get(),0));
        ComPtr<IMFMediaType> ot;
        for(DWORD index=0;index<16;index++){ComPtr<IMFMediaType> candidate;HRESULT r=xvp->GetOutputAvailableType(0,index,&candidate);if(FAILED(r))break;GUID sub{};UINT32 tf=0;candidate->GetGUID(MF_MT_SUBTYPE,&sub);candidate->GetUINT32(MF_MT_TRANSFER_FUNCTION,&tf);if(uses_sdr10() ? (sub==MFVideoFormat_A2R10G10B10 && tf==MFVideoTransFunc_sRGB) : (sub==MFVideoFormat_A16B16G16R16F && tf==MFVideoTransFunc_10_rel)){ot=candidate;break;}}
        if(!ot && !rpu_enabled){CK(MFCreateMediaType(&ot));CK(ot->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));}
        if(!ot)throw Failure{MF_E_INVALIDMEDIATYPE,"required declared Dolby output type unavailable"};
        CK(MFSetAttributeSize(ot.Get(),MF_MT_FRAME_SIZE,w,h));CK(MFSetAttributeRatio(ot.Get(),MF_MT_FRAME_RATE,fps_n,fps_d));CK(MFSetAttributeRatio(ot.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1));CK(ot->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
        CK(ot->SetGUID(MF_MT_SUBTYPE,uses_sdr10()?MFVideoFormat_A2R10G10B10:MFVideoFormat_A16B16G16R16F));
        CK(ot->SetUINT32(MF_MT_TRANSFER_FUNCTION,uses_sdr10()?MFVideoTransFunc_sRGB:MFVideoTransFunc_10_rel));
        CK(ot->SetUINT32(MF_MT_VIDEO_PRIMARIES,MFVideoPrimaries_BT709));CK(ot->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,MFNominalRange_0_255));
        CK(xvp->SetOutputType(0,ot.Get(),0));
        /* Same-sized complete input/output surfaces; no crop or scaling requested. */
        CK(xvp->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,0));CK(xvp->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,0));
        {D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=1;td.ArraySize=1;td.Format=render_format();td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;CK(d3d->CreateTexture2D(&td,nullptr,&texture));if(!gpu){td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;CK(d3d->CreateTexture2D(&td,nullptr,&staging));}}
        init_sdr_transport();
        }else{
        queue=Make<RawSource>(make_frames(),type.Get(),86400LL*10000000,true);
        extension=Make<SourceExtension>();extension->source=queue;
        auto notify=Make<Notify>();notify->window=window;ComPtr<IMFAttributes> a;CK(MFCreateAttributes(&a,8));CK(a->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT,output_format()));CK(a->SetUnknown(MF_MEDIA_ENGINE_CALLBACK,notify.Get()));CK(a->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER,manager.Get()));CK(a->SetUnknown(MF_MEDIA_ENGINE_EXTENSION,extension.Get()));CK(a->SetUINT64(MF_MEDIA_ENGINE_OPM_HWND,UINT64(UINT_PTR(window))));CK(a->SetGUID(MF_MEDIA_ENGINE_BROWSER_COMPATIBILITY_MODE,MF_MEDIA_ENGINE_BROWSER_COMPATIBILITY_MODE_IE_EDGE));
        ComPtr<IMFMediaEngineClassFactory> factory;CK(CoCreateInstance(CLSID_MFMediaEngineClassFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));CK(factory->CreateInstance(gpu ? MF_MEDIA_ENGINE_REAL_TIME_MODE : 0,a.Get(),&engine));CK(engine.As(&ex));CK(engine->SetMuted(TRUE));
        BSTR uri=SysAllocString(L"mpv-frame-source");HRESULT hr=engine->SetSource(uri);SysFreeString(uri);CK(hr);
        {D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=1;td.ArraySize=1;td.Format=output_format();td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;CK(d3d->CreateTexture2D(&td,nullptr,&texture));if(!gpu){td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;CK(d3d->CreateTexture2D(&td,nullptr,&staging));}}
        }
    }
    void wait_for_frame(){
        if(gpu){
            // Frame-ready/step-complete callbacks post to this STA message queue.
            // Wake on the event instead of sleeping through it (not a global timer change).
            DWORD result=MsgWaitForMultipleObjectsEx(0,nullptr,1,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
            if(result==WAIT_FAILED)throw Failure{HRESULT_FROM_WIN32(GetLastError()),"MediaEngine message wait"};
        }else Sleep(1);
    }
    int run(){
        if(relative_srgb)return run_relative();
        auto begin_ms=GetTickCount64(); prep_ms=copy_ms=0; bool first=!engine;
        if(first)init();
        if(!first&&need_step){stepReady=false;CK(ex->FrameStep(TRUE));need_step=false;}
        if(!first)CK(queue->append(make_frames()));
        auto deadline=GetTickCount64()+6000;uint32_t next=0;MSG msg{};
        while(next<count&&GetTickCount64()<deadline){
            while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}CK(error);
            LONGLONG tick=0;
            if(metadata&&ready&&engine->OnVideoStreamTick(&tick)==S_OK&&tick!=previous&&tick>=input[0].pts-origin){
                LONGLONG expected=input[next].pts-origin;
                if(std::llabs(tick-expected)>2)throw Failure{E_FAIL,"persistent frame PTS mismatch expected="+std::to_string(expected)+" actual="+std::to_string(tick)};
                auto copy_start=GetTickCount64(); RECT rect{0,0,LONG(w),LONG(h)};MFARGB border{};
                double tcopy=gpu_clock_ms(),trender=0,tshared=0,tcaller=0;
                HRESULT transfer=engine->TransferVideoFrame(texture.Get(),nullptr,&rect,&border);CK(transfer);
                if(transfer!=S_OK){wait_for_frame();continue;}
                if(gpu){
                    auto& shared_output=output_shared[rendered%8];
                    trender=gpu_clock_ms();
                    {SurfaceLease access(shared_output,false,0,1);
                     context->CopyResource(shared_output.renderer.Get(),texture.Get());access.release();}
                    tshared=gpu_clock_ms();
                    {ContextLease lock(caller_mt.Get());
                     SurfaceLease access(shared_output,true,1,0);
                     caller_context->CopyResource(static_cast<ID3D11Texture2D*>(gpu[next].output_texture),shared_output.caller.Get());access.release();}
                    tcaller=gpu_clock_ms();
                    if(tcaller-tcopy>8){wchar_t profile[32768];if(GetEnvironmentVariableW(L"GPU_COPY_PROFILE",profile,32768)){std::ofstream log(fs::path(profile),std::ios::app);log<<input[next].pts<<","<<trender-tcopy<<","<<tshared-trender<<","<<tcaller-tshared<<"\n";}}
                    CK(d3d->GetDeviceRemovedReason());CK(shared_device->GetDeviceRemovedReason());
                }else{context->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE map{};
                CK(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));
                for(uint32_t row=0;row<h;row++)memcpy(output[next].pixels+size_t(row)*output[next].stride,static_cast<uint8_t*>(map.pData)+size_t(row)*map.RowPitch,w*4);
                context->Unmap(staging.Get(),0);}copy_ms+=GetTickCount64()-copy_start;output[next].pts=input[next].pts;previous=tick;rendered++;next++;
                stepReady=false;if(next<count)CK(ex->FrameStep(TRUE));else need_step=true;
            }else wait_for_frame();
        }
        if(next!=count)throw Failure{HRESULT_FROM_WIN32(WAIT_TIMEOUT),"persistent frame sequence timeout"};
        if(rpu_enabled && !dolbyLoaded())throw Failure{E_FAIL,"Dolby RendererEffect not loaded"};
        last_end=input[count-1].pts+input[count-1].duration;queue->retire(previous); wchar_t path[32768]; if(GetEnvironmentVariableW(L"P84_TIMING_PATH",path,32768)){std::ofstream log(fs::path(path),std::ios::app);log<<rendered<<","<<GetTickCount64()-begin_ms<<","<<prep_ms<<","<<copy_ms<<","<<(gpu&&output_shared[0].fenced?"gpu-fence":"keyed-or-cpu")<<"\n";} return int(next);
    }
    int run_relative(){
        if(!xvp)init();
        MSG messages;while(PeekMessageW(&messages,nullptr,0,0,PM_REMOVE)){TranslateMessage(&messages);DispatchMessageW(&messages);}
        auto frames=make_frames();
        for(uint32_t i=0;i<count;i++){
          CK(xvp->ProcessInput(0,frames[i].Get(),0));
          ComPtr<IMFSample> sample;ComPtr<IMFMediaBuffer> mb;CK(MFCreateSample(&sample));
          CK(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D),texture.Get(),0,FALSE,&mb));CK(sample->AddBuffer(mb.Get()));
          MFT_OUTPUT_DATA_BUFFER od{};od.pSample=sample.Get();DWORD status=0;HRESULT r=xvp->ProcessOutput(0,1,&od,&status);if(od.pEvents)od.pEvents->Release();CK(r);
          LONGLONG actual=0;CK(sample->GetSampleTime(&actual));
          if(actual!=input[i].pts-origin)throw Failure{E_FAIL,"relative Dolby output PTS mismatch"};
          auto* encoded=transport_texture();

          auto& so=output_shared[rendered%8];
          {SurfaceLease access(so,false,0,1);context->CopyResource(so.renderer.Get(),encoded);access.release();}
          {ContextLease lock(caller_mt.Get());SurfaceLease access(so,true,1,0);caller_context->CopyResource(static_cast<ID3D11Texture2D*>(gpu[i].output_texture),so.caller.Get());access.release();}
          CK(d3d->GetDeviceRemovedReason());CK(shared_device->GetDeviceRemovedReason());output[i].pts=actual+origin;rendered++;
        }
        if(rpu_enabled && !dolbyLoaded())throw Failure{E_FAIL,"Dolby renderer not loaded by direct XVP"};
        last_end=input[count-1].pts+input[count-1].duration;return int(count);
    }
    void reconstruct_fel(uint32_t i,ComPtr<ID3D11Texture2D>&tex,std::vector<uint8_t>&nal,int&bytes){
        const auto&e=fel[i];const auto&v=input[i];
        auto*source=static_cast<ID3D11Texture2D*>(e.el_texture);
        D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);ComPtr<ID3D11Device> owner;source->GetDevice(&owner);
        if(owner.Get()!=shared_device||d.Format!=DXGI_FORMAT_P010||d.Width<e.el_w||d.Height<e.el_h||e.el_subresource>=d.ArraySize*d.MipLevels)
            throw Failure{E_INVALIDARG,"FEL EL texture contract"};
        if(!el_width){
            for(auto&surface:el_shared)surface.init(shared_device,d3d.Get(),e.el_w,e.el_h,DXGI_FORMAT_P010);
            el_width=e.el_w;el_height=e.el_h;
        }
        if(el_width!=e.el_w||el_height!=e.el_h)throw Failure{E_INVALIDARG,"FEL EL dimensions changed"};
        auto&shared=el_shared[(input_slot-1)%8];
        D3D11_BOX box{0,0,0,e.el_w,e.el_h,1};
        {ContextLease lock(caller_mt.Get());SurfaceLease access(shared,true,0,1);
         caller_context->CopySubresourceRegion(shared.caller.Get(),0,0,0,0,source,e.el_subresource,&box);access.release();}
        D3D11_TEXTURE2D_DESC target{};tex->GetDesc(&target);target.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> reconstructed;CK(d3d->CreateTexture2D(&target,nullptr,&reconstructed));
        fel_helper.init(d3d.Get(),e.pl_api_ver);
        {SurfaceLease access(shared,false,1,0);
         if(!el_enabled && !fel_helper.controlled)throw Failure{E_NOINTERFACE,"EL comparison helper unavailable"};
         bytes=el_enabled?fel_helper.render(fel_helper.ctx,tex.Get(),shared.renderer.Get(),reconstructed.Get(),w,h,&e,v.rpu,v.rpu_len,v.escaped,nal.data(),nal.size()):fel_helper.controlled(fel_helper.ctx,tex.Get(),shared.renderer.Get(),reconstructed.Get(),w,h,&e,v.rpu,v.rpu_len,v.escaped,nal.data(),nal.size(),0);
         context->ClearState();access.release();}
        if(bytes<1)throw Failure{E_FAIL,"FEL reconstruction/normalized RPU failed"};
        tex=reconstructed;++fel_completed;
        if(fel_completed==1){wchar_t logpath[32768];if(GetEnvironmentVariableW(L"WDV_FEL_EVIDENCE",logpath,32768)){
            std::ofstream log(fs::path(logpath),std::ios::app);
            log<<"FEL complete BL+EL GPU reconstruction -> P010 BT2020 NCL limited PQ; identity P8.1 RPU for Windows, quantized 10-bit, original PTS="<<v.pts<<" EL="<<e.el_w<<"x"<<e.el_h<<"\n";}}
    }
    std::vector<ComPtr<IMFSample>> make_frames(){
        HMODULE self=nullptr;CK(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&wnd),&self)?S_OK:HRESULT_FROM_WIN32(GetLastError()));
        wchar_t filename[32768];DWORD n=GetModuleFileNameW(self,filename,32768);if(!n||n>=32768)throw Failure{E_FAIL,"module path"};
        auto rpuPath=fs::path(filename).parent_path()/L"mpv_windows_dv_rpu.dll";
        HMODULE rpu=LoadLibraryExW(rpuPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!rpu)throw Failure{HRESULT_FROM_WIN32(GetLastError()),"RPU adapter load"};
        struct Guard{HMODULE h;~Guard(){FreeLibrary(h);}} guard{rpu};
        using Convert=int(__cdecl*)(const uint8_t*,size_t,int,uint8_t*,size_t,uint8_t*);
        auto convert=reinterpret_cast<Convert>(GetProcAddress(rpu,"wdv_convert_rpu_v1"));if(!convert)throw Failure{E_NOINTERFACE,"RPU ABI"};
        auto prep_start=GetTickCount64(); std::vector<ComPtr<IMFSample>> frames;
        for(uint32_t i=0;i<count;i++){
            const auto&v=input[i];std::vector<uint8_t> nal(65536);uint8_t profile=0;
            auto selected=source_profile==5?profile_transport().forward:convert;
            if(!selected&&!fel)throw Failure{E_NOINTERFACE,"P5 RPU transport missing"};
            int bytes=fel?1:selected(v.rpu,v.rpu_len,v.escaped,nal.data(),nal.size(),&profile);
            if(source_profile==5&&(profile!=5||v.paired_el))throw Failure{MF_E_INVALIDMEDIATYPE,"P5 requires original P5 RPU and no EL"};if(fel)profile=8;if(bytes<1)throw Failure{MF_E_INVALIDMEDIATYPE,"RPU rejected (FEL/invalid/unsupported); preserve MPV original"};
            if(native_sdr10 && (profile!=8 || v.paired_el || fel))throw Failure{MF_E_INVALIDMEDIATYPE,"P8 SDR10 requires original P8 RPU and no enhancement layer"};
            if(hlg && profile!=8)throw Failure{MF_E_INVALIDMEDIATYPE,"HLG contract requires original P8 RPU"};
            if(profile==7&&!v.paired_el)throw Failure{MF_E_INVALIDMEDIATYPE,"P7 requires an actually paired enhancement layer"};
            if(i&&v.pts<=input[i-1].pts)throw Failure{E_INVALIDARG,"non-monotonic frame PTS"};
            ComPtr<ID3D11Texture2D> tex;
            if(gpu){
                auto*source=static_cast<ID3D11Texture2D*>(gpu[i].input_texture);
                D3D11_TEXTURE2D_DESC src{};source->GetDesc(&src);
                if(src.Format!=DXGI_FORMAT_P010 || src.Width<w || src.Height<h || gpu[i].input_subresource>=src.ArraySize*src.MipLevels)throw Failure{E_INVALIDARG,"invalid GPU P010 input"};
                ComPtr<ID3D11Device> owner;source->GetDevice(&owner);if(owner.Get()!=shared_device)throw Failure{E_INVALIDARG,"GPU device mismatch"};
                D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_P010;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                CK(d3d->CreateTexture2D(&td,nullptr,&tex));
                auto& shared_input=input_shared[input_slot++%8];
                D3D11_BOX box{0,0,0,w,h,1};
                {ContextLease lock(caller_mt.Get());
                 SurfaceLease access(shared_input,true,0,1);
                 caller_context->CopySubresourceRegion(shared_input.caller.Get(),0,0,0,0,source,gpu[i].input_subresource,&box);access.release();}
                {SurfaceLease access(shared_input,false,1,0);
                 context->CopyResource(tex.Get(),shared_input.renderer.Get());access.release();}
            }else{
            std::vector<uint8_t> packed(size_t(w)*h*3);
            for(uint32_t row=0;row<h;row++)memcpy(packed.data()+size_t(row)*w*2,v.y+size_t(row)*v.stride_y,w*2);
            for(uint32_t row=0;row<h/2;row++)memcpy(packed.data()+size_t(w)*h*2+size_t(row)*w*2,v.uv+size_t(row)*v.stride_uv,w*2);
            D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_P010;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initial{packed.data(),w*2,w*h*3};CK(d3d->CreateTexture2D(&td,&initial,&tex));
            }
            if(fel)reconstruct_fel(i,tex,nal,bytes);
            ComPtr<IMFSample>s;ComPtr<IMFMediaBuffer>b;CK(MFCreateSample(&s));CK(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D),tex.Get(),0,FALSE,&b));DWORD cur=0,max=0;CK(b->GetCurrentLength(&cur));CK(b->GetMaxLength(&max));CK(b->SetCurrentLength(w*h*3));CK(s->AddBuffer(b.Get()));CK(s->SetSampleTime(v.pts-origin));CK(s->SetSampleDuration(v.duration));
            ComPtr<IMFCollection> units;ComPtr<IMFSample> unit;ComPtr<IMFMediaBuffer> nb;CK(MFCreateCollection(&units));CK(MFCreateSample(&unit));CK(MFCreateMemoryBuffer(bytes,&nb));BYTE*p=nullptr;CK(nb->Lock(&p,nullptr,nullptr));memcpy(p,nal.data(),bytes);CK(nb->Unlock());CK(nb->SetCurrentLength(bytes));CK(unit->AddBuffer(nb.Get()));CK(unit->SetSampleTime(v.pts-origin));CK(unit->SetUINT32(MFSampleExtension_ForwardedDecodeUnitType,MF_DECODE_UNIT_NAL));CK(units->AddElement(unit.Get()));if(rpu_enabled && !(hlg && WDV_TEST_OMIT_RPU))CK(s->SetUnknown(MFSampleExtension_ForwardedDecodeUnits,units.Get()));frames.push_back(s);
        }
        prep_ms+=GetTickCount64()-prep_start;return frames;
    }
};

struct PersistentSession {
    unsigned source_profile=8; bool native_sdr10=false;
    bool el_enabled=true,rpu_enabled=true;

    const wdv_fel_frame_v10*fel=nullptr;
    ComPtr<ID3D11Device> gpu_device; const wdv_gpu_frame_v5*gpu=nullptr;
    uint64_t prep_ms=0,copy_ms=0; bool fp16=false; bool relative_srgb=false; bool hlg=false; uint32_t fps_n=24000,fps_d=1001,chroma=0;
    std::mutex gate;std::condition_variable cv;std::thread thread;
    bool stop=false,pending=false,finished=false;int rc=-1;std::string error;
    const wdv_frame_v2*in=nullptr;wdv_output_v2*out=nullptr;uint32_t n=0,w=0,h=0;
    PersistentSession(bool is_hlg=false,uint32_t fn=24000,uint32_t fd=1001,uint32_t cs=0,ID3D11Device*dev=nullptr,bool use_float=false,bool use_relative=false):gpu_device(dev),fp16(use_float),relative_srgb(use_relative),hlg(is_hlg),fps_n(fn),fps_d(fd),chroma(cs){thread=std::thread([this]{loop();});}
    ~PersistentSession(){{std::lock_guard<std::mutex>l(gate);stop=true;cv.notify_all();}thread.join();}
    void loop(){
        HRESULT co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);HRESULT mf=SUCCEEDED(co)?MFStartup(MF_VERSION):co;
        std::unique_ptr<PersistentEngine> engine;
        for(;;){
            {std::unique_lock<std::mutex>l(gate);cv.wait(l,[&]{return stop||pending;});if(stop)break;}
            std::string why;int result=-2;
            try{
                CK(co);CK(mf);
                if(engine&&((engine->rpu_enabled!=rpu_enabled)||(bool(engine->fel)!=bool(fel))||engine->w!=w||engine->h!=h||std::llabs(in[0].pts-engine->last_end)>((hlg||gpu)?std::max<LONGLONG>(2000,(10000000LL*fps_d/fps_n)/2):2000)))engine.reset();
                if(!engine)engine=std::make_unique<PersistentEngine>(in,n,w,h,out,hlg,fps_n,fps_d,chroma,gpu,gpu_device.Get(),fp16,relative_srgb);
                else{engine->input=in;engine->output=out;engine->count=n;engine->gpu=gpu;}
                engine->el_enabled=el_enabled;engine->rpu_enabled=rpu_enabled;
                engine->source_profile=source_profile;engine->native_sdr10=native_sdr10;engine->fel=fel;
                result=engine->run();
            }catch(const Failure&e){std::ostringstream s;s<<e.site<<" HRESULT=0x"<<std::hex<<uint32_t(e.hr);why=s.str();engine.reset();}
            catch(const winrt::hresult_error&e){why="Windows Dolby service: "+winrt::to_string(e.message());engine.reset();}
            catch(const std::exception&e){why=e.what();engine.reset();}
            {std::lock_guard<std::mutex>l(gate);rc=result;error=why;pending=false;finished=true;cv.notify_all();}
        }
        engine.reset();if(SUCCEEDED(mf))MFShutdown();if(SUCCEEDED(co))CoUninitialize();
    }
};
extern "C" __declspec(dllexport) void* wdv_create_v3(){try{return new PersistentSession;}catch(...){return nullptr;}}
extern "C" __declspec(dllexport) void wdv_destroy_v3(void*ctx){delete static_cast<PersistentSession*>(ctx);}
extern "C" __declspec(dllexport) int wdv_render_v3(void*ctx,const wdv_frame_v2*in,uint32_t n,uint32_t w,uint32_t h,wdv_output_v2*out,char*err,uint32_t cap){
    if(!ctx||!in||!out||!n||n>8||w<2||h<2||(w&1)||(h&1)||w>3840||h>2160)return -1;
    for(uint32_t i=0;i<n;i++)if(!in[i].y||!in[i].uv||!in[i].rpu||in[i].rpu_len>65536||in[i].stride_y<int(w*2)||in[i].stride_uv<int(w*2)||!out[i].pixels||out[i].stride<int(w*4)||in[i].duration<=0)return -1;
    auto*p=static_cast<PersistentSession*>(ctx);std::unique_lock<std::mutex>l(p->gate);if(p->pending)return -3;
    p->fel=nullptr;p->in=in;p->n=n;p->w=w;p->h=h;p->out=out;p->pending=true;p->finished=false;p->cv.notify_all();p->cv.wait(l,[&]{return p->finished;});
    if(err&&cap){size_t nbytes=std::min<size_t>(cap-1,p->error.size());memcpy(err,p->error.data(),nbytes);err[nbytes]=0;}return p->rc;
}

extern "C" __declspec(dllexport) void* wdv_create_hlg_v4(uint32_t fn,uint32_t fd,uint32_t cs){if(!fn||!fd||cs>1||double(fn)/fd>240)return nullptr;try{return new PersistentSession(true,fn,fd,cs);}catch(...){return nullptr;}}

extern "C" __declspec(dllexport) void* wdv_create_gpu_v5(void*device,uint32_t fn,uint32_t fd,uint32_t cs){
    if(!device||!fn||!fd||cs>1||double(fn)/fd>240)return nullptr;
    try{return new PersistentSession(true,fn,fd,cs,static_cast<ID3D11Device*>(device));}catch(...){return nullptr;}
}
// Explicit transfer ABI. 1 = ST2084/PQ, 2 = HLG. Legacy v5 stays HLG.
extern "C" __declspec(dllexport) void* wdv_create_gpu_v6(void*device,uint32_t fn,uint32_t fd,uint32_t cs,uint32_t transfer){
    if(!device||!fn||!fd||cs>1||double(fn)/fd>240||(transfer!=1&&transfer!=2))return nullptr;
    try{return new PersistentSession(transfer==2,fn,fd,cs,static_cast<ID3D11Device*>(device));}catch(...){return nullptr;}
}
extern "C" __declspec(dllexport) int wdv_render_gpu_v5(void*ctx,const wdv_frame_v2*in,const wdv_gpu_frame_v5*gpu,uint32_t n,uint32_t w,uint32_t h,wdv_output_v2*out,char*err,uint32_t cap){
    if(!ctx||!in||!gpu||!out||!n||n>8||w<2||h<2||(w&1)||(h&1)||w>3840||h>2160)return -1;
    auto*p=static_cast<PersistentSession*>(ctx);
    for(uint32_t i=0;i<n;i++){
        if(!gpu[i].input_texture||!gpu[i].output_texture||!in[i].rpu||!in[i].rpu_len||in[i].rpu_len>65536||in[i].duration<=0)return -1;
        auto*tex=static_cast<ID3D11Texture2D*>(gpu[i].output_texture);D3D11_TEXTURE2D_DESC d{};tex->GetDesc(&d);ComPtr<ID3D11Device> owner;tex->GetDevice(&owner);
        DXGI_FORMAT expected=p->fp16?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_B8G8R8A8_UNORM;
        if(owner.Get()!=p->gpu_device.Get()||d.Format!=expected||d.Width!=w||d.Height!=h||d.ArraySize!=1||!(d.BindFlags&D3D11_BIND_RENDER_TARGET))return -1;
    }
    std::unique_lock<std::mutex>l(p->gate);if(p->pending)return -3;
    p->fel=nullptr;p->in=in;p->gpu=gpu;p->n=n;p->w=w;p->h=h;p->out=out;p->pending=true;p->finished=false;p->cv.notify_all();p->cv.wait(l,[&]{return p->finished;});
    if(err&&cap){size_t bytes=std::min<size_t>(cap-1,p->error.size());memcpy(err,p->error.data(),bytes);err[bytes]=0;}return p->rc;
}

// v7 opts into FP16 extended linear RGB. v5/v6 retain their BGRA contract.
extern "C" __declspec(dllexport) void* wdv_create_gpu_v7(void*device,uint32_t fn,uint32_t fd,uint32_t cs,uint32_t transfer){
    if(!device||!fn||!fd||cs>1||double(fn)/fd>240||(transfer!=1&&transfer!=2))return nullptr;
    try{return new PersistentSession(transfer==2,fn,fd,cs,static_cast<ID3D11Device*>(device),true);}catch(...){return nullptr;}
}

// v9 is HLG-only. Old v5/v6 BGRA and v7 scRGB contracts are unchanged.
// Output is display-relative linear encoded as sRGB in an extended FP16 texture.
extern "C" __declspec(dllexport) void* wdv_create_gpu_v9(void*device,uint32_t fn,uint32_t fd,uint32_t cs,uint32_t transfer){
    if(!device||!fn||!fd||cs>1||double(fn)/fd>240||transfer!=2)return nullptr;
    try{return new PersistentSession(true,fn,fd,cs,static_cast<ID3D11Device*>(device),true,true);}catch(...){return nullptr;}
}

// Explicit opt-in handoff; legacy v5/v6/v7/v9 layouts and paths unchanged.
extern "C" __declspec(dllexport) int wdv_render_gpu_fel_v10(void*ctx,const wdv_frame_v2*in,const wdv_gpu_frame_v5*gpu,const wdv_fel_frame_v10*fel,uint32_t n,uint32_t w,uint32_t h,wdv_output_v2*out,char*err,uint32_t cap){
    if(!ctx||!fel||!in||!gpu||!out||!n||n>8||w<2||h<2||(w&1)||(h&1)||w>3840||h>2160)return -1;
    auto*p=static_cast<PersistentSession*>(ctx);
    for(uint32_t i=0;i<n;i++){
        if(!fel[i].el_texture||!fel[i].bl_repr||!fel[i].el_repr||fel[i].pl_api_ver!=387||fel[i].el_w<2||fel[i].el_h<2||fel[i].el_w%2||fel[i].el_h%2||!in[i].paired_el)return -1;
        if(!gpu[i].input_texture||!gpu[i].output_texture||!in[i].rpu||!in[i].rpu_len||in[i].rpu_len>65536||in[i].duration<=0)return -1;
        auto*tex=static_cast<ID3D11Texture2D*>(gpu[i].output_texture);D3D11_TEXTURE2D_DESC d{};tex->GetDesc(&d);ComPtr<ID3D11Device> owner;tex->GetDevice(&owner);
        DXGI_FORMAT expected=p->fp16?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_B8G8R8A8_UNORM;
        if(owner.Get()!=p->gpu_device.Get()||d.Format!=expected||d.Width!=w||d.Height!=h||d.ArraySize!=1||!(d.BindFlags&D3D11_BIND_RENDER_TARGET))return -1;
    }
    std::unique_lock<std::mutex>l(p->gate);if(p->pending)return -3;
    p->fel=fel;p->in=in;p->gpu=gpu;p->n=n;p->w=w;p->h=h;p->out=out;p->pending=true;p->finished=false;p->cv.notify_all();p->cv.wait(l,[&]{return p->finished;});
    if(err&&cap){size_t bytes=std::min<size_t>(cap-1,p->error.size());memcpy(err,p->error.data(),bytes);err[bytes]=0;}return p->rc;
}


// Additive P5 entry: full-range IPT-PQ-C2 input, relative-sRGB FP16 output.
// P7/FEL and P8 retain their existing versioned entry points.
extern "C" __declspec(dllexport) void* wdv_create_gpu_profile_v11(void*device,uint32_t fn,uint32_t fd,uint32_t cs,uint32_t profile){
 if(!device||!fn||!fd||cs>1||double(fn)/fd>240||profile!=5)return nullptr;
 try{auto*p=new PersistentSession(false,fn,fd,cs,static_cast<ID3D11Device*>(device),true,true);p->source_profile=profile;return p;}catch(...){return nullptr;}
}

// v12: independent P8.1/P8.4 native SDR10 contract. Input transfer stays PQ/HLG.
// P5 v11 and legacy P7/FEL entry points retain their original behavior.
extern "C" __declspec(dllexport) void* wdv_create_gpu_sdr_v12(void*device,uint32_t fn,uint32_t fd,uint32_t cs,uint32_t transfer){
 if(!device||!fn||!fd||cs>1||double(fn)/fd>240||(transfer!=1&&transfer!=2))return nullptr;
 try{auto*p=new PersistentSession(transfer==2,fn,fd,cs,static_cast<ID3D11Device*>(device),true,true);p->native_sdr10=true;return p;}catch(...){return nullptr;}
}

// Called only between submitted batches. Changing RPU creates a clean Windows
// effect generation so an omitted RPU cannot reuse the previous frame metadata.
// FEL reconstruction still consumes mandatory reshape/NLQ coefficients upstream.
extern "C" __declspec(dllexport) int wdv_set_comparison_v14(void*ctx,int el,int rpu){
 if(!ctx || (el!=0 && el!=1) || (rpu!=0 && rpu!=1))return -1;
 auto*p=static_cast<PersistentSession*>(ctx);std::lock_guard<std::mutex>l(p->gate);
 if(p->pending || (!rpu && p->source_profile==5))return -1;
 p->el_enabled=el;p->rpu_enabled=rpu;return 0;
}
