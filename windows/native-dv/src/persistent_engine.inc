#include <d3d11_1.h>
#ifndef WDV_TEST_OMIT_RPU
#define WDV_TEST_OMIT_RPU 0
#endif
// Separate MediaEngine device: the MPV decoder/VO context is never given to MF.
// NT shared textures move pixels only on GPU. Key transitions establish producer
// completion before the consumer copies; caller textures remain alive for the job.
struct SharedGpuSurface {
    ComPtr<ID3D11Texture2D> caller, renderer;
    ComPtr<IDXGIKeyedMutex> caller_key, renderer_key;
    void init(ID3D11Device* a, ID3D11Device* b, UINT w, UINT h, DXGI_FORMAT format) {
        D3D11_TEXTURE2D_DESC d{}; d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;
        d.Format=format;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        d.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        CK(a->CreateTexture2D(&d,nullptr,&caller));
        ComPtr<IDXGIResource1> resource;CK(caller.As(&resource));HANDLE handle=nullptr;
        CK(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handle));
        ComPtr<ID3D11Device1> device;HRESULT hr=b->QueryInterface(IID_PPV_ARGS(&device));
        if(SUCCEEDED(hr))hr=device->OpenSharedResource1(handle,IID_PPV_ARGS(&renderer));
        CloseHandle(handle);CK(hr);CK(caller.As(&caller_key));CK(renderer.As(&renderer_key));
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
struct ContextLease {
    ID3D10Multithread* mt;
    ContextLease(ID3D10Multithread*m):mt(m){mt->Enter();}
    ~ContextLease(){mt->Leave();}
};
static double gpu_clock_ms(){LARGE_INTEGER c,f;QueryPerformanceCounter(&c);QueryPerformanceFrequency(&f);return 1000.0*double(c.QuadPart)/f.QuadPart;}
struct PersistentEngine {
    uint64_t prep_ms=0,copy_ms=0; bool hlg=false; uint32_t fps_n=24000,fps_d=1001,chroma=0;
    const wdv_gpu_frame_v5*gpu=nullptr; ID3D11Device*shared_device=nullptr;
    ComPtr<ID3D11DeviceContext> caller_context;
    ComPtr<ID3D10Multithread> caller_mt;
    SharedGpuSurface input_shared[8], output_shared;
    HWND window=nullptr;
    ComPtr<ID3D11Device> d3d;ComPtr<ID3D11DeviceContext> context;
    ComPtr<IMFDXGIDeviceManager> manager;
    ComPtr<IMFMediaEngine> engine;ComPtr<IMFMediaEngineEx> ex;
    ComPtr<SourceExtension> extension;ComPtr<ID3D11Texture2D> texture,staging;
    bool metadata=false,ready=false,stepReady=false;HRESULT error=S_OK;
    ComPtr<RawSource> queue;
    LONGLONG origin=0,previous=-1,last_end=0;bool need_step=false;uint64_t rendered=0;
    uint32_t w,h,count;const wdv_frame_v2*input;wdv_output_v2*output;
    PersistentEngine(const wdv_frame_v2*i,uint32_t n,uint32_t width,uint32_t height,wdv_output_v2*o,bool is_hlg=false,uint32_t fn=24000,uint32_t fd=1001,uint32_t cs=0,const wdv_gpu_frame_v5*g=nullptr,ID3D11Device*dev=nullptr):hlg(is_hlg),fps_n(fn),fps_d(fd),chroma(cs),gpu(g),shared_device(dev),w(width),h(height),count(n),input(i),output(o){origin=i[0].pts-10000000;}
    ~PersistentEngine(){if(engine)engine->Shutdown();if(extension&&extension->source)extension->source->Shutdown();if(window)DestroyWindow(window);}
    static LRESULT CALLBACK wnd(HWND hw,UINT m,WPARAM wp,LPARAM lp){
        auto*p=reinterpret_cast<PersistentEngine*>(GetWindowLongPtrW(hw,GWLP_USERDATA));
        if(m==WM_NCCREATE){p=static_cast<PersistentEngine*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hw,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}
        if(p&&m==EV){if(wp==MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA){p->metadata=true;p->error=p->engine->SetCurrentTime(1);}if(wp==MF_MEDIA_ENGINE_EVENT_FIRSTFRAMEREADY)p->ready=true;if(wp==MF_MEDIA_ENGINE_EVENT_FRAMESTEPCOMPLETED)p->stepReady=true;if(wp==MF_MEDIA_ENGINE_EVENT_ERROR)p->error=FAILED(HRESULT(lp))?HRESULT(lp):E_FAIL;return 0;}
        return DefWindowProcW(hw,m,wp,lp);
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
            output_shared.init(shared_device,d3d.Get(),w,h,DXGI_FORMAT_B8G8R8A8_UNORM);
        }else CK(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_VIDEO_SUPPORT|D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&d3d,nullptr,&context));
        ComPtr<ID3D10Multithread> mt;CK(d3d.As(&mt));mt->SetMultithreadProtected(TRUE);
        UINT token=0;CK(MFCreateDXGIDeviceManager(&token,&manager));CK(manager->ResetDevice(d3d.Get(),token));
        ComPtr<IMFMediaType> type;CK(MFCreateMediaType(&type));CK(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));CK(type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_P010));
        CK(MFSetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,w,h));CK(MFSetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,fps_n,fps_d));CK(MFSetAttributeRatio(type.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1));
        CK(type->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));CK(type->SetUINT32(MF_MT_VIDEO_PRIMARIES,MFVideoPrimaries_BT2020));CK(type->SetUINT32(MF_MT_TRANSFER_FUNCTION,hlg?MFVideoTransFunc_HLG:MFVideoTransFunc_2084));CK(type->SetUINT32(MF_MT_YUV_MATRIX,MFVideoTransferMatrix_BT2020_10));CK(type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,MFNominalRange_16_235));CK(type->SetString(MF_MT_VIDEO_RENDERER_EXTENSION_PROFILE,L"dvhe.08"));
        if(hlg)CK(type->SetUINT32(MF_MT_VIDEO_CHROMA_SITING,MFVideoChromaSubsampling_ProgressiveChroma | (chroma?MFVideoChromaSubsampling_Cosited:MFVideoChromaSubsampling_MPEG2)));
        queue=Make<RawSource>(make_frames(),type.Get(),86400LL*10000000,true);
        extension=Make<SourceExtension>();extension->source=queue;
        auto notify=Make<Notify>();notify->window=window;ComPtr<IMFAttributes> a;CK(MFCreateAttributes(&a,8));CK(a->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT,DXGI_FORMAT_B8G8R8A8_UNORM));CK(a->SetUnknown(MF_MEDIA_ENGINE_CALLBACK,notify.Get()));CK(a->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER,manager.Get()));CK(a->SetUnknown(MF_MEDIA_ENGINE_EXTENSION,extension.Get()));CK(a->SetUINT64(MF_MEDIA_ENGINE_OPM_HWND,UINT64(UINT_PTR(window))));CK(a->SetGUID(MF_MEDIA_ENGINE_BROWSER_COMPATIBILITY_MODE,MF_MEDIA_ENGINE_BROWSER_COMPATIBILITY_MODE_IE_EDGE));
        ComPtr<IMFMediaEngineClassFactory> factory;CK(CoCreateInstance(CLSID_MFMediaEngineClassFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));CK(factory->CreateInstance(gpu ? MF_MEDIA_ENGINE_REAL_TIME_MODE : 0,a.Get(),&engine));CK(engine.As(&ex));CK(engine->SetMuted(TRUE));
        BSTR uri=SysAllocString(L"mpv-frame-source");HRESULT hr=engine->SetSource(uri);SysFreeString(uri);CK(hr);
        {D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;CK(d3d->CreateTexture2D(&td,nullptr,&texture));if(!gpu){td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;CK(d3d->CreateTexture2D(&td,nullptr,&staging));}}
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
                    trender=gpu_clock_ms();
                    {KeyLease access(output_shared.renderer_key.Get(),0,1);
                     context->CopyResource(output_shared.renderer.Get(),texture.Get());access.release();}
                    tshared=gpu_clock_ms();
                    {KeyLease access(output_shared.caller_key.Get(),1,0);
                     ContextLease lock(caller_mt.Get());
                     caller_context->CopyResource(static_cast<ID3D11Texture2D*>(gpu[next].output_texture),output_shared.caller.Get());access.release();}
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
        if(!dolbyLoaded())throw Failure{E_FAIL,"Dolby RendererEffect not loaded"};
        last_end=input[count-1].pts+input[count-1].duration;queue->retire(previous); wchar_t path[32768]; if(GetEnvironmentVariableW(L"P84_TIMING_PATH",path,32768)){std::ofstream log(fs::path(path),std::ios::app);log<<rendered<<","<<GetTickCount64()-begin_ms<<","<<prep_ms<<","<<copy_ms<<"\n";} return int(next);
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
            int bytes=convert(v.rpu,v.rpu_len,v.escaped,nal.data(),nal.size(),&profile);if(bytes<1)throw Failure{MF_E_INVALIDMEDIATYPE,"RPU rejected (FEL/invalid/unsupported); preserve MPV original"};
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
                D3D11_BOX box{0,0,0,w,h,1};
                {KeyLease access(input_shared[i].caller_key.Get(),0,1);
                 ContextLease lock(caller_mt.Get());
                 caller_context->CopySubresourceRegion(input_shared[i].caller.Get(),0,0,0,0,source,gpu[i].input_subresource,&box);access.release();}
                {KeyLease access(input_shared[i].renderer_key.Get(),1,0);
                 context->CopyResource(tex.Get(),input_shared[i].renderer.Get());access.release();}
            }else{
            std::vector<uint8_t> packed(size_t(w)*h*3);
            for(uint32_t row=0;row<h;row++)memcpy(packed.data()+size_t(row)*w*2,v.y+size_t(row)*v.stride_y,w*2);
            for(uint32_t row=0;row<h/2;row++)memcpy(packed.data()+size_t(w)*h*2+size_t(row)*w*2,v.uv+size_t(row)*v.stride_uv,w*2);
            D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_P010;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initial{packed.data(),w*2,w*h*3};CK(d3d->CreateTexture2D(&td,&initial,&tex));
            }
            ComPtr<IMFSample>s;ComPtr<IMFMediaBuffer>b;CK(MFCreateSample(&s));CK(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D),tex.Get(),0,FALSE,&b));DWORD cur=0,max=0;CK(b->GetCurrentLength(&cur));CK(b->GetMaxLength(&max));CK(b->SetCurrentLength(w*h*3));CK(s->AddBuffer(b.Get()));CK(s->SetSampleTime(v.pts-origin));CK(s->SetSampleDuration(v.duration));
            ComPtr<IMFCollection> units;ComPtr<IMFSample> unit;ComPtr<IMFMediaBuffer> nb;CK(MFCreateCollection(&units));CK(MFCreateSample(&unit));CK(MFCreateMemoryBuffer(bytes,&nb));BYTE*p=nullptr;CK(nb->Lock(&p,nullptr,nullptr));memcpy(p,nal.data(),bytes);CK(nb->Unlock());CK(nb->SetCurrentLength(bytes));CK(unit->AddBuffer(nb.Get()));CK(unit->SetSampleTime(v.pts-origin));CK(unit->SetUINT32(MFSampleExtension_ForwardedDecodeUnitType,MF_DECODE_UNIT_NAL));CK(units->AddElement(unit.Get()));if(!(hlg && WDV_TEST_OMIT_RPU))CK(s->SetUnknown(MFSampleExtension_ForwardedDecodeUnits,units.Get()));frames.push_back(s);
        }
        prep_ms+=GetTickCount64()-prep_start;return frames;
    }
};

struct PersistentSession {
    ComPtr<ID3D11Device> gpu_device; const wdv_gpu_frame_v5*gpu=nullptr;
    uint64_t prep_ms=0,copy_ms=0; bool hlg=false; uint32_t fps_n=24000,fps_d=1001,chroma=0;
    std::mutex gate;std::condition_variable cv;std::thread thread;
    bool stop=false,pending=false,finished=false;int rc=-1;std::string error;
    const wdv_frame_v2*in=nullptr;wdv_output_v2*out=nullptr;uint32_t n=0,w=0,h=0;
    PersistentSession(bool is_hlg=false,uint32_t fn=24000,uint32_t fd=1001,uint32_t cs=0,ID3D11Device*dev=nullptr):gpu_device(dev),hlg(is_hlg),fps_n(fn),fps_d(fd),chroma(cs){thread=std::thread([this]{loop();});}
    ~PersistentSession(){{std::lock_guard<std::mutex>l(gate);stop=true;cv.notify_all();}thread.join();}
    void loop(){
        HRESULT co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);HRESULT mf=SUCCEEDED(co)?MFStartup(MF_VERSION):co;
        std::unique_ptr<PersistentEngine> engine;
        for(;;){
            {std::unique_lock<std::mutex>l(gate);cv.wait(l,[&]{return stop||pending;});if(stop)break;}
            std::string why;int result=-2;
            try{
                CK(co);CK(mf);
                if(engine&&(engine->w!=w||engine->h!=h||std::llabs(in[0].pts-engine->last_end)>(hlg?std::max<LONGLONG>(2000,(10000000LL*fps_d/fps_n)/2):2000)))engine.reset();
                if(!engine)engine=std::make_unique<PersistentEngine>(in,n,w,h,out,hlg,fps_n,fps_d,chroma,gpu,gpu_device.Get());
                else{engine->input=in;engine->output=out;engine->count=n;engine->gpu=gpu;}
                result=engine->run();
            }catch(const Failure&e){std::ostringstream s;s<<e.site<<" HRESULT=0x"<<std::hex<<uint32_t(e.hr);why=s.str();engine.reset();}
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
    p->in=in;p->n=n;p->w=w;p->h=h;p->out=out;p->pending=true;p->finished=false;p->cv.notify_all();p->cv.wait(l,[&]{return p->finished;});
    if(err&&cap){size_t nbytes=std::min<size_t>(cap-1,p->error.size());memcpy(err,p->error.data(),nbytes);err[nbytes]=0;}return p->rc;
}

extern "C" __declspec(dllexport) void* wdv_create_hlg_v4(uint32_t fn,uint32_t fd,uint32_t cs){if(!fn||!fd||cs>1||double(fn)/fd>240)return nullptr;try{return new PersistentSession(true,fn,fd,cs);}catch(...){return nullptr;}}

extern "C" __declspec(dllexport) void* wdv_create_gpu_v5(void*device,uint32_t fn,uint32_t fd,uint32_t cs){
    if(!device||!fn||!fd||cs>1||double(fn)/fd>240)return nullptr;
    try{return new PersistentSession(true,fn,fd,cs,static_cast<ID3D11Device*>(device));}catch(...){return nullptr;}
}
extern "C" __declspec(dllexport) int wdv_render_gpu_v5(void*ctx,const wdv_frame_v2*in,const wdv_gpu_frame_v5*gpu,uint32_t n,uint32_t w,uint32_t h,wdv_output_v2*out,char*err,uint32_t cap){
    if(!ctx||!in||!gpu||!out||!n||n>8||w<2||h<2||(w&1)||(h&1)||w>3840||h>2160)return -1;
    auto*p=static_cast<PersistentSession*>(ctx);
    for(uint32_t i=0;i<n;i++){
        if(!gpu[i].input_texture||!gpu[i].output_texture||!in[i].rpu||!in[i].rpu_len||in[i].rpu_len>65536||in[i].duration<=0)return -1;
        auto*tex=static_cast<ID3D11Texture2D*>(gpu[i].output_texture);D3D11_TEXTURE2D_DESC d{};tex->GetDesc(&d);ComPtr<ID3D11Device> owner;tex->GetDevice(&owner);
        if(owner.Get()!=p->gpu_device.Get()||d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM||d.Width!=w||d.Height!=h||d.ArraySize!=1||!(d.BindFlags&D3D11_BIND_RENDER_TARGET))return -1;
    }
    std::unique_lock<std::mutex>l(p->gate);if(p->pending)return -3;
    p->in=in;p->gpu=gpu;p->n=n;p->w=w;p->h=h;p->out=out;p->pending=true;p->finished=false;p->cv.notify_all();p->cv.wait(l,[&]{return p->finished;});
    if(err&&cap){size_t bytes=std::min<size_t>(cap-1,p->error.size());memcpy(err,p->error.data(),bytes);err[bytes]=0;}return p->rc;
}
