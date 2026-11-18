// Optional Windows Media Foundation backend. No bundled Dolby implementation.
// Public MediaEngine / DirectComposition API usage follows Microsoft's MIT
// MediaEngineDCompWin32Sample. See THIRD_PARTY.md for source and attribution.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfmediaengine.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <DispatcherQueue.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.AppService.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.System.h>
#include <Windows.Graphics.Display.Interop.h>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <memory>
#include <d3d11.h>
#include <d3d10.h>
#include <dcomp.h>
#include <psapi.h>
#include <wrl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::Make;
namespace fs = std::filesystem;

struct Failure { HRESULT hr; std::string site; };
void check(HRESULT hr, const char* site) { if (FAILED(hr)) throw Failure{hr,site}; }
#define CK(expr) check((expr), #expr)
std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);
    std::string s(n,'\0'); WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),s.data(),n,nullptr,nullptr); return s;
}
std::string quote(const std::string& s) {
    std::ostringstream o; o << '"';
    for (unsigned char c:s) {
        if(c=='"'||c=='\\') o << '\\' << c;
        else if(c<32) o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
        else o << c;
    } o << '"'; return o.str();
}
std::wstring lower(std::wstring s) { for(auto& c:s) c=wchar_t(towlower(c)); return s; }
std::wstring normalized(const std::wstring& p) { return lower(fs::path(p).lexically_normal().wstring()); }
bool within(std::wstring p,std::wstring root) {
    p=normalized(p); root=normalized(root);
    while(root.size()>3 && (root.back()=='\\'||root.back()=='/')) root.pop_back();
    return p==root || (p.size()>root.size() && p.compare(0,root.size(),root)==0 &&
      (root.back()=='\\'||p[root.size()]=='\\'||p[root.size()]=='/'));
}
struct Args {
    std::wstring input,status,control; std::vector<std::wstring> deny;
    bool probe=false,validate=false,mute=false,headless=false,paused=false,audit=false;
    double seconds=0,start=0,volume=1; DWORD parent=0;
};
Args parse(int argc,wchar_t** argv) {
    Args a;
    for(int i=1;i<argc;i++) {
        std::wstring x=argv[i];
        auto value=[&](){if(++i>=argc) throw std::runtime_error("missing argument");return std::wstring(argv[i]);};
        if(x==L"--probe")a.probe=true;
        else if(x==L"--audit-frames")a.audit=true;
        else if(x==L"--validate-input"){a.validate=true;a.input=value();}
        else if(x==L"--input")a.input=value();
        else if(x==L"--status")a.status=value();
        else if(x==L"--control")a.control=value();
        else if(x==L"--deny-root")a.deny.push_back(value());
        else if(x==L"--seconds")a.seconds=std::stod(value());
        else if(x==L"--start")a.start=std::stod(value());
        else if(x==L"--volume")a.volume=std::stod(value());
        else if(x==L"--parent")a.parent=std::stoul(value());
        else if(x==L"--mute")a.mute=true;
        else if(x==L"--paused")a.paused=true;
        else if(x==L"--headless")a.headless=true;
        else throw std::runtime_error("unknown option");
    }
    if(!std::isfinite(a.seconds)||a.seconds<0||!std::isfinite(a.start)||a.start<0||
       !std::isfinite(a.volume)||a.volume<0||a.volume>1)throw std::runtime_error("invalid numeric argument");
    return a;
}
std::string guard(const Args& a) {
    const auto& p=a.input;
    // Lexical checks FIRST: do not resolve blocked drives, UNC, devices or URLs.
    if(p.size()<3||!iswalpha(p[0])||p[1]!=L':'||(p[2]!=L'\\'&&p[2]!=L'/')||
       p.find(L':',2)!=std::wstring::npos) return "only absolute local files are allowed";
    for(const auto& root:a.deny) if(!root.empty()&&within(p,root))return "input is blocked by the backup/optical safety gate";
    // Checking the drive type does not open the disc or enumerate its files.
    if(GetDriveTypeW((p.substr(0,2)+L"\\").c_str())==DRIVE_CDROM)return "optical drives are not accepted by this backend";
    // Refuse directory junctions/symlinks rather than following into a blocked root.
    fs::path part=fs::path(p).root_path();
    for(const auto& comp:fs::path(p).relative_path()) {
        part/=comp; DWORD attr=GetFileAttributesW(part.c_str());
        if(attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_REPARSE_POINT))return "reparse-point inputs are not allowed";
    }
    return {};
}
std::vector<std::wstring> profiles() {
    IMFActivate** list=nullptr; UINT32 count=0; std::vector<std::wstring> out;
    CK(MFTEnumEx(MFT_CATEGORY_VIDEO_RENDERER_EFFECT,MFT_ENUM_FLAG_ALL|MFT_ENUM_FLAG_UNTRUSTED_STOREMFT,nullptr,nullptr,&list,&count));
    for(UINT32 i=0;i<count;i++) {
        PROPVARIANT v; PropVariantInit(&v);
        if(SUCCEEDED(list[i]->GetItem(MFT_ENUM_VIDEO_RENDERER_EXTENSION_PROFILE,&v))) {
            if(v.vt==(VT_VECTOR|VT_LPWSTR))for(ULONG j=0;j<v.calpwstr.cElems;j++)out.emplace_back(v.calpwstr.pElems[j]);
            else if(v.vt==VT_LPWSTR)out.emplace_back(v.pwszVal);
        }
        PropVariantClear(&v); list[i]->Release();
    } CoTaskMemFree(list); std::sort(out.begin(),out.end()); out.erase(std::unique(out.begin(),out.end()),out.end());return out;
}
// Bounded ISO-BMFF metadata walk: skips mdat, only walks known container boxes.
// A config record is evidence of a source profile, not of decoded RPU/FEL support.
uint64_t be(const unsigned char* p,int n){uint64_t v=0;while(n--)v=(v<<8)|*p++;return v;}
struct Dovi { int profile=0; bool rpu=false,el=false,bl=false; };
Dovi boxes(std::ifstream& f,uint64_t begin,uint64_t end,int depth,size_t& budget) {
    Dovi result; if(depth>12)return result;
    for(uint64_t pos=begin;pos+8<=end && budget<1024*1024; ) {
        unsigned char h[16]{}; f.seekg(pos); f.read((char*)h,8); if(!f)return result;budget+=8;
        uint64_t size=be(h,4),header=8; std::string type((char*)h+4,4);
        if(size==1){f.read((char*)h+8,8);if(!f)return result;size=be(h+8,8);header=16;budget+=8;}
        if(size==0)size=end-pos;
        if(size<header||size>end-pos)return result;
        uint64_t child=pos+header; bool recurse=false;
        if(type=="moov"||type=="trak"||type=="mdia"||type=="minf"||type=="stbl")recurse=true;
        else if(type=="stsd"){child+=8;recurse=true;}
        else if(type=="hvc1"||type=="hev1"||type=="dvh1"||type=="dvhe"||type=="avc1"||type=="dvav"||type=="dva1"){child+=78;recurse=true;}
        else if((type=="dvcC"||type=="dvvC") && size>=header+4) {
            unsigned char c[4]{};f.read((char*)c,4);budget+=4;
            if(f){result.profile=c[2]>>1;result.rpu=(c[3]&4)!=0;result.el=(c[3]&2)!=0;result.bl=(c[3]&1)!=0;return result;}
        }
        if(recurse&&child<pos+size){result=boxes(f,child,pos+size,depth+1,budget);if(result.profile)return result;}
        pos+=size;
    }return result;
}
Dovi readDovi(const std::wstring& path) {
    auto ext=lower(fs::path(path).extension().wstring());
    if(ext!=L".mp4"&&ext!=L".m4v"&&ext!=L".mov")return {};
    std::ifstream f(fs::path(path),std::ios::binary);if(!f)return {};
    f.seekg(0,std::ios::end);auto size=f.tellg();size_t budget=0;
    return size>0?boxes(f,0,uint64_t(size),0,budget):Dovi{};
}
bool dolbyLoaded() {
    HMODULE mods[2048];DWORD bytes=0;
    if(EnumProcessModules(GetCurrentProcess(),mods,sizeof(mods),&bytes))
        for(DWORD i=0;i<std::min<DWORD>(bytes/sizeof(HMODULE),2048);i++) {
            wchar_t path[32768]{};
            if(GetModuleFileNameW(mods[i],path,32768)){
                auto p=lower(path);
                if(p.find(L"dolby")!=std::wstring::npos)return true;
            }
        }
    return false;
}
// Transparent, opt-in decoder-output observer. Never rewrites pixels or metadata.
// It counts actual forwarded NAL units; it cannot prove a downstream Dolby
// algorithm consumed them or that the physical display emitted a DV signal.
struct FrameAudit {
    std::atomic<uint64_t> frames{0},rpuFrames{0},rpuUnits{0};
};
class FrameObserver final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,IMFTransform> {
    std::mutex mutex; ComPtr<IMFAttributes> attrs; ComPtr<IMFMediaType> input,output;
    ComPtr<IMFSample> pending; FrameAudit* audit;
    static bool raw(IMFMediaType* t){GUID major{},sub{};return t && SUCCEEDED(t->GetMajorType(&major))&&major==MFMediaType_Video && SUCCEEDED(t->GetGUID(MF_MT_SUBTYPE,&sub))&&(sub==MFVideoFormat_P010||sub==MFVideoFormat_NV12||sub==MFVideoFormat_ARGB32||sub==MFVideoFormat_RGB32);}
    HRESULT copyType(IMFMediaType* t,ComPtr<IMFMediaType>& dest){dest.Reset();if(!t)return S_OK;HRESULT hr=MFCreateMediaType(&dest);return FAILED(hr)?hr:t->CopyAllItems(dest.Get());}
public:
    explicit FrameObserver(FrameAudit* destination):audit(destination){CK(MFCreateAttributes(&attrs,2));CK(attrs->SetUINT32(MF_SA_D3D11_AWARE,TRUE));}
    STDMETHODIMP GetStreamLimits(DWORD*a,DWORD*b,DWORD*c,DWORD*d)override{if(!a||!b||!c||!d)return E_POINTER;*a=*b=*c=*d=1;return S_OK;}
    STDMETHODIMP GetStreamCount(DWORD*a,DWORD*b)override{if(!a||!b)return E_POINTER;*a=*b=1;return S_OK;}
    STDMETHODIMP GetStreamIDs(DWORD,DWORD*,DWORD,DWORD*)override{return E_NOTIMPL;}
    STDMETHODIMP GetInputStreamInfo(DWORD id,MFT_INPUT_STREAM_INFO*p)override{if(id)return MF_E_INVALIDSTREAMNUMBER;if(!p)return E_POINTER;*p={};p->dwFlags=MFT_INPUT_STREAM_WHOLE_SAMPLES|MFT_INPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER;return S_OK;}
    STDMETHODIMP GetOutputStreamInfo(DWORD id,MFT_OUTPUT_STREAM_INFO*p)override{if(id)return MF_E_INVALIDSTREAMNUMBER;if(!p)return E_POINTER;*p={};p->dwFlags=MFT_OUTPUT_STREAM_WHOLE_SAMPLES|MFT_OUTPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER|MFT_OUTPUT_STREAM_PROVIDES_SAMPLES;return S_OK;}
    STDMETHODIMP GetAttributes(IMFAttributes**p)override{return attrs.CopyTo(p);}
    STDMETHODIMP GetInputStreamAttributes(DWORD,IMFAttributes**)override{return E_NOTIMPL;}
    STDMETHODIMP GetOutputStreamAttributes(DWORD,IMFAttributes**)override{return E_NOTIMPL;}
    STDMETHODIMP DeleteInputStream(DWORD)override{return E_NOTIMPL;}
    STDMETHODIMP AddInputStreams(DWORD,DWORD*)override{return E_NOTIMPL;}
    STDMETHODIMP GetInputAvailableType(DWORD id,DWORD index,IMFMediaType**p)override{
        if(id)return MF_E_INVALIDSTREAMNUMBER;if(!p)return E_POINTER;std::lock_guard<std::mutex> lock(mutex);
        if(input){if(index)return MF_E_NO_MORE_TYPES;return input.CopyTo(p);}if(index>1)return MF_E_NO_MORE_TYPES;
        ComPtr<IMFMediaType> t;HRESULT hr=MFCreateMediaType(&t);if(FAILED(hr))return hr;t->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);t->SetGUID(MF_MT_SUBTYPE,index?MFVideoFormat_NV12:MFVideoFormat_P010);return t.CopyTo(p);}
    STDMETHODIMP GetOutputAvailableType(DWORD id,DWORD index,IMFMediaType**p)override{if(id)return MF_E_INVALIDSTREAMNUMBER;std::lock_guard<std::mutex> lock(mutex);if(index)return MF_E_NO_MORE_TYPES;return input?input.CopyTo(p):MF_E_TRANSFORM_TYPE_NOT_SET;}
    STDMETHODIMP SetInputType(DWORD id,IMFMediaType*t,DWORD flags)override{if(id)return MF_E_INVALIDSTREAMNUMBER;std::lock_guard<std::mutex> lock(mutex);if(t&&!raw(t))return MF_E_INVALIDMEDIATYPE;if(flags&MFT_SET_TYPE_TEST_ONLY)return S_OK;return copyType(t,input);}
    STDMETHODIMP SetOutputType(DWORD id,IMFMediaType*t,DWORD flags)override{if(id)return MF_E_INVALIDSTREAMNUMBER;std::lock_guard<std::mutex> lock(mutex);if(t){if(!input)return MF_E_TRANSFORM_TYPE_NOT_SET;BOOL equal=FALSE;input->Compare(t,MF_ATTRIBUTES_MATCH_ALL_ITEMS,&equal);if(!equal)return MF_E_INVALIDMEDIATYPE;}if(flags&MFT_SET_TYPE_TEST_ONLY)return S_OK;return copyType(t,output);}
    STDMETHODIMP GetInputCurrentType(DWORD id,IMFMediaType**p)override{if(id)return MF_E_INVALIDSTREAMNUMBER;std::lock_guard<std::mutex> lock(mutex);return input?input.CopyTo(p):MF_E_TRANSFORM_TYPE_NOT_SET;}
    STDMETHODIMP GetOutputCurrentType(DWORD id,IMFMediaType**p)override{if(id)return MF_E_INVALIDSTREAMNUMBER;std::lock_guard<std::mutex> lock(mutex);return output?output.CopyTo(p):MF_E_TRANSFORM_TYPE_NOT_SET;}
    STDMETHODIMP GetInputStatus(DWORD id,DWORD*p)override{if(id)return MF_E_INVALIDSTREAMNUMBER;if(!p)return E_POINTER;std::lock_guard<std::mutex> lock(mutex);*p=pending?0:MFT_INPUT_STATUS_ACCEPT_DATA;return S_OK;}
    STDMETHODIMP GetOutputStatus(DWORD*p)override{if(!p)return E_POINTER;std::lock_guard<std::mutex> lock(mutex);*p=pending?MFT_OUTPUT_STATUS_SAMPLE_READY:0;return S_OK;}
    STDMETHODIMP SetOutputBounds(LONGLONG,LONGLONG)override{return E_NOTIMPL;}
    STDMETHODIMP ProcessEvent(DWORD,IMFMediaEvent*)override{return E_NOTIMPL;}
    STDMETHODIMP ProcessMessage(MFT_MESSAGE_TYPE msg,ULONG_PTR)override{std::lock_guard<std::mutex> lock(mutex);if(msg==MFT_MESSAGE_COMMAND_FLUSH)pending.Reset();return S_OK;}
    STDMETHODIMP ProcessInput(DWORD id,IMFSample*sample,DWORD flags)override{
        if(id)return MF_E_INVALIDSTREAMNUMBER;if(!sample)return E_POINTER;if(flags)return E_INVALIDARG;
        std::lock_guard<std::mutex> lock(mutex);if(!input||!output)return MF_E_TRANSFORM_TYPE_NOT_SET;if(pending)return MF_E_NOTACCEPTING;
        audit->frames++;uint64_t found=0;ComPtr<IMFCollection> units;
        if(SUCCEEDED(sample->GetUnknown(MFSampleExtension_ForwardedDecodeUnits,IID_PPV_ARGS(&units)))){
            DWORD count=0;units->GetElementCount(&count);
            for(DWORD i=0;i<count;i++){
                ComPtr<IUnknown> item;ComPtr<IMFSample> unit;ComPtr<IMFMediaBuffer> buffer;UINT32 type=99;
                if(FAILED(units->GetElement(i,&item))||FAILED(item.As(&unit))||FAILED(unit->GetUINT32(MFSampleExtension_ForwardedDecodeUnitType,&type))||type!=MF_DECODE_UNIT_NAL||FAILED(unit->ConvertToContiguousBuffer(&buffer)))continue;
                BYTE*data=nullptr;DWORD size=0;if(SUCCEEDED(buffer->Lock(&data,nullptr,&size))){
                    // Decoder forwards de-escaped NAL bytes (SDK contract). Accept
                    // a start-code prefix too, but never infer RPU from all units.
                    DWORD offset=0;if(size>=4&&data[0]==0&&data[1]==0){if(data[2]==1)offset=3;else if(data[2]==0&&data[3]==1)offset=4;}
                    if(size>=offset+2&&((data[offset]>>1)&63)==62)found++;
                    buffer->Unlock();
                }
            }
        }
        audit->rpuUnits+=found;if(found)audit->rpuFrames++;pending=sample;return S_OK;
    }
    STDMETHODIMP ProcessOutput(DWORD flags,DWORD count,MFT_OUTPUT_DATA_BUFFER*p,DWORD*status)override{
        if(flags||count!=1)return E_INVALIDARG;if(!p||!status)return E_POINTER;std::lock_guard<std::mutex> lock(mutex);
        if(!pending)return MF_E_TRANSFORM_NEED_MORE_INPUT;if(p->pSample)return E_INVALIDARG;
        p->pSample=pending.Detach();p->dwStatus=0;p->pEvents=nullptr;*status=0;return S_OK;
    }
};

struct State {
    std::string state="starting",error;HRESULT hr=S_OK;
    bool opened=false,attached=false,module=false,tagged=false,forward=false,paused=false;
    bool auditEnabled=false; uint64_t decodedFrames=0,rpuFrames=0,rpuUnits=0;
    std::wstring profile; Dovi dovi; uint64_t frames=0,dropped=0;double position=0;
    std::string json()const{
        std::ostringstream o;o<<std::boolalpha<<"{\"backend\":\"windows-media-engine\",\"state\":"<<quote(state)
          <<",\"error\":"<<quote(error)<<",\"hresult\":"<<long(hr)<<",\"media_opened\":"<<opened
          <<",\"source_dv_profile\":"<<quote(utf8(profile))<<",\"dovi_config_rpu_present\":"<<dovi.rpu
          <<",\"dovi_config_el_present\":"<<dovi.el<<",\"dovi_config_bl_present\":"<<dovi.bl
          <<",\"renderer_profile_requested\":"<<tagged<<",\"nalu_forwarding_requested\":"<<forward
          <<",\"frames_rendered\":"<<frames<<",\"frames_dropped\":"<<dropped
          <<",\"frameserver_ready\":"<<attached<<",\"position\":"<<(std::isfinite(position)?position:0)
          <<",\"paused\":"<<paused<<",\"dolby_module_loaded\":"<<module
          <<",\"frame_audit_enabled\":"<<auditEnabled<<",\"decoded_frames_observed\":"<<decodedFrames<<",\"decoded_rpu_frames\":"<<rpuFrames
          <<",\"forwarded_rpu_units\":"<<rpuUnits<<",\"rpu_forwarding_observed\":"<<(rpuUnits>0)
          <<",\"rpu_processing_verified\":false,\"native_dv_output_verified\":false}\n";return o.str();
    }
};
void writeStatus(const Args&a,const State&s) {
    if(a.status.empty())return;
    const auto parent=fs::path(a.status).parent_path();
    if(!parent.empty())fs::create_directories(parent);
    fs::path temp=a.status+L".tmp";
    {std::ofstream f(temp,std::ios::binary);if(!f)throw std::runtime_error("cannot write status");f<<s.json();}
    if(!MoveFileExW(temp.c_str(),a.status.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("cannot commit status");
}
constexpr UINT EV=WM_APP+31;
class Notify final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,IMFMediaEngineNotify> {
public:HWND window=nullptr;
    HRESULT STDMETHODCALLTYPE EventNotify(DWORD event,DWORD_PTR p1,DWORD p2)override {
        if(event==MF_MEDIA_ENGINE_EVENT_NOTIFYSTABLESTATE){SetEvent((HANDLE)p1);return S_OK;}
        PostMessageW(window,EV,event,LPARAM(p2));return S_OK;
    }
};
class SourceExtension final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,IMFMediaEngineExtension> {
public:ComPtr<IMFMediaSource> source;
    HRESULT STDMETHODCALLTYPE CanPlayType(BOOL,BSTR,MF_MEDIA_ENGINE_CANPLAY* answer)override{*answer=MF_MEDIA_ENGINE_CANPLAY_MAYBE;return S_OK;}
    HRESULT STDMETHODCALLTYPE BeginCreateObject(BSTR,IMFByteStream*,MF_OBJECT_TYPE type,IUnknown** cookie,IMFAsyncCallback* cb,IUnknown* state)override {
        if(cookie)*cookie=nullptr;if(type!=MF_OBJECT_MEDIASOURCE||!source)return MF_E_UNEXPECTED;
        ComPtr<IMFAsyncResult> result;HRESULT hr=MFCreateAsyncResult(source.Get(),cb,state,&result);
        if(FAILED(hr))return hr;result->SetStatus(S_OK);return cb->Invoke(result.Get());
    }
    HRESULT STDMETHODCALLTYPE EndCreateObject(IMFAsyncResult* result,IUnknown** object)override{
        HRESULT hr=result->GetStatus();return FAILED(hr)?hr:result->GetObject(object);
    }
    HRESULT STDMETHODCALLTYPE CancelObjectCreation(IUnknown*)override{return E_NOTIMPL;}
};

// Probe only: expose already decoded P010 + original sample attributes through
// the public IMFMediaSource contract. No compressed data enters MediaEngine.
// Reader here is a test producer; a production MPV adapter must replace it with
// a bounded MPV-owned frame queue and flush on seek/file/disc-title changes.
class RawSource;
class RawStream final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,IMFMediaStream> {
    RawSource* owner;ComPtr<IMFMediaEventQueue> events;ComPtr<IMFStreamDescriptor> descriptor;
public:
    RawStream(RawSource*s,IMFMediaType*t):owner(s){CK(MFCreateEventQueue(&events));IMFMediaType* types[]={t};CK(MFCreateStreamDescriptor(1,1,types,&descriptor));ComPtr<IMFMediaTypeHandler> h;CK(descriptor->GetMediaTypeHandler(&h));CK(h->SetCurrentMediaType(t));}
    STDMETHODIMP GetEvent(DWORD f,IMFMediaEvent**e)override{return events->GetEvent(f,e);}
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback*c,IUnknown*s)override{return events->BeginGetEvent(c,s);}
    STDMETHODIMP EndGetEvent(IMFAsyncResult*r,IMFMediaEvent**e)override{return events->EndGetEvent(r,e);}
    STDMETHODIMP QueueEvent(MediaEventType t,REFGUID g,HRESULT h,const PROPVARIANT*v)override{return events->QueueEventParamVar(t,g,h,v);}
    STDMETHODIMP GetMediaSource(IMFMediaSource**s)override;
    STDMETHODIMP GetStreamDescriptor(IMFStreamDescriptor**s)override{return descriptor.CopyTo(s);}
    STDMETHODIMP RequestSample(IUnknown*token)override;
    void shutdown(){events->Shutdown();owner=nullptr;}
    HRESULT sample(IMFSample*s){return events->QueueEventParamUnk(MEMediaSample,GUID_NULL,S_OK,s);}
};
class RawSource final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,IMFMediaSource> {
    friend class RawStream;
    ComPtr<IMFMediaEventQueue> events;ComPtr<IMFPresentationDescriptor> pd;
    std::deque<ComPtr<IMFSample>> frames;size_t cursor=0;
    std::deque<ComPtr<IUnknown>> tokens;ComPtr<RawStream> stream;
    bool announced=false,dead=false,streaming=false;std::mutex gate;
    HRESULT deliver(IUnknown*token){auto s=frames[cursor++];if(token)s->SetUnknown(MFSampleExtension_Token,token);else s->DeleteItem(MFSampleExtension_Token);return stream->sample(s.Get());}
public:
    RawSource(std::vector<ComPtr<IMFSample>> samples,IMFMediaType*type,LONGLONG duration,bool continuous=false):frames(samples.begin(),samples.end()),streaming(continuous){CK(MFCreateEventQueue(&events));stream=Make<RawStream>(this,type);ComPtr<IMFStreamDescriptor> sd;CK(stream->GetStreamDescriptor(&sd));IMFStreamDescriptor* desc[]={sd.Get()};CK(MFCreatePresentationDescriptor(1,desc,&pd));CK(pd->SelectStream(0));CK(pd->SetUINT64(MF_PD_DURATION,duration));}
    HRESULT append(const std::vector<ComPtr<IMFSample>>&samples){std::lock_guard<std::mutex>lock(gate);if(dead)return MF_E_SHUTDOWN;if(frames.size()+samples.size()>24)return MF_E_NOTACCEPTING;for(auto&s:samples)frames.push_back(s);while(!tokens.empty()&&cursor<frames.size()){auto t=tokens.front();tokens.pop_front();HRESULT hr=deliver(t.Get());if(FAILED(hr))return hr;}return S_OK;}
    void retire(LONGLONG pts){std::lock_guard<std::mutex>lock(gate);while(cursor>2&&!frames.empty()){LONGLONG t=0;frames.front()->GetSampleTime(&t);if(t>=pts)break;frames.pop_front();cursor--;}}
    STDMETHODIMP GetEvent(DWORD f,IMFMediaEvent**e)override{return events->GetEvent(f,e);}
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback*c,IUnknown*s)override{return events->BeginGetEvent(c,s);}
    STDMETHODIMP EndGetEvent(IMFAsyncResult*r,IMFMediaEvent**e)override{return events->EndGetEvent(r,e);}
    STDMETHODIMP QueueEvent(MediaEventType t,REFGUID g,HRESULT h,const PROPVARIANT*v)override{return events->QueueEventParamVar(t,g,h,v);}
    STDMETHODIMP GetCharacteristics(DWORD*c)override{if(!c)return E_POINTER;*c=MFMEDIASOURCE_CAN_SEEK|MFMEDIASOURCE_CAN_PAUSE;return S_OK;}
    STDMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor**p)override{return pd->Clone(p);}
    STDMETHODIMP Start(IMFPresentationDescriptor*,const GUID*format,const PROPVARIANT*pos)override{
        std::lock_guard<std::mutex>lock(gate);if(dead)return MF_E_SHUTDOWN;if(format&&*format!=GUID_NULL)return MF_E_UNSUPPORTED_TIME_FORMAT;
        bool seeking=announced&&pos&&pos->vt==VT_I8;if(!announced||seeking){cursor=0;tokens.clear();}if(pos&&pos->vt==VT_I8){while(cursor<frames.size()){LONGLONG pts=0;frames[cursor]->GetSampleTime(&pts);if(pts>=pos->hVal.QuadPart)break;cursor++;}}
        HRESULT hr=events->QueueEventParamUnk(announced?MEUpdatedStream:MENewStream,GUID_NULL,S_OK,stream.Get());if(FAILED(hr))return hr;announced=true;
        hr=stream->QueueEvent(seeking?MEStreamSeeked:MEStreamStarted,GUID_NULL,S_OK,pos);if(FAILED(hr))return hr;return QueueEvent(seeking?MESourceSeeked:MESourceStarted,GUID_NULL,S_OK,pos);
    }
    STDMETHODIMP Stop()override{stream->QueueEvent(MEStreamStopped,GUID_NULL,S_OK,nullptr);return QueueEvent(MESourceStopped,GUID_NULL,S_OK,nullptr);}
    STDMETHODIMP Pause()override{stream->QueueEvent(MEStreamPaused,GUID_NULL,S_OK,nullptr);return QueueEvent(MESourcePaused,GUID_NULL,S_OK,nullptr);}
    STDMETHODIMP Shutdown()override{std::lock_guard<std::mutex>lock(gate);if(dead)return S_OK;dead=true;tokens.clear();frames.clear();stream->shutdown();return events->Shutdown();}
    HRESULT request(IUnknown*token){std::lock_guard<std::mutex>lock(gate);if(dead)return MF_E_SHUTDOWN;if(cursor<frames.size())return deliver(token);if(streaming){if(tokens.size()>=32)return MF_E_NOTACCEPTING;tokens.emplace_back(token);return S_OK;}stream->QueueEvent(MEEndOfStream,GUID_NULL,S_OK,nullptr);return QueueEvent(MEEndOfPresentation,GUID_NULL,S_OK,nullptr);}
};
HRESULT RawStream::GetMediaSource(IMFMediaSource**s){if(!s)return E_POINTER;if(!owner)return MF_E_SHUTDOWN;*s=owner;owner->AddRef();return S_OK;}
HRESULT RawStream::RequestSample(IUnknown*token){return owner?owner->request(token):MF_E_SHUTDOWN;}

#include "batch_engine.inc"
#include "persistent_engine.inc"
