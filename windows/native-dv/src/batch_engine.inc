#include "wdv_frames.h"
#include <thread>

struct BatchEngine {
    HWND window=nullptr;
    ComPtr<ID3D11Device> d3d;ComPtr<ID3D11DeviceContext> context;
    ComPtr<IMFDXGIDeviceManager> manager;
    ComPtr<IMFMediaEngine> engine;ComPtr<IMFMediaEngineEx> ex;
    ComPtr<SourceExtension> extension;ComPtr<ID3D11Texture2D> texture,staging;
    bool metadata=false,ready=false,stepReady=false;HRESULT error=S_OK;
    uint32_t w,h,count;const wdv_frame_v2*input;wdv_output_v2*output;
    BatchEngine(const wdv_frame_v2*i,uint32_t n,uint32_t width,uint32_t height,wdv_output_v2*o):w(width),h(height),count(n),input(i),output(o){}
    ~BatchEngine(){if(engine)engine->Shutdown();if(extension&&extension->source)extension->source->Shutdown();if(window)DestroyWindow(window);}
    static LRESULT CALLBACK wnd(HWND hw,UINT m,WPARAM wp,LPARAM lp){
        auto*p=reinterpret_cast<BatchEngine*>(GetWindowLongPtrW(hw,GWLP_USERDATA));
        if(m==WM_NCCREATE){p=static_cast<BatchEngine*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hw,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}
        if(p&&m==EV){std::cout<<"wdv_event="<<wp<<std::endl;if(wp==MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA){p->metadata=true;p->error=p->engine->SetCurrentTime(p->input[0].pts/10000000.0);}if(wp==MF_MEDIA_ENGINE_EVENT_FIRSTFRAMEREADY)p->ready=true;if(wp==MF_MEDIA_ENGINE_EVENT_FRAMESTEPCOMPLETED)p->stepReady=true;if(wp==MF_MEDIA_ENGINE_EVENT_ERROR)p->error=FAILED(HRESULT(lp))?HRESULT(lp):E_FAIL;return 0;}
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
        CK(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_VIDEO_SUPPORT|D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&d3d,nullptr,&context));
        ComPtr<ID3D10Multithread> mt;CK(d3d.As(&mt));mt->SetMultithreadProtected(TRUE);
        UINT token=0;CK(MFCreateDXGIDeviceManager(&token,&manager));CK(manager->ResetDevice(d3d.Get(),token));
        ComPtr<IMFMediaType> type;CK(MFCreateMediaType(&type));CK(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));CK(type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_P010));
        CK(MFSetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,w,h));CK(MFSetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,24000,1001));CK(MFSetAttributeRatio(type.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1));
        CK(type->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));CK(type->SetUINT32(MF_MT_VIDEO_PRIMARIES,MFVideoPrimaries_BT2020));CK(type->SetUINT32(MF_MT_TRANSFER_FUNCTION,MFVideoTransFunc_2084));CK(type->SetUINT32(MF_MT_YUV_MATRIX,MFVideoTransferMatrix_BT2020_10));CK(type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,MFNominalRange_16_235));CK(type->SetString(MF_MT_VIDEO_RENDERER_EXTENSION_PROFILE,L"dvhe.08"));
        std::vector<ComPtr<IMFSample>> frames;
        for(uint32_t i=0;i<count;i++){
            const auto&v=input[i];std::vector<uint8_t> nal(65536);uint8_t profile=0;
            int bytes=convert(v.rpu,v.rpu_len,v.escaped,nal.data(),nal.size(),&profile);if(bytes<1)throw Failure{MF_E_INVALIDMEDIATYPE,"RPU rejected (FEL/invalid/unsupported); preserve MPV original"};
            if(profile==7&&!v.paired_el)throw Failure{MF_E_INVALIDMEDIATYPE,"P7 requires an actually paired enhancement layer"};
            if(i&&v.pts<=input[i-1].pts)throw Failure{E_INVALIDARG,"non-monotonic frame PTS"};
            std::vector<uint8_t> packed(size_t(w)*h*3);
            for(uint32_t row=0;row<h;row++)memcpy(packed.data()+size_t(row)*w*2,v.y+size_t(row)*v.stride_y,w*2);
            for(uint32_t row=0;row<h/2;row++)memcpy(packed.data()+size_t(w)*h*2+size_t(row)*w*2,v.uv+size_t(row)*v.stride_uv,w*2);
            D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_P010;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initial{packed.data(),w*2,w*h*3};ComPtr<ID3D11Texture2D> tex;CK(d3d->CreateTexture2D(&td,&initial,&tex));
            ComPtr<IMFSample>s;ComPtr<IMFMediaBuffer>b;CK(MFCreateSample(&s));CK(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D),tex.Get(),0,FALSE,&b));DWORD cur=0,max=0;CK(b->GetCurrentLength(&cur));CK(b->GetMaxLength(&max));std::cout<<"surface_bytes current="<<cur<<" max="<<max<<std::endl;CK(b->SetCurrentLength(w*h*3));CK(s->AddBuffer(b.Get()));CK(s->SetSampleTime(v.pts));CK(s->SetSampleDuration(v.duration));
            ComPtr<IMFCollection> units;ComPtr<IMFSample> unit;ComPtr<IMFMediaBuffer> nb;CK(MFCreateCollection(&units));CK(MFCreateSample(&unit));CK(MFCreateMemoryBuffer(bytes,&nb));BYTE*p=nullptr;CK(nb->Lock(&p,nullptr,nullptr));memcpy(p,nal.data(),bytes);CK(nb->Unlock());CK(nb->SetCurrentLength(bytes));CK(unit->AddBuffer(nb.Get()));CK(unit->SetSampleTime(v.pts));CK(unit->SetUINT32(MFSampleExtension_ForwardedDecodeUnitType,MF_DECODE_UNIT_NAL));CK(units->AddElement(unit.Get()));CK(s->SetUnknown(MFSampleExtension_ForwardedDecodeUnits,units.Get()));frames.push_back(s);
        }
        extension=Make<SourceExtension>();extension->source=Make<RawSource>(frames,type.Get(),input[count-1].pts+input[count-1].duration);
        auto notify=Make<Notify>();notify->window=window;ComPtr<IMFAttributes> a;CK(MFCreateAttributes(&a,8));CK(a->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT,DXGI_FORMAT_B8G8R8A8_UNORM));CK(a->SetUnknown(MF_MEDIA_ENGINE_CALLBACK,notify.Get()));CK(a->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER,manager.Get()));CK(a->SetUnknown(MF_MEDIA_ENGINE_EXTENSION,extension.Get()));CK(a->SetUINT64(MF_MEDIA_ENGINE_OPM_HWND,UINT64(UINT_PTR(window))));CK(a->SetGUID(MF_MEDIA_ENGINE_BROWSER_COMPATIBILITY_MODE,MF_MEDIA_ENGINE_BROWSER_COMPATIBILITY_MODE_IE_EDGE));
        ComPtr<IMFMediaEngineClassFactory> factory;CK(CoCreateInstance(CLSID_MFMediaEngineClassFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));CK(factory->CreateInstance(0,a.Get(),&engine));CK(engine.As(&ex));CK(engine->SetMuted(TRUE));
        BSTR uri=SysAllocString(L"mpv-frame-source");HRESULT hr=engine->SetSource(uri);SysFreeString(uri);CK(hr);
        D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;CK(d3d->CreateTexture2D(&td,nullptr,&texture));td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;CK(d3d->CreateTexture2D(&td,nullptr,&staging));
    }
    int run(){
        init();auto deadline=GetTickCount64()+6000;uint32_t next=0;LONGLONG previous=-1;MSG msg{};
        while(next<count&&GetTickCount64()<deadline){
            while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}CK(error);
            LONGLONG tick=0;if(metadata&&ready&&engine->OnVideoStreamTick(&tick)==S_OK&&tick!=previous&&tick>=input[0].pts){
                LONGLONG expected=input[next].pts;if(std::llabs(tick-expected)>2)throw Failure{E_FAIL,"frame server returned wrong PTS expected="+std::to_string(expected)+" actual="+std::to_string(tick)+" index="+std::to_string(next)};
                RECT rect{0,0,LONG(w),LONG(h)};MFARGB border{};HRESULT transfer=engine->TransferVideoFrame(texture.Get(),nullptr,&rect,&border);std::cout<<"transfer=0x"<<std::hex<<uint32_t(transfer)<<std::dec<<" tick="<<tick<<std::endl;CK(transfer);if(transfer!=S_OK){Sleep(10);continue;}context->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE map{};CK(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));for(uint32_t row=0;row<h;row++)memcpy(output[next].pixels+size_t(row)*output[next].stride,static_cast<uint8_t*>(map.pData)+size_t(row)*map.RowPitch,w*4);context->Unmap(staging.Get(),0);output[next].pts=input[next].pts;previous=tick;next++;stepReady=false;if(next<count)CK(ex->FrameStep(TRUE));
            }
            Sleep(1);
        }
        if(next!=count)throw Failure{HRESULT_FROM_WIN32(WAIT_TIMEOUT),"frame sequence timeout"};
        if(!dolbyLoaded())throw Failure{E_FAIL,"Dolby RendererEffect was not loaded"};
        return int(next);
    }
};
extern "C" __declspec(dllexport) int wdv_render_batch_v2(const wdv_frame_v2*in,uint32_t n,uint32_t w,uint32_t h,wdv_output_v2*out,char*error,uint32_t cap){
    if(!in||!out||!n||n>8||w<2||h<2||(w&1)||(h&1)||w>3840||h>2160)return -1;
    for(uint32_t i=0;i<n;i++)if(!in[i].y||!in[i].uv||!in[i].rpu||in[i].rpu_len>65536||in[i].stride_y<int(w*2)||in[i].stride_uv<int(w*2)||!out[i].pixels||out[i].stride<int(w*4)||in[i].duration<=0)return -1;
    int result=-2;std::string why;
    std::thread worker([&]{HRESULT co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);bool mf=false;try{CK(co);CK(MFStartup(MF_VERSION));mf=true;{BatchEngine engine(in,n,w,h,out);result=engine.run();}}catch(const Failure&e){std::ostringstream s;s<<e.site<<" HRESULT=0x"<<std::hex<<uint32_t(e.hr);why=s.str();}catch(const std::exception&e){why=e.what();}if(mf)MFShutdown();if(SUCCEEDED(co))CoUninitialize();});worker.join();
    if(error&&cap){size_t len=std::min<size_t>(cap-1,why.size());memcpy(error,why.data(),len);error[len]=0;}return result;
}
